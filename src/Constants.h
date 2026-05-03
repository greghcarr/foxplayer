#pragma once

#include "JuceCore.h"

// Tunable values used by the StylusCore library: scanner pacing and the set
// of audio-file extensions the library scanner accepts. These are independent
// of GUI/look-and-feel; UI tunables live in UIConstants.h.
namespace Stylus::Constants
{
    // Scanner
    // Smaller batches = smoother per-track progress reporting, at the cost of
    // slightly more main-thread dispatch overhead per scan. The previous 50
    // value made the iOS progress bar appear to "stick" between batches.
    static constexpr int scannerBatchSize = 10;
    // Sleep this many ms after each emitted batch so the scanner thread
    // breathes instead of saturating a core. Trades total scan time for
    // lower CPU.
    static constexpr int scannerInterBatchPauseMs = 60;

    // Supported audio file extensions (lowercase)
    static const juce::StringArray supportedExtensions {
        "mp3", "flac", "wav", "aiff", "aif", "m4a", "aac", "alac", "ogg", "opus"
    };
}
