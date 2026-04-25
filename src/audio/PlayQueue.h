#pragma once

#include "TrackInfo.h"
#include <JuceHeader.h>
#include <vector>
#include <functional>

namespace FoxPlayer
{

class PlayQueue
{
public:
    struct QueueSource
    {
        juce::String name;
        int          sidebarId { 1 };
    };

    PlayQueue() = default;

    void setTracks(std::vector<TrackInfo> tracks, int startIndex, QueueSource source);
    void appendTracks(std::vector<TrackInfo> tracks, QueueSource source);
    void insertAfterCurrent(std::vector<TrackInfo> tracks, QueueSource source);
    void clear();

    bool hasCurrent() const;
    bool hasNext()    const;
    bool hasPrev()    const;

    const TrackInfo& current() const;
    QueueSource      currentSource() const;
    int              currentIndex() const { return index_; }
    int              size()         const { return static_cast<int>(tracks_.size()); }

    bool advanceToNext();
    bool retreatToPrev();
    bool jumpTo(int index);

    // Removes the track at the given index. No-op if index is the currently
    // playing track or out of range. Also strips the track from the saved
    // original-order list when shuffle is on, so the un-shuffle restore
    // doesn't bring it back.
    void removeAt(int index);

    const std::vector<TrackInfo>& tracks() const { return tracks_; }

    // Returns the original (un-shuffled) track order if shuffle is on, or the
    // live track list otherwise. Used when persisting the queue so the user's
    // un-shuffle mapping is preserved across sessions.
    const std::vector<TrackInfo>& originalTracks() const
    {
        return isShuffled_ ? originalTracks_ : tracks_;
    }

    // Shuffle all tracks after the current index. Saves original order for restoration.
    void shuffleRemaining();
    // Move the current track to index 0 and shuffle all other tracks after it.
    // Saves original order for restoration. Used when activating a track with shuffle on.
    void shuffleAll();
    // Restore the original pre-shuffle order, keeping the current track at its original position.
    void unshuffleRemaining();
    bool isShuffled() const { return isShuffled_; }

    std::function<void()>    onQueueChanged;
    std::function<void(int)> onIndexChanged;
    // Fired when shuffle state is reset externally (e.g., new tracks loaded).
    std::function<void(bool)> onShuffleStateChanged;

private:
    std::vector<TrackInfo>   tracks_;
    std::vector<QueueSource> sources_;  // parallel to tracks_
    int                      index_      { -1 };
    bool                     isShuffled_ { false };
    std::vector<TrackInfo>   originalTracks_;
    std::vector<QueueSource> originalSources_;
};

} // namespace FoxPlayer
