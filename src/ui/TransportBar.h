#pragma once

#include "audio/AudioEngine.h"
#include "audio/TrackInfo.h"
#include "audio/AlbumArtExtractor.h"
#include <JuceHeader.h>
#include <functional>

namespace FoxPlayer
{

// Circular transport control button with a vector-drawn icon.
class TransportButton : public juce::Component
{
public:
    enum class Icon { Prev, Play, Pause, Next, Shuffle, Repeat, Pin };

    Icon icon        { Icon::Play };
    int  toggleState { 0 };   // 0=off, 1=on, 2=on-alt (repeat-one)
    // When true, the button is visibly "off" (dim grey) in toggleState 0 and
    // "on" (chrome + red icon) when toggleState > 0. When false, the button
    // always uses the chrome/black look (e.g. play/prev/next have no off state).
    bool toggleStyle { false };
    std::function<void()> onClick;

    TransportButton()
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
    }

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

private:
    bool hovered_ { false };
    bool pressed_  { false };

    // Cached parsed SVG - rebuilt only when icon, toggleState, or color changes.
    struct SvgCache
    {
        Icon                            icon        { Icon::Play };
        int                             toggleState { -1 };
        juce::Colour                    color;
        std::unique_ptr<juce::Drawable> drawable;
    };
    mutable SvgCache svgCache_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportButton)
};

// ----------------------------------------------------------------------------

class TransportBar : public juce::Component,
                     private juce::Timer
{
public:
    explicit TransportBar(AudioEngine& engine);
    ~TransportBar() override;

    void setCurrentTrack(const TrackInfo& track);
    void clearTrack();

    // Sets the "Playing from:" source label displayed below the artist name.
    void setPlayingFrom(const juce::String& sourceName, int sourceSidebarId);

    // Resets the shuffle button toggle state (called externally when queue is replaced).
    void setShuffleOn(bool on);

    // Sets the repeat button state. 0=off, 1=repeat-all, 2=repeat-one.
    void setRepeatMode(int mode);

    // Callbacks
    std::function<void()>    onPrevClicked;
    std::function<void()>    onNextClicked;
    std::function<void()>    onChangeFolderClicked;
    // Fired when the user clicks the "Playing from: X" source link.
    std::function<void(int)> onPlayingFromClicked;
    // Fired with the new toggle state when the user clicks shuffle/repeat.
    // Shuffle: true=on, false=off.
    // Repeat: 0=off, 1=repeat-all, 2=repeat-one.
    std::function<void(bool)> onShuffleToggled;
    std::function<void(int)>  onRepeatToggled;
    // Fired with the new 0..1 slider value after the user adjusts the volume.
    // Does NOT fire for internal slider updates (mute/unmute).
    std::function<void(double)> onVolumeChanged;

    // Fired when mute state changes. Args: muted, premuteVolume.
    std::function<void(bool, double)> onMuteChanged;

    // Apply a persisted volume without firing onVolumeChanged. Call once at
    // startup after wiring up the callback.
    void setInitialVolume(double value);

    // Restore muted state without firing callbacks. Call once at startup.
    void setInitialMute(bool muted, double premuteVolume);

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;
    juce::MouseCursor getMouseCursor() override;

private:
    void timerCallback() override;
    void updateDisplay();
    // Apply reduced alpha to the volume slider when it's silent (muted or
    // manually set to zero), full alpha otherwise.
    void refreshVolumeAlpha();

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

    TransportButton shuffleButton_;
    TransportButton prevButton_;
    TransportButton playPauseButton_;
    TransportButton nextButton_;
    TransportButton repeatButton_;

    // Custom slider LookAndFeel that overlays a dark-grey "pressed" dot on the
    // thumb while the user is dragging.
    struct DraggingDotSliderLnF : public juce::LookAndFeel_V4
    {
        void drawLinearSlider(juce::Graphics& g,
                              int x, int y, int width, int height,
                              float sliderPos, float minPos, float maxPos,
                              juce::Slider::SliderStyle style,
                              juce::Slider& slider) override;
    };

    DraggingDotSliderLnF volumeSliderLnF_;
    juce::Slider         volumeSlider_;

    juce::Label  elapsedLabel_;
    juce::Label  totalLabel_;

    // Seek bar, album art, and track info are painted manually.
    juce::Rectangle<int> seekBarBounds_;
    juce::Rectangle<int> albumArtBounds_;
    juce::Rectangle<int> infoAreaBounds_;
    juce::Rectangle<int> sourceLinkBounds_;     // clickable "X" portion of "Playing from: X"
    juce::Rectangle<int> compactInfoBounds_;    // mini-mode "Artist - Title" line above the buttons
    juce::Rectangle<int> speakerBounds_;     // clickable speaker icon area
    juce::Image          albumArt_;
    float                recordRotation_ { 0.0f };

    // Speaker/mute state
    bool   muted_         { false };
    double premuteVolume_ { 1.0 };    // slider value to restore when unmuting via speaker click

    // Pre-loaded speaker SVG drawables (loaded once, tinted at construction).
    std::unique_ptr<juce::Drawable> speakerNoneDrawable_;
    std::unique_ptr<juce::Drawable> speakerLowDrawable_;
    std::unique_ptr<juce::Drawable> speakerHighDrawable_;
    std::unique_ptr<juce::Drawable> speakerMutedDrawable_;

    juce::String         playingFromName_;
    int                  playingFromSidebarId_ { 1 };

    // Millisecond timestamp when the compact-line marquee started its current
    // cycle. Reset on setCurrentTrack so each new track scrolls from the start.
    juce::uint32 compactScrollStartMs_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportBar)
};

} // namespace FoxPlayer
