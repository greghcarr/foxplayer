#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <vector>

namespace FoxPlayer
{

// Disk cache for the scanned library so subsequent launches can populate
// fullLibrary_ instantly without waiting for the background scan to finish.
// Returns false on read errors / mismatched folder set; the caller should
// fall back to a fresh scan in that case.
class LibraryCache
{
public:
    // Path of the cache file under ~/Library/Application Support/.
    static juce::File cacheFile();

    static bool save(const std::vector<TrackInfo>& tracks,
                     const std::vector<juce::File>& folders);

    // Loads the cache. Returns true and fills outTracks/outFolders if the
    // file exists and parses cleanly. Returns false (and leaves outputs
    // untouched) on missing file or any kind of read error.
    static bool tryLoad(std::vector<TrackInfo>& outTracks,
                        std::vector<juce::File>& outFolders);
};

} // namespace FoxPlayer
