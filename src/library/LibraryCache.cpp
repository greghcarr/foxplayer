#include "LibraryCache.h"

namespace FoxPlayer
{

static constexpr int cacheVersion = 1;

juce::File LibraryCache::cacheFile()
{
    auto dir = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    dir = dir.getChildFile("Application Support");
   #endif
    return dir.getChildFile("FoxPlayer.libcache.json");
}

bool LibraryCache::save(const std::vector<TrackInfo>& tracks,
                         const std::vector<juce::File>& folders)
{
    auto* root = new juce::DynamicObject();
    root->setProperty("version", cacheVersion);

    juce::Array<juce::var> folderArr;
    for (const auto& f : folders)
        folderArr.add(f.getFullPathName());
    root->setProperty("folders", folderArr);

    juce::Array<juce::var> trackArr;
    trackArr.ensureStorageAllocated(static_cast<int>(tracks.size()));
    for (const auto& t : tracks)
    {
        auto* o = new juce::DynamicObject();
        o->setProperty("file",        t.file.getFullPathName());
        o->setProperty("title",       t.title);
        o->setProperty("artist",      t.artist);
        o->setProperty("album",       t.album);
        o->setProperty("genre",       t.genre);
        o->setProperty("year",        t.year);
        o->setProperty("trackNumber", t.trackNumber);
        o->setProperty("durationSecs",t.durationSecs);
        o->setProperty("bpm",         t.bpm);
        o->setProperty("key",         t.musicalKey);
        o->setProperty("lufs",        static_cast<double>(t.lufs));
        o->setProperty("hidden",      t.hidden);
        o->setProperty("playCount",   t.playCount);
        trackArr.add(juce::var(o));
    }
    root->setProperty("tracks", trackArr);

    const juce::var rootVar(root);
    const juce::String text = juce::JSON::toString(rootVar, true);

    auto file = cacheFile();
    file.getParentDirectory().createDirectory();
    return file.replaceWithText(text);
}

bool LibraryCache::tryLoad(std::vector<TrackInfo>& outTracks,
                            std::vector<juce::File>& outFolders)
{
    const auto file = cacheFile();
    if (! file.existsAsFile()) return false;

    const juce::String text = file.loadFileAsString();
    if (text.isEmpty()) return false;

    juce::var root;
    try
    {
        root = juce::JSON::parse(text);
    }
    catch (...)
    {
        return false;
    }
    if (! root.isObject()) return false;

    auto* obj = root.getDynamicObject();
    if (obj == nullptr) return false;
    if (static_cast<int>(obj->getProperty("version")) != cacheVersion) return false;

    auto foldersVar = obj->getProperty("folders");
    if (! foldersVar.isArray()) return false;

    auto tracksVar = obj->getProperty("tracks");
    if (! tracksVar.isArray()) return false;

    std::vector<juce::File>  folders;
    std::vector<TrackInfo>   tracks;
    folders.reserve(static_cast<size_t>(foldersVar.size()));
    tracks.reserve (static_cast<size_t>(tracksVar.size()));

    for (const auto& f : *foldersVar.getArray())
        folders.emplace_back(f.toString());

    for (const auto& v : *tracksVar.getArray())
    {
        auto* t = v.getDynamicObject();
        if (t == nullptr) continue;

        TrackInfo info;
        info.file         = juce::File(t->getProperty("file").toString());
        info.title        = t->getProperty("title").toString();
        info.artist       = t->getProperty("artist").toString();
        info.album        = t->getProperty("album").toString();
        info.genre        = t->getProperty("genre").toString();
        info.year         = t->getProperty("year").toString();
        info.trackNumber  = static_cast<int>   (t->getProperty("trackNumber"));
        info.durationSecs = static_cast<double>(t->getProperty("durationSecs"));
        info.bpm          = static_cast<double>(t->getProperty("bpm"));
        info.musicalKey   = t->getProperty("key").toString();
        info.lufs         = static_cast<float> (static_cast<double>(t->getProperty("lufs")));
        info.hidden       = static_cast<bool>  (t->getProperty("hidden"));
        info.playCount    = static_cast<int>   (t->getProperty("playCount"));
        tracks.push_back(std::move(info));
    }

    outTracks  = std::move(tracks);
    outFolders = std::move(folders);
    return true;
}

} // namespace FoxPlayer
