#pragma once

#include "Constants.h"
#include <JuceHeader.h>

namespace FoxPlayer
{

// Small component that draws a rotating arc next to a caption. Used to signal
// that the library is currently being scanned.
class LoadingIndicator : public juce::Component,
                         private juce::Timer
{
public:
    explicit LoadingIndicator(juce::String caption = "Loading Music Library...")
        : caption_(std::move(caption))
    {
    }

    ~LoadingIndicator() override { stopTimer(); }

    void visibilityChanged() override
    {
        if (isVisible()) startTimerHz(30);
        else             stopTimer();
    }

    void paint(juce::Graphics& g) override
    {
        const juce::Font font(juce::FontOptions().withHeight(18.0f));

        juce::GlyphArrangement ga;
        ga.addLineOfText(font, caption_, 0.0f, 0.0f);
        const int textW = static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth()));

        constexpr int spinnerD = 20;
        constexpr int gap      = 10;

        const int totalW = spinnerD + gap + textW;
        const int startX = (getWidth()  - totalW)    / 2;
        const int cy     = getHeight() / 2;

        const float r  = spinnerD * 0.5f - 1.5f;
        const float cx = static_cast<float>(startX) + spinnerD * 0.5f;
        const float cyF = static_cast<float>(cy);

        // Full faint ring
        g.setColour(Constants::Color::textDim);
        g.drawEllipse(cx - r, cyF - r, r * 2.0f, r * 2.0f, 2.0f);

        // Rotating accent arc: ~280 degrees sweep
        juce::Path arc;
        const float sweep = juce::MathConstants<float>::pi * 1.55f;
        arc.addCentredArc(cx, cyF, r, r,
                          0.0f,
                          rotation_,
                          rotation_ + sweep,
                          true);
        g.setColour(Constants::Color::accent);
        g.strokePath(arc, juce::PathStrokeType(2.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

        // Caption
        g.setFont(font);
        g.setColour(Constants::Color::textSecondary);
        g.drawText(caption_,
                   startX + spinnerD + gap, 0,
                   textW + 4, getHeight(),
                   juce::Justification::centredLeft, false);
    }

private:
    void timerCallback() override
    {
        rotation_ += juce::MathConstants<float>::pi * 0.08f;
        if (rotation_ > juce::MathConstants<float>::twoPi)
            rotation_ -= juce::MathConstants<float>::twoPi;
        repaint();
    }

    juce::String caption_;
    float        rotation_ { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LoadingIndicator)
};

} // namespace FoxPlayer
