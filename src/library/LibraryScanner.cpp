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

void LibraryScanner::scanFolder(const juce::File& folder)
{
    cancelScan();
    {
        juce::ScopedLock sl(lock_);
        scanRoot_ = folder;
    }
    startThread(juce::Thread::Priority::low);
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
    juce::File root;
    {
        juce::ScopedLock sl(lock_);
        root = scanRoot_;
    }

    if (!root.isDirectory()) return;

    // Collect all audio files.
    juce::Array<juce::File> allFiles;
    root.findChildFiles(allFiles,
                        juce::File::findFiles,
                        true /* recursive */);

    // Returns true if any path component (file or folder) starts with '.'.
    auto hasHiddenComponent = [&](const juce::File& f) -> bool {
        juce::File current = f;
        while (current != root)
        {
            if (current.getFileName().startsWith("."))
                return true;
            current = current.getParentDirectory();
        }
        return false;
    };

    // Filter to supported extensions, excluding hidden files and folders.
    juce::Array<juce::File> audioFiles;
    for (const auto& f : allFiles)
    {
        if (threadShouldExit()) return;
        if (Constants::supportedExtensions.contains(
                f.getFileExtension().trimCharactersAtStart(".").toLowerCase())
            && !hasHiddenComponent(f))
        {
            audioFiles.add(f);
        }
    }

    // Sort alphabetically.
    audioFiles.sort();

    juce::AudioFormatManager fmgr;
    fmgr.registerBasicFormats(); // includes CoreAudioFormat on macOS

    std::vector<TrackInfo> batch;
    batch.reserve(static_cast<size_t>(Constants::scannerBatchSize));
    int total = 0;

    for (const auto& file : audioFiles)
    {
        if (threadShouldExit()) return;

        TrackInfo info = buildTrackInfo(file, fmgr);
        batch.push_back(std::move(info));
        ++total;

        if (static_cast<int>(batch.size()) >= Constants::scannerBatchSize)
        {
            auto batchCopy = batch;
            juce::MessageManager::callAsync([this, b = std::move(batchCopy)]() mutable {
                if (onBatchReady) onBatchReady(std::move(b));
            });
            batch.clear();
        }
    }

    // Flush remaining tracks.
    if (!batch.empty())
    {
        juce::MessageManager::callAsync([this, b = std::move(batch)]() mutable {
            if (onBatchReady) onBatchReady(std::move(b));
        });
    }

    const int finalTotal = total;
    juce::MessageManager::callAsync([this, finalTotal]() {
        if (onScanComplete) onScanComplete(finalTotal);
    });
}

TrackInfo LibraryScanner::buildTrackInfo(const juce::File& file,
                                          juce::AudioFormatManager& fmgr) const
{
    TrackInfo info;
    info.file = file;

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
        // Capture it as the track number if none was found in tags.
        {
            int i = 0;
            while (i < stem.length() && juce::CharacterFunctions::isDigit(stem[i]))
                ++i;

            if (i > 0 && i <= 3)
            {
                int j = i;
                if (j < stem.length() && stem[j] == '.') ++j;  // optional period
                // consume spaces
                const int spacesStart = j;
                while (j < stem.length() && stem[j] == ' ') ++j;

                if (j > spacesStart)  // at least one space after the digits
                {
                    // If the next token is also a separator (" - "), consume it too.
                    // e.g. "01 - Artist - Title"
                    if (stem.substring(j).startsWith("- "))
                        j += 2;

                    if (info.trackNumber == 0)
                        info.trackNumber = stem.substring(0, i).getIntValue();

                    stem = stem.substring(j);
                }
            }
        }

        // Now try "Artist - Title" on the cleaned stem.
        const int sep = stem.indexOf(" - ");
        if (sep > 0)
        {
            info.artist = stem.substring(0, sep).trim();
            if (info.title.isEmpty())
                info.title = stem.substring(sep + 3).trim();
        }
    }

    // Load any previously saved data from the sidecar (overrides all of the above).
    FoxpFile::load(info);

    return info;
}

} // namespace FoxPlayer
