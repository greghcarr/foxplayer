#include "AppleMusicLookup.h"
#include "audio/FoxpFile.h"

namespace FoxPlayer
{

AppleMusicLookup::AppleMusicLookup()
    : juce::Thread("FoxPlayer.AppleMusicLookup")
{
}

AppleMusicLookup::~AppleMusicLookup()
{
    cancelAll();
}

juce::File AppleMusicLookup::artworkSidecarFor(const juce::File& audioFile)
{
    const juce::File parent = audioFile.getParentDirectory();
    const juce::String hidden = "." + audioFile.getFileName() + ".foxp-art.jpg";
    return parent.getChildFile(hidden);
}

void AppleMusicLookup::enqueue(const TrackInfo& track, bool overwrite)
{
    {
        juce::ScopedLock sl(queueLock_);
        queue_.push_back(Job{ track, overwrite, /*isBatch=*/false });
    }

    if (onLookupQueued) onLookupQueued(track);

    if (! suspended_.load() && ! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AppleMusicLookup::enqueueAll(const std::vector<TrackInfo>& tracks, bool overwrite)
{
    {
        juce::ScopedLock sl(queueLock_);
        for (const auto& t : tracks)
            queue_.push_back(Job{ t, overwrite, /*isBatch=*/true });
    }

    if (onLookupQueued)
        for (const auto& t : tracks)
            onLookupQueued(t);

    if (! suspended_.load() && ! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AppleMusicLookup::enqueueArtOnly(const TrackInfo& track)
{
    {
        juce::ScopedLock sl(queueLock_);
        queue_.push_back(Job{ track, /*overwrite=*/true, /*isBatch=*/false, /*artOnly=*/true });
    }
    if (onLookupQueued) onLookupQueued(track);
    if (! suspended_.load() && ! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AppleMusicLookup::enqueueAllArtOnly(const std::vector<TrackInfo>& tracks)
{
    {
        juce::ScopedLock sl(queueLock_);
        for (const auto& t : tracks)
            queue_.push_back(Job{ t, /*overwrite=*/true, /*isBatch=*/true, /*artOnly=*/true });
    }
    if (onLookupQueued)
        for (const auto& t : tracks)
            onLookupQueued(t);
    if (! suspended_.load() && ! isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AppleMusicLookup::cancelAll()
{
    {
        juce::ScopedLock sl(queueLock_);
        queue_.clear();
    }
    signalThreadShouldExit();
    stopThread(4000);
}

void AppleMusicLookup::run()
{
    if (suspended_.load()) return;

    while (! threadShouldExit())
    {
        Job job;
        {
            juce::ScopedLock sl(queueLock_);
            if (queue_.empty()) break;
            job = queue_.front();
            queue_.pop_front();
        }

        processOne(std::move(job));

        if (suspended_.load()) break;
    }
}

namespace
{
    // Builds the search query from whatever metadata we have. Title + artist
    // works best; falls back to the bare filename when both are missing.
    juce::String buildQuery(const TrackInfo& track)
    {
        juce::StringArray parts;
        if (track.title.isNotEmpty())  parts.add(track.title);
        if (track.artist.isNotEmpty()) parts.add(track.artist);

        if (parts.isEmpty())
            parts.add(track.file.getFileNameWithoutExtension());

        return parts.joinIntoString(" ");
    }

    // iTunes Search API returns artworkUrl100 for a 100x100 image. The size
    // segment can be swapped for a larger version like 600x600.
    juce::String upscaleArtworkUrl(const juce::String& url100, int targetPx)
    {
        const juce::String oldSize = "100x100bb";
        const juce::String newSize = juce::String(targetPx) + "x"
                                   + juce::String(targetPx) + "bb";
        if (url100.contains(oldSize))
            return url100.replace(oldSize, newSize);
        return url100;
    }

    // Scores a single iTunes result for how well it matches the track.
    // Higher is better. Criteria (additive):
    //   +4  artist matches exactly (case-insensitive)
    //   +2  collectionType is "Album" (not a single/EP release)
    //   +1  trackCount > 1 (belt-and-suspenders album check)
    //   +3  collectionName matches the dominant album already resolved for
    //       this track's directory (cross-track consistency)
    int scoreResult(const juce::var& result,
                    const TrackInfo& track,
                    const juce::String& hintAlbum)
    {
        auto* obj = result.getDynamicObject();
        if (obj == nullptr) return -1;

        int score = 0;

        if (track.artist.isNotEmpty())
        {
            const juce::String got = obj->getProperty("artistName").toString().toLowerCase();
            if (got == track.artist.toLowerCase())
                score += 4;
        }

        const juce::String colType = obj->getProperty("collectionType").toString();
        if (colType.equalsIgnoreCase("Album"))
            score += 2;

        const int trackCount = static_cast<int>(obj->getProperty("trackCount"));
        if (trackCount > 1)
            score += 1;

        if (hintAlbum.isNotEmpty())
        {
            const juce::String colName = obj->getProperty("collectionName").toString();
            if (colName.equalsIgnoreCase(hintAlbum))
                score += 3;
        }

        return score;
    }

    juce::var pickBestMatch(const juce::Array<juce::var>& results,
                            const TrackInfo& track,
                            const juce::String& hintAlbum)
    {
        if (results.isEmpty()) return {};

        int      bestScore = -1;
        juce::var best;

        for (const auto& r : results)
        {
            const int s = scoreResult(r, track, hintAlbum);
            if (s > bestScore) { bestScore = s; best = r; }
        }

        return best;
    }
}

void AppleMusicLookup::processOne(Job job)
{
    TrackInfo       track     = job.track;
    const bool      overwrite = job.overwrite;
    const bool      isBatch   = job.isBatch;
    const bool      artOnly   = job.artOnly;

    juce::MessageManager::callAsync([this, t = track]() mutable {
        if (onLookupStarted) onLookupStarted(std::move(t));
    });

    if (threadShouldExit()) return;

    const juce::String query = buildQuery(track);
    const juce::URL url = juce::URL("https://itunes.apple.com/search")
                              .withParameter("term",   query)
                              .withParameter("entity", "song")
                              .withParameter("limit",  "10");

    juce::String response;
    {
        const auto opts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                              .withConnectionTimeoutMs(8000);
        if (auto stream = url.createInputStream(opts))
            response = stream->readEntireStreamAsString();
    }

    if (response.isEmpty() || threadShouldExit())
    {
        ++consecutiveNetworkFailures_;
        const bool shouldSuspend = (consecutiveNetworkFailures_ >= maxConsecutiveFailures);
        if (shouldSuspend) suspended_.store(true);

        juce::MessageManager::callAsync([this, t = track, isBatch, shouldSuspend]() mutable {
            if (onLookupCompleted) onLookupCompleted(std::move(t), "Network error", isBatch);
            if (shouldSuspend && onLookupSuspended) onLookupSuspended();
        });
        return;
    }

    const juce::var json = juce::JSON::parse(response);
    auto* root = json.getDynamicObject();
    if (root == nullptr || ! root->hasProperty("results"))
    {
        juce::MessageManager::callAsync([this, t = track, isBatch]() mutable {
            if (onLookupCompleted) onLookupCompleted(std::move(t), "No match", isBatch);
        });
        return;
    }

    auto resultsVar = root->getProperty("results");
    if (! resultsVar.isArray())
    {
        juce::MessageManager::callAsync([this, t = track, isBatch]() mutable {
            if (onLookupCompleted) onLookupCompleted(std::move(t), "No match", isBatch);
        });
        return;
    }

    const auto& results = *resultsVar.getArray();

    // Look up the dominant album already resolved for this directory so the
    // scorer can prefer results that are consistent with sibling tracks.
    const juce::String dirKey  = track.file.getParentDirectory().getFullPathName();
    juce::String       hintAlbum;
    {
        auto it = resolvedAlbums_.find(dirKey);
        if (it != resolvedAlbums_.end() && !it->second.empty())
        {
            // Pick the album name with the highest count.
            int best = 0;
            for (const auto& kv : it->second)
                if (kv.second > best) { best = kv.second; hintAlbum = kv.first; }
        }
    }

    const juce::var match = pickBestMatch(results, track, hintAlbum);
    auto* matchObj = match.getDynamicObject();
    if (matchObj == nullptr)
    {
        juce::MessageManager::callAsync([this, t = track, isBatch]() mutable {
            if (onLookupCompleted) onLookupCompleted(std::move(t), "No match", isBatch);
        });
        return;
    }

    bool changed = false;

    if (! artOnly)
    {
        // Fill in text metadata. With overwrite=false only blank fields are
        // populated; with overwrite=true any matching Apple Music value replaces
        // the existing content.
        auto applyString = [&](juce::String& field, const juce::String& value) {
            if (value.isEmpty()) return;
            if (! overwrite && ! field.isEmpty()) return;
            if (field == value) return;
            field   = value;
            changed = true;
        };

        applyString(track.album,  matchObj->getProperty("collectionName").toString());
        applyString(track.artist, matchObj->getProperty("artistName").toString());
        applyString(track.genre,  matchObj->getProperty("primaryGenreName").toString());
        applyString(track.title,  matchObj->getProperty("trackName").toString());

        if (overwrite || track.year.isEmpty())
        {
            const juce::String releaseDate = matchObj->getProperty("releaseDate").toString();
            if (releaseDate.length() >= 4)
            {
                const juce::String y = releaseDate.substring(0, 4);
                if (track.year != y)
                {
                    track.year = y;
                    changed    = true;
                }
            }
        }

        if ((overwrite || track.trackNumber == 0) && matchObj->hasProperty("trackNumber"))
        {
            const int n = static_cast<int>(matchObj->getProperty("trackNumber"));
            if (n > 0 && n != track.trackNumber)
            {
                track.trackNumber = n;
                changed           = true;
            }
        }
    }

    // Album art: with overwrite=false, only download if no sidecar exists yet.
    // With overwrite=true, always re-download and replace.
    const juce::File artFile = artworkSidecarFor(track.file);
    if (overwrite || ! artFile.existsAsFile())
    {
        const juce::String url100 = matchObj->getProperty("artworkUrl100").toString();
        if (url100.isNotEmpty())
        {
            const juce::URL artUrl(upscaleArtworkUrl(url100, 600));
            const auto artOpts = juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                                     .withConnectionTimeoutMs(8000);
            if (auto stream = artUrl.createInputStream(artOpts))
            {
                juce::MemoryBlock data;
                stream->readIntoMemoryBlock(data);
                if (data.getSize() > 0)
                {
                    artFile.replaceWithData(data.getData(), data.getSize());
                    changed = true;
                }
            }
        }
    }

    if (changed && ! artOnly)
        FoxpFile::save(track);

    // A successful match resets the consecutive-failure counter.
    consecutiveNetworkFailures_ = 0;

    // Record the resolved album for this directory so future tracks in the
    // same folder can bias toward the same collection.
    const juce::String resolvedAlbum = matchObj->getProperty("collectionName").toString();
    if (resolvedAlbum.isNotEmpty())
        resolvedAlbums_[dirKey][resolvedAlbum]++;

    juce::String summary;
    const juce::String foundAlbum  = resolvedAlbum;
    const juce::String foundArtist = matchObj->getProperty("artistName").toString();
    if (foundAlbum.isNotEmpty())
        summary = "Found: " + foundAlbum;
    else if (foundArtist.isNotEmpty())
        summary = "Found: " + foundArtist;
    else
        summary = "Found";

    juce::MessageManager::callAsync([this, t = track, summary, isBatch]() mutable {
        if (onLookupCompleted) onLookupCompleted(std::move(t), summary, isBatch);
    });
}

} // namespace FoxPlayer
