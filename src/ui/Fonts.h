#pragma once

#include <JuceHeader.h>

namespace FoxPlayer
{

// Lazy-initialised, shared Foxwhelp typeface loaded from the embedded TTF.
inline juce::Typeface::Ptr getFoxwhelpTypeface()
{
    static juce::Typeface::Ptr tf = juce::Typeface::createSystemTypefaceFor(
        BinaryData::FoxwhelpRegular_ttf,
        BinaryData::FoxwhelpRegular_ttfSize);
    return tf;
}

} // namespace FoxPlayer
