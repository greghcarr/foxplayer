#pragma once

#include "TrackInfo.h"
#include <vector>
#include <functional>

namespace FoxPlayer
{

class PlayQueue
{
public:
    PlayQueue() = default;

    void setTracks(std::vector<TrackInfo> tracks, int startIndex = 0);
    void clear();

    bool hasCurrent() const;
    bool hasNext()    const;
    bool hasPrev()    const;

    const TrackInfo& current() const;
    int              currentIndex() const { return index_; }
    int              size()         const { return static_cast<int>(tracks_.size()); }

    bool advanceToNext();
    bool retreatToPrev();
    bool jumpTo(int index);

    const std::vector<TrackInfo>& tracks() const { return tracks_; }

    std::function<void()> onQueueChanged;
    std::function<void(int)> onIndexChanged;

private:
    std::vector<TrackInfo> tracks_;
    int index_ { -1 };
};

} // namespace FoxPlayer
