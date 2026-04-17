#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <deque>

namespace FoxPlayer
{

// Background worker that queries the public iTunes Search API for each enqueued
// track, fills in any missing metadata fields (album, year, genre, track number)
// without overwriting existing values, and downloads cover art into a sidecar
// JPEG that AlbumArtExtractor will pick up. Results are reported back to the
// message thread via callbacks.
class AppleMusicLookup : private juce::Thread
{
public:
    AppleMusicLookup();
    ~AppleMusicLookup() override;

    // overwrite=false fills only blank fields; overwrite=true replaces
    // existing artist/album/etc. and re-downloads cover art even if a
    // sidecar already exists.
    void enqueue(const TrackInfo& track, bool overwrite);
    void enqueueAll(const std::vector<TrackInfo>& tracks, bool overwrite);

    // All callbacks fire on the message thread.
    std::function<void(TrackInfo)> onLookupQueued;
    std::function<void(TrackInfo)> onLookupStarted;
    // The completed track has any new metadata fields filled in. The status
    // string is human-readable: e.g. "Found: <album>", "No match", "Network error".
    std::function<void(TrackInfo, juce::String)> onLookupCompleted;

    // Sidecar JPEG path that AlbumArtExtractor checks for downloaded art.
    // Mirrors the `.foxp` naming convention so the file stays hidden and
    // sits next to the audio.
    static juce::File artworkSidecarFor(const juce::File& audioFile);

private:
    struct Job
    {
        TrackInfo track;
        bool      overwrite { false };
    };

    void run() override;
    void cancelAll();
    void processOne(Job job);

    std::deque<Job>        queue_;
    juce::CriticalSection  queueLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppleMusicLookup)
};

} // namespace FoxPlayer
