#pragma once

#include "JuceCore.h"
#include <functional>

namespace Stylus
{

// K-weighted integrated loudness measurement, modeled on ITU-R BS.1770.
// Returns a negative number in LUFS (e.g. -14.0 for streaming-target
// loudness), or 0.0f if the file couldn't be opened or had no samples.
//
// Implementation note: uses the BS.1770 K-weighting filter (high-shelf
// pre-filter + RLB high-pass) with the canonical 48 kHz coefficients
// applied at all sample rates. This introduces under ~1 LU of error at
// rates like 44.1 / 96 kHz vs strict BS.1770, which is well below what
// matters for matching playback volume between tracks. Gating is omitted
// (full-file mean square integration).
class LufsDetector
{
public:
    static float detect(const juce::File& file,
                        juce::AudioFormatManager& formatManager,
                        std::function<bool()> shouldExit);
};

} // namespace Stylus
