#include "AudioEngine.h"

namespace FoxPlayer
{

AudioEngine::AudioEngine()
{
    // Register all built-in decoders plus CoreAudioFormat (macOS — handles MP3, AAC, ALAC, AIFF, WAV).
    formatManager_.registerBasicFormats(); // includes CoreAudioFormat on macOS

    // Initialise audio device using system defaults.
    auto result = deviceManager_.initialiseWithDefaultDevices(0, 2);
    jassert(result.isEmpty()); // empty string means success
    juce::ignoreUnused(result);

    // Wire: deviceManager -> sourcePlayer -> transportSource.
    deviceManager_.addAudioCallback(&sourcePlayer_);
    sourcePlayer_.setSource(&transportSource_);
    transportSource_.addChangeListener(this);

    startTimerHz(30); // 30 Hz position polling for seek bar updates
}

AudioEngine::~AudioEngine()
{
    stopTimer();
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
    transportSource_.setGain(volume_);
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
    readerSource_ = std::make_unique<juce::AudioFormatReaderSource>(reader, true);
    transportSource_.setSource(readerSource_.get(),
                               0,            // no read-ahead buffer (synchronous read)
                               nullptr,
                               reader->sampleRate);
    trackLoaded_ = true;
    loading_ = false;
    DBG("AudioEngine::loadTrack — done");
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

void AudioEngine::timerCallback()
{
    if (onStateChanged && isPlaying())
        onStateChanged(); // triggers seek bar repaint
}

} // namespace FoxPlayer
