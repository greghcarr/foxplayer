#include "PlayQueue.h"

namespace FoxPlayer
{

void PlayQueue::setTracks(std::vector<TrackInfo> tracks, int startIndex)
{
    tracks_ = std::move(tracks);
    index_  = tracks_.empty() ? -1 : juce::jlimit(0, static_cast<int>(tracks_.size()) - 1, startIndex);

    if (onQueueChanged)   onQueueChanged();
    if (onIndexChanged)   onIndexChanged(index_);
}

void PlayQueue::clear()
{
    tracks_.clear();
    index_ = -1;
    if (onQueueChanged) onQueueChanged();
    if (onIndexChanged) onIndexChanged(index_);
}

bool PlayQueue::hasCurrent() const
{
    return index_ >= 0 && index_ < static_cast<int>(tracks_.size());
}

bool PlayQueue::hasNext() const
{
    return index_ >= 0 && index_ + 1 < static_cast<int>(tracks_.size());
}

bool PlayQueue::hasPrev() const
{
    return index_ > 0;
}

const TrackInfo& PlayQueue::current() const
{
    jassert(hasCurrent());
    return tracks_[static_cast<size_t>(index_)];
}

bool PlayQueue::advanceToNext()
{
    if (!hasNext()) return false;
    ++index_;
    if (onIndexChanged) onIndexChanged(index_);
    return true;
}

bool PlayQueue::retreatToPrev()
{
    if (!hasPrev()) return false;
    --index_;
    if (onIndexChanged) onIndexChanged(index_);
    return true;
}

bool PlayQueue::jumpTo(int newIndex)
{
    if (newIndex < 0 || newIndex >= static_cast<int>(tracks_.size())) return false;
    index_ = newIndex;
    if (onIndexChanged) onIndexChanged(index_);
    return true;
}

} // namespace FoxPlayer
