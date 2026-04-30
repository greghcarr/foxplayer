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

    // Collect music files paired with their owning root so folder inference can
    // determine depth. Sort globally by path so the library appears alphabetical.
    std::vector<std::pair<juce::File, juce::File>> musicFilesWithRoot;
    for (const auto& root : musicRoots)
    {
        if (threadShouldExit()) return;
        juce::Array<juce::File> candidates;
        collectFiles(root, candidates);
        for (const auto& f : candidates)
            if (! isUnderPodcastRoot(f))
                musicFilesWithRoot.push_back({ f, root });
    }
    std::sort(musicFilesWithRoot.begin(), musicFilesWithRoot.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

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

        // Yield the CPU between batches: scans were saturating a core during
        // startup. Trades a slower scan for a more responsive system; the
        // message thread also gets cleaner windows in which to apply each
        // batch's UI updates.
        if (Constants::scannerInterBatchPauseMs > 0)
            wait(Constants::scannerInterBatchPauseMs);
    };

    auto emit = [&](const juce::File& file, bool asPodcast, const juce::File& root = {}) {
        if (threadShouldExit()) return;
        batch.push_back(buildTrackInfo(file, fmgr, asPodcast, root));
        ++total;
        if (static_cast<int>(batch.size()) >= Constants::scannerBatchSize)
            flush();
    };

    for (const auto& [f, root] : musicFilesWithRoot) emit(f, false, root);
    for (const auto& f : podcastFiles)                emit(f, true);
    flush();

    const int finalTotal = total;
    DBG("LibraryScanner: scan finished, total=" + juce::String(finalTotal));
    juce::MessageManager::callAsync([this, finalTotal]() {
        if (onScanComplete) onScanComplete(finalTotal);
    });
}

// static
int LibraryScanner::guessEpisodeNumber(const juce::String& stem)
{
        // Normalise separators to spaces and lowercase for pattern matching.
        const juce::String s = stem.toLowerCase().replaceCharacters("-_.", "   ");

        auto parseInt = [&](int i) -> int {
            const int start = i;
            while (i < s.length() && juce::CharacterFunctions::isDigit(s[i])) ++i;
            if (i == start) return 0;
            return s.substring(start, i).getIntValue();
        };

        // Pattern A: "episode <N>" or "ep <N>" — keyword followed by digits.
        for (const auto* kw : { "episode", "ep" })
        {
            const juce::String keyword(kw);
            const int kwLen = keyword.length();

            for (int pos = s.indexOf(keyword); pos >= 0; pos = s.indexOf(pos + 1, keyword))
            {
                // Require a word boundary before the keyword.
                if (pos > 0 && juce::CharacterFunctions::isLetter(s[pos - 1]))
                    continue;

                int after = pos + kwLen;

                // For "ep", ensure it is not the start of a longer word (e.g. "episode").
                if (kwLen == 2 && after < s.length() && juce::CharacterFunctions::isLetter(s[after]))
                    continue;

                // Skip any whitespace between keyword and number.
                while (after < s.length() && s[after] == ' ') ++after;

                const int num = parseInt(after);
                if (num > 0) return num;
            }
        }

        // Pattern B: leading number at the very start of the stem (1-4 digits).
        {
            int i = 0;
            while (i < s.length() && s[i] == ' ') ++i;
            const int start = i;
            while (i < s.length() && juce::CharacterFunctions::isDigit(s[i])) ++i;
            const int numLen = i - start;
            if (numLen >= 1 && numLen <= 4)
            {
                const int num = s.substring(start, i).getIntValue();
                // Skip 4-digit years.
                const bool isYear = (numLen == 4 && num >= 1900 && num <= 2099);
                if (!isYear && num > 0)
                    return num;
            }
        }

        // Pattern C: first 1-5 digit number anywhere in the stem.
        for (int i = 0; i < s.length(); ++i)
        {
            if (!juce::CharacterFunctions::isDigit(s[i])) continue;
            const int start = i;
            while (i < s.length() && juce::CharacterFunctions::isDigit(s[i])) ++i;
            const int numLen = i - start;
            if (numLen >= 1 && numLen <= 5)
            {
                const int num = s.substring(start, i).getIntValue();
                const bool isYear = (numLen == 4 && num >= 1900 && num <= 2099);
                if (!isYear && num > 0) return num;
            }
        }

        return 0;
}

TrackInfo LibraryScanner::buildTrackInfo(const juce::File& file,
                                          juce::AudioFormatManager& fmgr,
                                          bool isPodcast,
                                          const juce::File& root) const
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

    // Infer album and artist from folder names when tags and filename parsing
    // leave them empty. Parent folder = album, grandparent folder = artist,
    // both only when still inside the music root. FoxpFile::load() below can
    // still override these with user-edited values.
    if (!isPodcast && root.isDirectory())
    {
        const juce::File parentDir   = file.getParentDirectory();
        const juce::File grandParDir = parentDir.getParentDirectory();

        if (info.album.isEmpty() && parentDir != root && parentDir.isAChildOf(root))
            info.album = parentDir.getFileName();

        if (info.artist.isEmpty() && grandParDir != root && grandParDir.isAChildOf(root))
            info.artist = grandParDir.getFileName();
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

        // Infer episode number from filename when none was found in tags or
        // foxp. Skip when the foxp explicitly contained a trackNumber
        // (including 0, meaning the user cleared the field for a "bonus"
        // episode that has no episode number).
        if (info.trackNumber == 0 && !info.foxpHadTrackNumber)
            info.trackNumber = guessEpisodeNumber(file.getFileNameWithoutExtension());
    }
    else
    {
        // Clear any stale podcast fields left over from a previous scan as podcast.
        info.podcast = {};
    }

    if (info.dateAdded == 0)
    {
        info.dateAdded = juce::Time::currentTimeMillis();
        FoxpFile::save(info);
    }

    return info;
}

} // namespace FoxPlayer
