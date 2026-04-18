#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <deque>
#include <atomic>
#include <map>

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

    // Returns true after maxConsecutiveFailures consecutive network errors.
    // While suspended the background thread will not process further jobs.
    bool isSuspended() const { return suspended_.load(); }

    // All callbacks fire on the message thread.
    std::function<void(TrackInfo)> onLookupQueued;
    std::function<void(TrackInfo)> onLookupStarted;
    // The completed track has any new metadata fields filled in. The status
    // string is human-readable: "Found: <album>", "No match", "Network error".
    // isBatch is true when the job came from enqueueAll() or from an automatic
    // retry; false when it came from a single enqueue() call.
    std::function<void(TrackInfo, juce::String /*status*/, bool /*isBatch*/)> onLookupCompleted;
    // Fired when consecutive network failures hit the threshold. The worker
    // stops processing after this fires; no further callbacks will arrive.
    std::function<void()> onLookupSuspended;

    // Sidecar JPEG path that AlbumArtExtractor checks for downloaded art.
    // Mirrors the `.foxp` naming convention so the file stays hidden and
    // sits next to the audio.
    static juce::File artworkSidecarFor(const juce::File& audioFile);

    static constexpr int maxConsecutiveFailures = 5;

private:
    struct Job
    {
        TrackInfo track;
        bool      overwrite { false };
        bool      isBatch   { false };
    };

    void run() override;
    void cancelAll();
    void processOne(Job job);

    std::deque<Job>        queue_;
    juce::CriticalSection  queueLock_;

    // Tracks which album name has been resolved most often for each directory.
    // Only read/written on the background thread — no lock required.
    // Key: canonical directory path. Value: album name -> resolution count.
    std::map<juce::String, std::map<juce::String, int>> resolvedAlbums_;

    // Incremented on each sequential network failure; reset to 0 on any
    // success. Only read/written from the background thread.
    int consecutiveNetworkFailures_ { 0 };

    // Set to true when the failure threshold is reached. Checked at the top
    // of run() so the thread exits cleanly; also checked by callers via
    // isSuspended() before re-enqueueing retry jobs.
    std::atomic<bool> suspended_ { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppleMusicLookup)
};

} // namespace FoxPlayer
