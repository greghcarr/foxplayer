#pragma once

#include "audio/AudioEngine.h"
#include "audio/TrackInfo.h"
#include "audio/AlbumArtExtractor.h"
#include <JuceHeader.h>
#include <functional>

namespace FoxPlayer
{

class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar(AudioEngine& engine);
    ~TransportBar() override;

    void setCurrentTrack(const TrackInfo& track);
    void clearTrack();

    // Callbacks
    std::function<void()> onPrevClicked;
    std::function<void()> onNextClicked;
    std::function<void()> onQueueToggleClicked;
    std::function<void()> onChangeFolderClicked;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void timerCallback() override;
    void updateDisplay();

    juce::String formatSeconds(double secs) const;

    // Seek bar interaction
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    double seekBarNormalizedX(int x) const;

    AudioEngine& engine_;
    TrackInfo    currentTrack_;
    bool         hasTrack_     { false };
    bool         draggingSeek_ { false };
    double       seekDragPos_  { 0.0 };

    juce::TextButton prevButton_      { juce::String(juce::CharPointer_UTF8("\xe2\x8f\xae")) };
    juce::TextButton playPauseButton_;
    juce::TextButton nextButton_      { juce::String(juce::CharPointer_UTF8("\xe2\x8f\xad")) };
    juce::TextButton queueButton_ { "Queue" };

    juce::Slider volumeSlider_;
    juce::Label  volLabel_;

    juce::Label  trackLabel_;
    juce::Label  timeLabel_;

    // Seek bar and album art are painted manually.
    juce::Rectangle<int> seekBarBounds_;
    juce::Rectangle<int> albumArtBounds_;
    juce::Image          albumArt_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace FoxPlayer
