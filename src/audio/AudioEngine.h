#pragma once

#include "PlayQueue.h"
#include <JuceHeader.h>
#include <functional>

namespace Stylus
{

// Owns the full audio pipeline:
//   AudioDeviceManager -> AudioSourcePlayer -> AudioTransportSource -> AudioFormatReaderSource
//
// AudioEngine runs on the message thread except for the audio callback (getNextAudioBlock),
// which JUCE dispatches on the audio thread internally via AudioSourcePlayer.
class AudioEngine : private juce::ChangeListener
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Loads and immediately plays the given track.
    void play(const TrackInfo& track);

    // Loads the track and seeks to the given elapsed seconds, but stays paused
    // (does not start the transport). Used to restore a persisted session.
    void prepareTrackPaused(const TrackInfo& track, double elapsedSeconds);

    void pause();
    void resume();
    void stop();

    // Seeks to a normalised position in [0, 1].
    void seekToNormalized(double position);

    // Volume: 0.0 (silent) to 1.0 (full). Stored so it survives track changes.
    void  setVolume(float gain);
    float volume() const { return volume_; }

    // When enabled, each track's playback gain is offset based on its
    // measured integrated loudness (LUFS) so different songs play at a
    // similar level. Tracks with no LUFS measurement (lufs == 0) play
    // unchanged. The user volume slider still scales the result; this
    // sits underneath it as a per-track multiplier.
    void  setVolumeNormalizationEnabled(bool enabled);
    bool  isVolumeNormalizationEnabled() const { return normalizeVolume_; }

    // Read-only access to the loaded track. MainComponent uses this to
    // recognise late LUFS analysis results that target the playing file.
    const TrackInfo& currentTrack() const { return currentTrack_; }

    // Push an updated LUFS value for the currently-loaded track and
    // re-apply the combined gain. Called when lazy LUFS analysis finishes
    // for the track that's still playing, so the user hears the new level
    // ramp in (via JUCE's setGain ramp) without a track reload.
    void updateCurrentTrackLufs(float lufs);

    // Mark the currently-loaded track's LUFS analysis as definitively
    // unavailable - measurement ran but couldn't produce a value (file
    // unreadable, all-silence, decode failure). Applied combined gain
    // falls back to unity for this track instead of staying in the -3 dB
    // pre-roll forever.
    void markLufsAnalysisFailed();

    bool isPlaying()  const;
    bool isPaused()   const;

    // Returns normalised playback position in [0, 1], or 0 if no track loaded.
    double normalizedPosition() const;

    // Elapsed / total seconds of the current track.
    double elapsedSeconds()  const;
    double durationSeconds() const;

    juce::AudioDeviceManager& deviceManager() { return deviceManager_; }

    // Callbacks (called on the message thread).
    std::function<void(const TrackInfo&)> onTrackStarted;
    std::function<void()>                onPlaybackStopped;
    std::function<void()>                onTrackFinished;   // natural end-of-track
    std::function<void()>                onTrackFailed;     // load failed (missing/corrupt file)
    std::function<void()>                onStateChanged;

private:
    void loadTrack(const TrackInfo& track);
    void unloadCurrentReader();

    // Pushes (user volume * per-track LUFS-based offset) to the transport.
    // Called from setVolume / setVolumeNormalizationEnabled / loadTrack so
    // the audible level always reflects the latest combination. When
    // `smooth` is true the change ramps over kRampDurationMs in dB-space
    // (used for normalisation-driven transitions); when false it's
    // applied immediately (volume-slider drags, track changes).
    void applyCombinedGain(bool smooth = false);

    // Computes the target gain based on user volume + normalisation state.
    // Pure function over the engine's current state; used by both the
    // immediate path and the ramp timer.
    float computeTargetGain() const;

    // Drives the per-tick gain interpolation when a smooth ramp is active.
    void stepRamp();

    // Ticks the gain ramp on the message thread. Inner class because
    // juce::Timer is abstract and we already inherit ChangeListener.
    class RampTimer : public juce::Timer
    {
    public:
        explicit RampTimer(AudioEngine& e) : engine_(e) {}
        void timerCallback() override { engine_.stepRamp(); }
    private:
        AudioEngine& engine_;
    };

    // juce::ChangeListener, fired by AudioTransportSource when state changes.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    juce::AudioDeviceManager           deviceManager_;
    juce::AudioFormatManager           formatManager_;
    juce::AudioSourcePlayer            sourcePlayer_;
    // Declared before transportSource_ so it outlives it: the BufferingAudioSource
    // that AudioTransportSource builds internally references this thread for
    // background reads, and must be torn down (via setSource(nullptr)) before
    // the thread is destroyed.
    juce::TimeSliceThread              readAheadThread_ { "Stylus Read-Ahead" };
    juce::AudioTransportSource         transportSource_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    TrackInfo   currentTrack_;
    bool        paused_        { false };
    bool        trackLoaded_   { false };
    bool        loading_       { false }; // true while loadTrack() is running
    float       volume_        { 1.0f };
    bool        normalizeVolume_ { false };
    // True once we have a definitive answer about the loaded track's
    // loudness - either a measured LUFS value or a confirmed analysis
    // failure. While false, applyCombinedGain pre-rolls at -3 dB; once
    // true, gain settles to either the measured offset or unity.
    bool        lufsKnown_     { false };

    // Ramp state. currentGain_ is the gain that's actually live on the
    // transport. When a smooth ramp is requested, the timer interpolates
    // from rampStartGain_ to rampTargetGain_ in log space (perceptually
    // even fade) over kRampDurationMs.
    float       currentGain_      { 1.0f };
    float       rampStartGain_    { 1.0f };
    float       rampTargetGain_   { 1.0f };
    juce::int64 rampStartTimeMs_  { 0 };
    static constexpr int kRampDurationMs = 2000;
    RampTimer   rampTimer_ { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace Stylus
