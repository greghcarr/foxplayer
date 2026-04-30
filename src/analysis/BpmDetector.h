#pragma once

#include <JuceHeader.h>

namespace Stylus
{

// Estimates the BPM of an audio file using envelope autocorrelation.
//
// Algorithm:
//   1. Read up to analysisSeconds of audio, mix to mono.
//   2. Rectify and low-pass to obtain the onset-energy envelope.
//   3. Downsample the envelope to a fixed rate (envelopeRate Hz).
//   4. Compute autocorrelation over the BPM search range (minBpm..maxBpm).
//   5. Pick the lag with the highest correlation; convert to BPM.
//
// Returns 0.0 on failure (file unreadable, too short, etc.).
class BpmDetector
{
public:
    struct Settings
    {
        double minBpm           { 60.0 };
        double maxBpm           { 200.0 };
        double analysisSeconds  { 60.0 };  // how much of the file to read
        int    envelopeRate     { 200 };   // Hz - envelope downsample rate
        int    lpfOrder         { 8 };     // envelope low-pass window (samples at envelopeRate)
    };

    static double detect(const juce::File& audioFile,
                         juce::AudioFormatManager& formatManager,
                         const Settings& settings);
};

} // namespace Stylus
