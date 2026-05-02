#pragma once

#include "JuceCore.h"

namespace Stylus
{

// Estimates the musical key of an audio file using a chromagram correlated
// against Krumhansl-Schmuckler major/minor key profiles.
//
// Returns a string like "C", "F#m", "Bbm", or "" on failure.
class KeyDetector
{
public:
    struct Settings
    {
        double analysisSeconds { 60.0 };
        int    fftOrder        { 12 };   // FFT size = 2^fftOrder (4096)
    };

    static juce::String detect(const juce::File& audioFile,
                                juce::AudioFormatManager& formatManager,
                                const Settings& settings);
};

} // namespace Stylus
