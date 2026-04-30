#pragma once

#include "TrackInfo.h"
#include <JuceHeader.h>

namespace Stylus
{

// Reads and writes the .styl JSON sidecar file that lives alongside each
// audio file. Hidden (dot-prefixed) so Finder doesn't show it.
//
// Format (example track.flac -> .track.flac.styl):
// {
//   "version": 1,
//   "bpm": 128.0,
//   "key": "Am",
//   "lufs": -14.2
// }
//
// Only fields that have been analysed are written. Missing fields are treated
// as "not yet analysed" when read back.
class StylFile
{
public:
    // Returns the .styl sidecar path for a given audio file.
    static juce::File sidecarFor(const juce::File& audioFile);

    // Reads sidecar data into track. Returns true if the file existed.
    static bool load(TrackInfo& track);

    // Writes all fields from track into its .styl sidecar. Returns true on success.
    static bool save(const TrackInfo& track);

    // Returns true if a sidecar exists for this track.
    static bool exists(const TrackInfo& track);
};

} // namespace Stylus
