#include "LibraryScanner.h"
#include "audio/FoxpFile.h"
#include "Constants.h"

namespace FoxPlayer
{

LibraryScanner::LibraryScanner()
    : juce::Thread("FoxPlayer.LibraryScanner")
{
}

LibraryScanner::~LibraryScanner()
{
    cancelScan();
}

void LibraryScanner::scanFolders(std::vector<juce::File> musicFolders,
                                  std::vector<juce::File> podcastFolders)
{
    cancelScan();
    {
        juce::ScopedLock sl(lock_);
        musicRoots_   = std::move(musicFolders);
        podcastRoots_ = std::move(podcastFolders);
    }
    DBG("LibraryScanner::scanFolders called with "
        + juce::String((int) musicRoots_.size()) + " music root(s), "
        + juce::String((int) podcastRoots_.size()) + " podcast root(s)");
    if (! musicRoots_.empty() || ! podcastRoots_.empty())
        startThread(juce::Thread::Priority::low);
    else if (onScanComplete)
        juce::MessageManager::callAsync([this] { if (onScanComplete) onScanComplete(0); });
}

void LibraryScanner::cancelScan()
{
    signalThreadShouldExit();
    stopThread(3000);
}

bool LibraryScanner::isScanning() const
{
    return isThreadRunning();
}

void LibraryScanner::run()
{
    std::vector<juce::File> musicRoots, podcastRoots;
    {
        juce::ScopedLock sl(lock_);
        musicRoots   = musicRoots_;
        podcastRoots = podcastRoots_;
    }

    // Helper: collect non-hidden audio files under a root folder.
    auto collectFiles = [&](const juce::File& root, juce::Array<juce::File>& out)
    {
        if (! root.isDirectory()) return;
        juce::Array<juce::File> all;
        root.findChildFiles(all, juce::File::findFiles, true);
        for (const auto& f : all)
        {
            if (threadShouldExit()) return;
            juce::File cur = f;
            bool hidden = false;
            while (cur != root)
            {
                if (cur.getFileName().startsWith(".")) { hidden = true; break; }
                cur = cur.getParentDirectory();
            }
            if (!hidden && Constants::supportedExtensions.contains(
                    f.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
                out.add(f);
        }
    };

    // Collect podcast files first so we can exclude them from the music scan.
    juce::Array<juce::File> podcastFiles;
    for (const auto& root : podcastRoots)
    {
        if (threadShouldExit()) return;
        collectFiles(root, podcastFiles);
    }
    podcastFiles.sort();

    // Build a set of podcast paths for fast lookup during music scan.
    juce::StringArray podcastPaths;
    for (const auto& f : podcastFiles)
        podcastPaths.add(f.getFullPathName());

    // Also need to know which paths fall UNDER a podcast root, so music folders
    // that contain a podcast sub-folder don't re-import those files as songs.
    auto isUnderPodcastRoot = [&podcastRoots](const juce::File& f) -> bool {
        for (const auto& pr : podcastRoots)
            if (f.isAChildOf(pr) || f == pr)
                return true;
        return false;
    };

    juce::Array<juce::File> musicFiles;
    for (const auto& root : musicRoots)
    {
        if (threadShouldExit()) return;
        juce::Array<juce::File> candidates;
        collectFiles(root, candidates);
        for (const auto& f : candidates)
        {
            if (! isUnderPodcastRoot(f))
                musicFiles.add(f);
        }
    }
    musicFiles.sort();

    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats();

    std::vector<TrackInfo> batch;
    batch.reserve(static_cast<size_t>(Constants::scannerBatchSize));
    int total = 0;

    auto flush = [&]() {
        if (batch.empty()) return;
        juce::MessageManager::callAsync([this, b = std::move(batch)]() mutable {
            if (onBatchReady) onBatchReady(std::move(b));
        });
        batch.clear();
    };

    auto emit = [&](const juce::File& file, bool asPodcast) {
        if (threadShouldExit()) return;
        batch.push_back(buildTrackInfo(file, fmgr, asPodcast));
        ++total;
        if (static_cast<int>(batch.size()) >= Constants::scannerBatchSize)
            flush();
    };

    for (const auto& f : musicFiles)   emit(f, false);
    for (const auto& f : podcastFiles) emit(f, true);
    flush();

    const int finalTotal = total;
    DBG("LibraryScanner: scan finished, total=" + juce::String(finalTotal));
    juce::MessageManager::callAsync([this, finalTotal]() {
        if (onScanComplete) onScanComplete(finalTotal);
    });
}

TrackInfo LibraryScanner::buildTrackInfo(const juce::File& file,
                                          juce::AudioFormatManager& fmgr,
                                          bool isPodcast) const
{
    TrackInfo info;
    info.file = file;
    info.isPodcast = isPodcast;

    std::unique_ptr<juce::AudioFormatReader> reader(fmgr.createReaderFor(file));
    if (reader != nullptr)
    {
        info.durationSecs = static_cast<double>(reader->lengthInSamples) / reader->sampleRate;

        const auto& meta = reader->metadataValues;
        info.title       = meta.getValue("TITLE",       "");
        info.artist      = meta.getValue("ARTIST",      "");
        info.album       = meta.getValue("ALBUM",       "");
        info.genre       = meta.getValue("GENRE",       "");
        info.year        = meta.getValue("DATE",        "");
        info.trackNumber = meta.getValue("TRACKNUMBER", "0").getIntValue();

        // CoreAudio uses different key names for some tags.
        if (info.title.isEmpty())  info.title  = meta.getValue("title",  "");
        if (info.artist.isEmpty()) info.artist = meta.getValue("artist", "");
        if (info.album.isEmpty())  info.album  = meta.getValue("album",  "");
    }

    if (!isPodcast)
    {
        // If no artist was found in the tags, try parsing the filename.
        // Handles patterns like:
        //   "Artist - Title"
        //   "01 Artist - Title"
        //   "01. Artist - Title"
        //   "01 - Artist - Title"
        if (info.artist.isEmpty())
        {
            juce::String stem = file.getFileNameWithoutExtension();

            // Strip a leading track number: 1-3 digits, optional period, then spaces.
            {
                int i = 0;
                while (i < stem.length() && juce::CharacterFunctions::isDigit(stem[i]))
                    ++i;

                if (i > 0 && i <= 3)
                {
                    int j = i;
                    if (j < stem.length() && stem[j] == '.') ++j;
                    const int spacesStart = j;
                    while (j < stem.length() && stem[j] == ' ') ++j;

                    if (j > spacesStart)
                    {
                        if (stem.substring(j).startsWith("- "))
                            j += 2;

                        if (info.trackNumber == 0)
                            info.trackNumber = stem.substring(0, i).getIntValue();

                        stem = stem.substring(j);
                    }
                }
            }

            const int sep = stem.indexOf(" - ");
            if (sep > 0)
            {
                info.artist = stem.substring(0, sep).trim();
                if (info.title.isEmpty())
                    info.title = stem.substring(sep + 3).trim();
            }
        }
    }

    // Load any previously saved user data from the sidecar. Applied before the
    // podcast-specific block so that the canonical podcast field clearing below
    // always wins over stale music metadata in the foxp.
    FoxpFile::load(info);

    if (isPodcast)
    {
        // Derive show name from foxp-stored podcast field, then album tag, then
        // parent folder name. The foxp may have a user-edited show name we want
        // to preserve; otherwise fall back to audio-tag album or the folder.
        if (info.podcast.isEmpty())
        {
            if (info.album.isNotEmpty())
                info.podcast = info.album;
            else
                info.podcast = file.getParentDirectory().getFileName();
        }
        // Always clear album and stale music-only fields for podcast tracks.
        info.album  = {};
        info.artist = {};
    }
    else
    {
        // Clear any stale podcast fields left over from a previous scan as podcast.
        info.podcast = {};
    }

    return info;
}

} // namespace FoxPlayer
