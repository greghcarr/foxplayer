#pragma once

#include <JuceHeader.h>

namespace FoxPlayer
{

// Paints the app icon (loaded from the embedded PNG) centred on a transparent
// window - no background rectangle, just the icon floating on the desktop.
class SplashComponent : public juce::Component
{
public:
    SplashComponent()
    {
        setOpaque(false);
        setInterceptsMouseClicks(false, false);
        icon_ = juce::ImageCache::getFromMemory(BinaryData::appicon_png,
                                                BinaryData::appicon_pngSize);
    }

    void paint(juce::Graphics& g) override
    {
        if (icon_.isNull()) return;

        g.drawImageWithin(icon_,
                          0, 0, getWidth(), getHeight(),
                          juce::RectanglePlacement::centred
                          | juce::RectanglePlacement::onlyReduceInSize,
                          false);
    }

private:
    juce::Image icon_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashComponent)
};

// Transparent borderless window that shows only the app icon on top of
// whatever's behind it on the desktop.
class SplashWindow : public juce::DocumentWindow
{
public:
    SplashWindow()
        : juce::DocumentWindow({}, juce::Colours::transparentBlack, 0)
    {
        setUsingNativeTitleBar(false);
        setTitleBarHeight(0);
        setDropShadowEnabled(false);
        setOpaque(false);
        setBackgroundColour(juce::Colours::transparentBlack);
        setContentOwned(new SplashComponent(), true);
        centreWithSize(280, 280);
        setVisible(true);
        toFront(true);
    }

    void closeButtonPressed() override {}

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SplashWindow)
};

} // namespace FoxPlayer
