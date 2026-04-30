#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <vector>

namespace Stylus
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
                     const std::vector<juce::File>& musicFolders,
                     const std::vector<juce::File>& podcastFolders);

    // Loads the cache. Returns true and fills output parameters if the file
    // exists and parses cleanly. Returns false on missing file or any error.
    static bool tryLoad(std::vector<TrackInfo>& outTracks,
                        std::vector<juce::File>& outMusicFolders,
                        std::vector<juce::File>& outPodcastFolders);
};

} // namespace Stylus
