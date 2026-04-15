#pragma once

#include "TrackInfo.h"
#include <JuceHeader.h>

namespace FoxPlayer
{

// Reads and writes the .foxp JSON sidecar file that lives alongside each audio file.
//
// Format (example track.flac -> track.flac.foxp):
// {
//   "version": 1,
//   "bpm": 128.0,
//   "key": "Am",
//   "lufs": -14.2
// }
//
// Only fields that have been analysed are written. Missing fields are treated as
// "not yet analysed" when read back.
class FoxpFile
{
public:
    // Returns the .foxp path for a given audio file.
    static juce::File sidecarFor(const juce::File& audioFile);

    // Reads analysis data from the sidecar into track. Returns true if the file existed.
    static bool load(TrackInfo& track);

    // Writes all analysis fields from track into its sidecar. Returns true on success.
    static bool save(const TrackInfo& track);

    // Returns true if a sidecar exists for this track.
    static bool exists(const TrackInfo& track);
};

} // namespace FoxPlayer
