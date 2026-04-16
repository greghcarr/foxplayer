#pragma once

#include <JuceHeader.h>

namespace FoxPlayer
{

// Viewport whose vertical scrollbar is hidden by default and briefly revealed
// when the user scrolls or hovers inside it, then hides again after idle.
class AutoHideViewport : public juce::Viewport,
                         private juce::Timer
{
public:
    AutoHideViewport()
    {
        setScrollBarsShown(true, false);
        getVerticalScrollBar().setAlpha(0.0f);
    }

    ~AutoHideViewport() override
    {
        juce::Desktop::getInstance().getAnimator().cancelAnimation(&getVerticalScrollBar(), false);
    }

    void mouseEnter(const juce::MouseEvent& e) override
    {
        juce::Viewport::mouseEnter(e);
        flashScrollBar();
    }

    void mouseMove(const juce::MouseEvent& e) override
    {
        juce::Viewport::mouseMove(e);
        flashScrollBar();
    }

    void mouseExit(const juce::MouseEvent& e) override
    {
        juce::Viewport::mouseExit(e);
        startTimer(hideDelayMs);
    }

    void mouseWheelMove(const juce::MouseEvent& e,
                        const juce::MouseWheelDetails& w) override
    {
        juce::Viewport::mouseWheelMove(e, w);
        flashScrollBar();
    }

    void visibleAreaChanged(const juce::Rectangle<int>& newVisibleArea) override
    {
        juce::Viewport::visibleAreaChanged(newVisibleArea);
        flashScrollBar();
    }

private:
    bool contentIsScrollable() const
    {
        auto* c = getViewedComponent();
        return c != nullptr && c->getHeight() > getMaximumVisibleHeight();
    }

    void flashScrollBar()
    {
        if (! contentIsScrollable()) return;

        auto& sb = getVerticalScrollBar();
        juce::Desktop::getInstance().getAnimator().cancelAnimation(&sb, false);
        sb.setAlpha(1.0f);
        startTimer(hideDelayMs);
    }

    void timerCallback() override
    {
        stopTimer();
        auto& sb = getVerticalScrollBar();
        juce::Desktop::getInstance().getAnimator().animateComponent(
            &sb, sb.getBounds(), 0.0f, fadeMs, false, 1.0, 1.0);
    }

    static constexpr int hideDelayMs = 800;
    static constexpr int fadeMs      = 400;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AutoHideViewport)
};

} // namespace FoxPlayer
