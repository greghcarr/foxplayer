#pragma once

#include "PlayQueue.h"
#include <JuceHeader.h>
#include <functional>

namespace FoxPlayer
{

// Owns the full audio pipeline:
//   AudioDeviceManager -> AudioSourcePlayer -> AudioTransportSource -> AudioFormatReaderSource
//
// AudioEngine runs on the message thread except for the audio callback (getNextAudioBlock),
// which JUCE dispatches on the audio thread internally via AudioSourcePlayer.
class AudioEngine : private juce::ChangeListener,
                    private juce::Timer
{
public:
    AudioEngine();
    ~AudioEngine() override;

    // Loads and immediately plays the given track.
    void play(const TrackInfo& track);

    void pause();
    void resume();
    void stop();

    // Seeks to a normalised position in [0, 1].
    void seekToNormalized(double position);

    // Volume: 0.0 (silent) to 1.0 (full). Stored so it survives track changes.
    void  setVolume(float gain);
    float volume() const { return volume_; }

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
    std::function<void()>                onStateChanged;

private:
    void loadTrack(const TrackInfo& track);
    void unloadCurrentReader();

    // juce::ChangeListener — fired by AudioTransportSource when state changes.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // juce::Timer — polls position for seek bar updates.
    void timerCallback() override;

    juce::AudioDeviceManager           deviceManager_;
    juce::AudioFormatManager           formatManager_;
    juce::AudioSourcePlayer            sourcePlayer_;
    juce::AudioTransportSource         transportSource_;
    std::unique_ptr<juce::AudioFormatReaderSource> readerSource_;

    TrackInfo   currentTrack_;
    bool        paused_        { false };
    bool        trackLoaded_   { false };
    bool        loading_       { false }; // true while loadTrack() is running
    float       volume_        { 1.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};

} // namespace FoxPlayer
