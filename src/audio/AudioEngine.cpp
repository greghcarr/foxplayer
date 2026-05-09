#include "AudioEngine.h"
#include "../Constants.h"

#if JUCE_WINDOWS
 #include "MediaFoundationReader.h"
#endif

namespace Stylus
{

AudioEngine::AudioEngine()
{
    // Register all built-in decoders plus CoreAudioFormat (macOS, handles MP3, AAC, ALAC, AIFF, WAV).
    formatManager_.registerBasicFormats(); // includes CoreAudioFormat on macOS

    // Initialise audio device using system defaults.
    auto result = deviceManager_.initialiseWithDefaultDevices(0, 2);
    jassert(result.isEmpty()); // empty string means success
    juce::ignoreUnused(result);

    // Background reader thread for AudioTransportSource. Decouples disk I/O
    // and decoding from the audio callback so a busy CPU/disk or an OS
    // priority dip (Cmd-Tab, Mission Control, Spotlight) can't starve the
    // audio thread into a dropout.
    readAheadThread_.startThread(juce::Thread::Priority::high);

    // Wire: deviceManager -> sourcePlayer -> transportSource.
    deviceManager_.addAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(&transportSource_);
    transportSource_.addChangeListener(this);

    // No periodic timer here: every state transition (play/pause/resume/stop/
    // seek/track-finished) calls onStateChanged synchronously below, and the
    // TransportBar runs its own 30 Hz timer for seek-bar visual updates while
    // playing. A second timer here was firing redundant transportBar.repaint()
    // and StatusBarItem state-set calls every 33 ms, even when nothing changed.
}

AudioEngine::~AudioEngine()
{
    transportSource_.removeChangeListener(this);
    transportSource_.setSource(nullptr);
    sourcePlayer_.setSource(nullptr);
    deviceManager_.removeAudioCallback(&sourcePlayer_);
    unloadCurrentReader();
}

void AudioEngine::play(const TrackInfo& track)
{
    DBG("AudioEngine::play - " + track.file.getFullPathName());
    loadTrack(track);
    if (!trackLoaded_)
    {
        DBG("AudioEngine::play - loadTrack failed, skipping track");
        juce::MessageManager::callAsync([this] { if (onTrackFailed) onTrackFailed(); });
        return;
    }
    transportSource_.setPosition(0.0);
    transportSource_.start();
    paused_ = false;
    DBG("AudioEngine::play - transport started");

    if (onTrackStarted) onTrackStarted(currentTrack_);
    if (onStateChanged) onStateChanged();
}

void AudioEngine::prepareTrackPaused(const TrackInfo& track, double elapsedSeconds)
{
    loadTrack(track);
    if (!trackLoaded_) return;

    const double len = transportSource_.getLengthInSeconds();
    const double pos = juce::jlimit(0.0, juce::jmax(0.0, len), elapsedSeconds);
    transportSource_.setPosition(pos);
    paused_ = true;

    if (onTrackStarted) onTrackStarted(currentTrack_);
    if (onStateChanged) onStateChanged();
}

void AudioEngine::pause()
{
    if (!trackLoaded_ || paused_) return;
    transportSource_.stop();
    paused_ = true;
    if (onStateChanged) onStateChanged();
}

void AudioEngine::resume()
{
    if (!trackLoaded_ || !paused_) return;
    transportSource_.start();
    paused_ = false;
    if (onStateChanged) onStateChanged();
}

void AudioEngine::stop()
{
    transportSource_.stop();
    transportSource_.setPosition(0.0);
    paused_ = false;
    if (onPlaybackStopped) onPlaybackStopped();
    if (onStateChanged)    onStateChanged();
}

void AudioEngine::setVolume(float gain)
{
    volume_ = juce::jlimit(0.0f, 1.0f, gain);
    // Volume slider should feel responsive: apply instantly, no fade.
    applyCombinedGain(false);
}

void AudioEngine::setVolumeNormalizationEnabled(bool enabled)
{
    if (normalizeVolume_ == enabled) return;
    normalizeVolume_ = enabled;
    // Normalisation toggle is a global decision the user makes - fade the
    // transition so an active track doesn't lurch between levels.
    applyCombinedGain(true);
}

void AudioEngine::updateCurrentTrackLufs(float lufs)
{
    if (currentTrack_.lufs == lufs && lufsKnown_) return;
    currentTrack_.lufs = lufs;
    lufsKnown_ = true;
    // Lazy LUFS just landed mid-track: fade from pre-roll to the measured
    // level so the correction reads as a smooth settle, not a jump.
    applyCombinedGain(true);
}

void AudioEngine::markLufsAnalysisFailed()
{
    if (lufsKnown_) return;   // already settled
    lufsKnown_ = true;        // we now know there's no measurement
    // Same rationale as updateCurrentTrackLufs: fade pre-roll back to
    // unity rather than snapping.
    applyCombinedGain(true);
}

float AudioEngine::computeTargetGain() const
{
    // Target loudness: -14 LUFS, the streaming-platform standard (Apple
    // Music / Spotify / YouTube Music all normalise around this). A track
    // measured at -14 plays through at unity; quieter tracks get amplified,
    // louder ones attenuated.
    constexpr float kTargetLufs = -14.0f;
    // Cap the boost / cut so a wildly miscalibrated lufs value can't push
    // the gain into clipping or near-silence. ±6 dB matches what most
    // streaming services apply in practice.
    constexpr float kMaxGainLin = 2.0f;     // +6 dB
    constexpr float kMinGainLin = 0.5f;     // -6 dB

    // When normalisation is on but the current track hasn't been analysed
    // yet, pre-roll at -3 dB instead of unity. Most loud-mastered tracks
    // will land near this once the lazy LUFS measurement arrives, so the
    // ramp from pre-roll to final is small and unobtrusive.
    constexpr float kPreAnalysisOffset = 0.7079f;   // 10^(-3/20) = -3 dB

    float trackOffset = 1.0f;
    if (normalizeVolume_)
    {
        if (currentTrack_.lufs != 0.0f)
        {
            const float dB = kTargetLufs - currentTrack_.lufs;
            trackOffset    = std::pow(10.0f, dB / 20.0f);
            trackOffset    = juce::jlimit(kMinGainLin, kMaxGainLin, trackOffset);
        }
        else if (! lufsKnown_)
        {
            trackOffset = kPreAnalysisOffset;
        }
        // else: lufsKnown_ true with lufs==0 means analysis ran but
        // produced no value (file unreadable / decode failure / silent
        // track). Fall through to unity.
    }
    return volume_ * trackOffset;
}

void AudioEngine::applyCombinedGain(bool smooth)
{
    const float target = computeTargetGain();

    if (! smooth)
    {
        // Immediate path: stop any in-progress ramp and snap to target.
        // JUCE's setGain still ramps internally over one audio block
        // (~10 ms), so even the "instant" branch isn't a hard click.
        rampTimer_.stopTimer();
        currentGain_ = target;
        transportSource_.setGain(target);
        return;
    }

    // Smooth path: nothing to do if we're already at the target.
    if (std::abs(target - currentGain_) < 1.0e-5f)
    {
        rampTimer_.stopTimer();
        return;
    }

    // (Re)start the ramp from the live gain - cleanly handles a new ramp
    // arriving mid-fade by treating wherever we are right now as the new
    // start point.
    rampStartGain_   = currentGain_;
    rampTargetGain_  = target;
    rampStartTimeMs_ = juce::Time::currentTimeMillis();
    if (! rampTimer_.isTimerRunning())
        rampTimer_.startTimerHz(60);
}

void AudioEngine::stepRamp()
{
    const auto elapsed = juce::Time::currentTimeMillis() - rampStartTimeMs_;
    float t = (float) elapsed / (float) kRampDurationMs;
    if (t >= 1.0f)
    {
        t = 1.0f;
        rampTimer_.stopTimer();
    }

    // Smoothstep ease-in-out so the ramp visibly accelerates and
    // decelerates rather than starting and ending abruptly.
    const float eased = t * t * (3.0f - 2.0f * t);

    // Interpolate in log (dB) space: a perceptually even fade between two
    // gains. Floors at -120 dB to keep std::log10(0) out of play.
    constexpr float kFloorDb = -120.0f;
    const float dbStart  = (rampStartGain_  > 0.0f) ? std::log10(rampStartGain_)  * 20.0f : kFloorDb;
    const float dbTarget = (rampTargetGain_ > 0.0f) ? std::log10(rampTargetGain_) * 20.0f : kFloorDb;
    const float db       = dbStart + (dbTarget - dbStart) * eased;
    const float g        = std::pow(10.0f, db / 20.0f);

    currentGain_ = g;
    transportSource_.setGain(g);
}

void AudioEngine::seekToNormalized(double position)
{
    const double len = transportSource_.getLengthInSeconds();
    if (len > 0.0)
        transportSource_.setPosition(juce::jlimit(0.0, len, position * len));
}

bool AudioEngine::isPlaying() const
{
    return trackLoaded_ && !paused_ && transportSource_.isPlaying();
}

bool AudioEngine::isPaused() const
{
    return trackLoaded_ && paused_;
}

double AudioEngine::normalizedPosition() const
{
    const double len = transportSource_.getLengthInSeconds();
    if (len <= 0.0) return 0.0;
    return juce::jlimit(0.0, 1.0, transportSource_.getCurrentPosition() / len);
}

double AudioEngine::elapsedSeconds() const
{
    return transportSource_.getCurrentPosition();
}

double AudioEngine::durationSeconds() const
{
    return transportSource_.getLengthInSeconds();
}

void AudioEngine::loadTrack(const TrackInfo& track)
{
    DBG("AudioEngine::loadTrack - stopping transport");
    loading_ = true;

    transportSource_.stop();
    transportSource_.setSource(nullptr);
    unloadCurrentReader();

    DBG("AudioEngine::loadTrack - creating reader for: " + track.file.getFullPathName());
    auto* reader = formatManager_.createReaderFor(track.file);

   #if JUCE_WINDOWS
    // JUCE's basic formats on Windows cover MP3 / WAV / AIFF / FLAC / Ogg.
    // For AAC / Apple Lossless / WMA we fall through to a Media Foundation
    // reader, which uses the OS-provided codecs. This keeps Apple-Music
    // libraries playable without bundling FFmpeg.
    if (reader == nullptr)
        reader = MediaFoundation::createReaderForFile(track.file);
   #endif

    if (reader == nullptr)
    {
        DBG("AudioEngine::loadTrack - ERROR: no reader for file (unsupported format or missing file)");
        trackLoaded_ = false;
        loading_ = false;
        return;
    }

    DBG("AudioEngine::loadTrack - reader created, sampleRate=" + juce::String(reader->sampleRate)
        + " length=" + juce::String(reader->lengthInSamples));

    currentTrack_ = track;
    // Tracks loaded with a non-zero LUFS already have a definitive measurement.
    // Tracks with lufs==0 are "unknown" - lazy analysis is expected to land
    // (or report a failure) shortly. applyCombinedGain reads this to decide
    // pre-roll vs measured vs unity.
    lufsKnown_ = (currentTrack_.lufs != 0.0f);
    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transportSource_.setSource(readerSource_.get(),
                               Constants::audioReadAheadBufferSize,
                               &readAheadThread_,
                               reader->sampleRate);
    trackLoaded_ = true;
    loading_ = false;
    // Lock in the per-track gain (user volume * LUFS offset if normalisation
    // is on) for the freshly-loaded track. Done after setSource so the
    // gain applies to the new transport state, not the unloaded one.
    // Instant: the transport hasn't started yet, so there's nothing to
    // fade from anyway, and a cross-track fade would be a separate
    // feature with different semantics.
    applyCombinedGain(/*smooth=*/ false);
    DBG("AudioEngine::loadTrack, done");
}

void AudioEngine::unloadCurrentReader()
{
    readerSource_.reset();
    trackLoaded_ = false;
}

void AudioEngine::changeListenerCallback(juce::ChangeBroadcaster* source)
{
    if (source != &transportSource_) return;
    if (loading_) return; // ignore state changes triggered by our own loadTrack()

    DBG("AudioEngine::changeListenerCallback - isPlaying=" + juce::String((int)transportSource_.isPlaying())
        + " paused=" + juce::String((int)paused_)
        + " trackLoaded=" + juce::String((int)trackLoaded_));

    // Natural end-of-track: transport has stopped but we did not pause it.
    if (!transportSource_.isPlaying() && !paused_ && trackLoaded_)
    {
        DBG("AudioEngine::changeListenerCallback - track finished naturally");
        if (onTrackFinished) onTrackFinished();
        if (onStateChanged)  onStateChanged();
    }
}

} // namespace Stylus
