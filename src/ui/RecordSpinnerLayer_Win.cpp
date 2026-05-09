#include "RecordSpinnerLayer.h"

namespace Stylus
{

// 33.3 RPM = 60 / 33.333 sec/rotation = 1.8 seconds per full turn. Mirrors
// the macOS implementation so the disc spins at the same speed on both.
static constexpr double kRotationSeconds = 1.8;

RecordSpinnerLayer::RecordSpinnerLayer()
{
    setInterceptsMouseClicks(false, false);
    setOpaque(false);
}

RecordSpinnerLayer::~RecordSpinnerLayer() = default;

void RecordSpinnerLayer::setImage(const juce::Image& image)
{
    image_ = image;
    repaint();
}

void RecordSpinnerLayer::setSpinning(bool shouldSpin)
{
    if (shouldSpin == spinning_) return;
    spinning_ = shouldSpin;

    if (spinning_)
    {
        lastTickMs_ = juce::Time::currentTimeMillis();
        startTimerHz(30);
    }
    else
    {
        stopTimer();
        repaint();
    }
}

void RecordSpinnerLayer::timerCallback()
{
    const auto now = juce::Time::currentTimeMillis();
    const auto dt  = (now - lastTickMs_) / 1000.0;
    lastTickMs_ = now;

    angleRadians_ += (juce::MathConstants<double>::twoPi / kRotationSeconds) * dt;
    if (angleRadians_ > juce::MathConstants<double>::twoPi)
        angleRadians_ = std::fmod(angleRadians_, juce::MathConstants<double>::twoPi);

    repaint();
}

void RecordSpinnerLayer::paint(juce::Graphics& g)
{
    if (! image_.isValid()) return;

    const auto bounds = getLocalBounds().toFloat();
    if (bounds.isEmpty()) return;

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();

    // Fit the image to the host bounds, scaled around the centre.
    const float scaleX = bounds.getWidth()  / static_cast<float>(image_.getWidth());
    const float scaleY = bounds.getHeight() / static_cast<float>(image_.getHeight());

    auto transform = juce::AffineTransform::translation(-image_.getWidth()  * 0.5f,
                                                        -image_.getHeight() * 0.5f)
                         .scaled(scaleX, scaleY)
                         .rotated(static_cast<float>(angleRadians_))
                         .translated(cx, cy);

    g.drawImageTransformed(image_, transform, false);
}

} // namespace Stylus
