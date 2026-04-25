#include "PlayQueue.h"

namespace FoxPlayer
{

void PlayQueue::setTracks(std::vector<TrackInfo> tracks, int startIndex, PlayQueue::QueueSource source)
{
    // Reset shuffle state whenever a new queue is loaded.
    if (isShuffled_)
    {
        isShuffled_ = false;
        originalTracks_.clear();
        originalSources_.clear();
        if (onShuffleStateChanged) onShuffleStateChanged(false);
    }

    tracks_ = std::move(tracks);
    sources_.assign(tracks_.size(), source);
    index_  = tracks_.empty() ? -1 : juce::jlimit(0, static_cast<int>(tracks_.size()) - 1, startIndex);

    if (onQueueChanged)   onQueueChanged();
    if (onIndexChanged)   onIndexChanged(index_);
}

void PlayQueue::appendTracks(std::vector<TrackInfo> tracks, PlayQueue::QueueSource source)
{
    for (auto& t : tracks)
    {
        if (isShuffled_)
        {
            originalTracks_.push_back(t);
            originalSources_.push_back(source);
        }
        tracks_.push_back(std::move(t));
        sources_.push_back(source);
    }
    if (onQueueChanged) onQueueChanged();
}

void PlayQueue::insertAfterCurrent(std::vector<TrackInfo> tracks, PlayQueue::QueueSource source)
{
    if (tracks.empty()) return;
    const int pos = hasCurrent() ? index_ + 1 : static_cast<int>(tracks_.size());
    tracks_.insert (tracks_.begin()  + pos, tracks.begin(),  tracks.end());
    sources_.insert(sources_.begin() + pos, tracks.size(), source);
    if (onQueueChanged) onQueueChanged();
}

void PlayQueue::clear()
{
    tracks_.clear();
    sources_.clear();
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

PlayQueue::QueueSource PlayQueue::currentSource() const
{
    if (index_ < 0 || index_ >= static_cast<int>(sources_.size()))
        return {};
    return sources_[static_cast<size_t>(index_)];
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

void PlayQueue::removeAt(int idx)
{
    if (idx < 0 || idx >= static_cast<int>(tracks_.size())) return;
    if (idx == index_) return;  // caller guards this; never silently kill the playing track

    const juce::File removedFile = tracks_[static_cast<size_t>(idx)].file;

    tracks_.erase(tracks_.begin()  + idx);
    sources_.erase(sources_.begin() + idx);

    if (isShuffled_)
    {
        for (int i = 0; i < static_cast<int>(originalTracks_.size()); ++i)
        {
            if (originalTracks_[static_cast<size_t>(i)].file == removedFile)
            {
                originalTracks_.erase(originalTracks_.begin()  + i);
                originalSources_.erase(originalSources_.begin() + i);
                break;
            }
        }
    }

    const int oldIndex = index_;
    if (idx < index_) --index_;

    if (onQueueChanged) onQueueChanged();
    if (oldIndex != index_ && onIndexChanged) onIndexChanged(index_);
}

void PlayQueue::shuffleRemaining()
{
    if (isShuffled_ || !hasCurrent()) return;

    originalTracks_  = tracks_;
    originalSources_ = sources_;
    isShuffled_ = true;

    const int afterStart = index_ + 1;
    const int count      = static_cast<int>(tracks_.size()) - afterStart;

    if (count > 1)
    {
        juce::Random rng;
        for (int i = count - 1; i > 0; --i)
        {
            const int j  = rng.nextInt(i + 1);
            const int ai = afterStart + i;
            const int aj = afterStart + j;
            std::swap(tracks_[static_cast<size_t>(ai)],  tracks_[static_cast<size_t>(aj)]);
            std::swap(sources_[static_cast<size_t>(ai)], sources_[static_cast<size_t>(aj)]);
        }
    }

    if (onQueueChanged) onQueueChanged();
}

void PlayQueue::shuffleAll()
{
    if (!hasCurrent()) return;

    originalTracks_  = tracks_;
    originalSources_ = sources_;
    isShuffled_      = true;

    // Build new list: current track first, all others appended.
    const size_t ci = static_cast<size_t>(index_);
    std::vector<TrackInfo>   newTracks  { tracks_[ci] };
    std::vector<QueueSource> newSources { sources_[ci] };
    newTracks.reserve(tracks_.size());
    newSources.reserve(sources_.size());
    for (int i = 0; i < static_cast<int>(tracks_.size()); ++i)
        if (i != index_)
        {
            newTracks.push_back(tracks_[static_cast<size_t>(i)]);
            newSources.push_back(sources_[static_cast<size_t>(i)]);
        }

    // Fisher-Yates shuffle of everything after position 0.
    juce::Random rng;
    const int count = static_cast<int>(newTracks.size()) - 1;
    for (int i = count - 1; i > 0; --i)
    {
        const int j = rng.nextInt(i + 1);
        std::swap(newTracks [static_cast<size_t>(1 + i)], newTracks [static_cast<size_t>(1 + j)]);
        std::swap(newSources[static_cast<size_t>(1 + i)], newSources[static_cast<size_t>(1 + j)]);
    }

    tracks_  = std::move(newTracks);
    sources_ = std::move(newSources);
    index_   = 0;

    if (onQueueChanged) onQueueChanged();
    if (onIndexChanged) onIndexChanged(index_);
}

void PlayQueue::unshuffleRemaining()
{
    if (!isShuffled_) return;

    const juce::File currentFile = hasCurrent()
                                       ? tracks_[static_cast<size_t>(index_)].file
                                       : juce::File{};

    isShuffled_ = false;

    // Find where the current track sits in the original order, then keep only
    // that track and the ones that follow it — tracks that came before it in
    // the original order are already "in the past" and shouldn't reappear.
    int originalIdx = -1;
    if (currentFile.getFullPathName().isNotEmpty())
    {
        for (int i = 0; i < static_cast<int>(originalTracks_.size()); ++i)
        {
            if (originalTracks_[static_cast<size_t>(i)].file == currentFile)
            {
                originalIdx = i;
                break;
            }
        }
    }

    // Restore full original order and place index_ at the current track.
    tracks_  = originalTracks_;
    sources_ = originalSources_;
    index_   = (originalIdx >= 0) ? originalIdx : 0;

    originalTracks_.clear();
    originalSources_.clear();

    if (onQueueChanged) onQueueChanged();
    if (onIndexChanged) onIndexChanged(index_);
}

} // namespace FoxPlayer
