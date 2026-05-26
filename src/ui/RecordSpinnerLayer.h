#pragma once

#include <JuceHeader.h>

namespace Stylus
{

// Spinning-disc overlay above the album art.
//
// macOS: backed by a Core Animation CALayer parented inside a layer-backed
// NSView (juce::NSViewComponent host). Rotation runs entirely on the GPU via
// a CABasicAnimation on transform.rotation.z, so the message thread does no
// per-frame work.
//
// Other platforms (Windows): JUCE-component fallback. A juce::Timer at 30 Hz
// advances the rotation angle and triggers a repaint that draws the disc
// image with an affine rotation. Slightly more main-thread work, but small
// in absolute terms (the disc is one image, drawn once per frame).
//
// Public surface is identical on both platforms so call sites in TransportBar
// don't need to know which backend is active.

#if JUCE_MAC

class RecordSpinnerLayer : public juce::NSViewComponent
{
public:
    RecordSpinnerLayer();
    ~RecordSpinnerLayer() override;

    // Sets the disc image. Pass an empty/invalid image to clear. The image is
    // converted to a CGImage and assigned to the layer; old contents are released.
    void setImage(const juce::Image& image);

    // Starts or stops the continuous rotation animation. While stopped, the
    // layer freezes at its current rotation (the GPU keeps the angle).
    void setSpinning(bool shouldSpin);

    // Drops the disc's opacity to match the lock-overlay dim applied to the
    // rest of the window when a Stylus dialog (Edit Info, Preferences,
    // Quit confirm, etc.) is open. Driven by MainComponent rather than by
    // window-key state - dimming on app-deactivation looked wrong because
    // the rest of the window doesn't dim on resign-key.
    void setDimmed(bool dim);

private:
    void* discView_ { nullptr };  // StylusRecordSpinnerView*

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordSpinnerLayer)
};

#else

class RecordSpinnerLayer : public juce::Component,
                           private juce::Timer
{
public:
    RecordSpinnerLayer();
    ~RecordSpinnerLayer() override;

    void setImage(const juce::Image& image);
    void setSpinning(bool shouldSpin);
    // See macOS class comment - dim is driven by dialog state, not focus.
    void setDimmed(bool dim);

    void paint(juce::Graphics& g) override;

private:
    void timerCallback() override;

    juce::Image image_;
    double      angleRadians_ { 0.0 };
    bool        spinning_     { false };
    bool        dimmed_       { false };
    juce::int64 lastTickMs_   { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordSpinnerLayer)
};

#endif

} // namespace Stylus
