#pragma once

#include <JuceHeader.h>

namespace Stylus
{

// Native overlay component that displays a spinning CD using a Core Animation
// CALayer parented inside a layer-backed NSView. Rotation runs entirely on the
// GPU via a CABasicAnimation on transform.rotation.z, so it is decoupled from
// JUCE's paint cycle: the message thread does no per-frame work.
//
// Built on juce::NSViewComponent, which handles the parenting and frame
// tracking automatically and ensures the native view composites on top of
// JUCE's drawing (the only way to make a native overlay reliably visible
// inside a JUCE component hierarchy on macOS).
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

private:
    void* discView_ { nullptr };  // StylusRecordSpinnerView*

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RecordSpinnerLayer)
};

} // namespace Stylus
