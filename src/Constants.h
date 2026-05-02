#pragma once

#include "JuceCore.h"

// Tunable values used by the StylusCore library: scanner pacing and the set
// of audio-file extensions the library scanner accepts. These are independent
// of GUI/look-and-feel; UI tunables live in UIConstants.h.
namespace Stylus::Constants
{
    // Scanner
    static constexpr int scannerBatchSize = 50;
    // Sleep this many ms after each emitted batch so the scanner thread
    // breathes instead of saturating a core. Trades total scan time for
    // lower CPU; 60 ms per 50 files = roughly a third of the CPU during
    // a scan, at the cost of scans taking ~3x as long.
    static constexpr int scannerInterBatchPauseMs = 60;

    // Supported audio file extensions (lowercase)
    static const juce::StringArray supportedExtensions {
        "mp3", "flac", "wav", "aiff", "aif", "m4a", "aac", "alac", "ogg", "opus"
    };
}
