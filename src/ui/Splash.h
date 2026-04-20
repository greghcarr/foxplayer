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

        // The squircle occupies roughly the inner 80 % of the PNG canvas.
        // Draw the black radiance only in the ring OUTSIDE the squircle so the
        // shadow does not darken the icon through its semi-transparent edges.
        const auto  bounds   = getLocalBounds().toFloat();
        const float pad      = bounds.getWidth() * 0.10f;
        const float cr       = (bounds.getWidth() - pad * 2.0f) * 0.22f;
        const auto  squircle = bounds.reduced(pad);

        {
            juce::Path clipRing;
            clipRing.addRectangle(bounds);
            clipRing.addRoundedRectangle(squircle, cr);
            clipRing.setUsingNonZeroWinding(false); // even-odd: squircle becomes a hole

            g.saveState();
            g.reduceClipRegion(clipRing);

            juce::Path glowShape;
            glowShape.addRoundedRectangle(squircle, cr);
            juce::DropShadow(juce::Colours::black.withAlpha(0.80f), 22, {}).drawForPath(g, glowShape);

            g.restoreState();
        }

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
