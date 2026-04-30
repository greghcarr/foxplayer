#include "PlaylistStore.h"

namespace Stylus
{

PlaylistStore::PlaylistStore(juce::ApplicationProperties& props)
    : props_(props)
{
    load();
    if (playlists_.empty())
        createPlaylist("Playlist 1");
}

int PlaylistStore::createPlaylist(const juce::String& name)
{
    Playlist p;
    p.id   = nextId_++;
    p.name = name;
    playlists_.push_back(std::move(p));
    save();
    if (onPlaylistsChanged) onPlaylistsChanged();
    return playlists_.back().id;
}

const Playlist* PlaylistStore::findById(int id) const
{
    for (const auto& p : playlists_)
        if (p.id == id) return &p;
    return nullptr;
}

void PlaylistStore::save()
{
    auto* settings = props_.getUserSettings();
    if (!settings) return;

    juce::Array<juce::var> arr;
    for (const auto& p : playlists_)
    {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("id",   p.id);
        obj->setProperty("name", p.name);

        juce::Array<juce::var> paths;
        for (const auto& path : p.trackPaths)
            paths.add(path);
        obj->setProperty("tracks", juce::var(paths));

        arr.add(juce::var(obj));
    }

    settings->setValue("playlists",       juce::JSON::toString(juce::var(arr), false));
    settings->setValue("playlistNextId",  nextId_);
}

void PlaylistStore::load()
{
    auto* settings = props_.getUserSettings();
    if (!settings) return;

    nextId_ = settings->getIntValue("playlistNextId", 1);

    const auto json = settings->getValue("playlists", "[]");
    const auto parsed = juce::JSON::parse(json);
    const auto* arr = parsed.getArray();
    if (!arr) return;

    for (const auto& item : *arr)
    {
        auto* obj = item.getDynamicObject();
        if (!obj) continue;

        Playlist p;
        p.id   = static_cast<int>(obj->getProperty("id"));
        p.name = obj->getProperty("name").toString();

        const auto& tracksVar = obj->getProperty("tracks");
        if (const auto* paths = tracksVar.getArray())
            for (const auto& path : *paths)
                p.trackPaths.push_back(path.toString());

        playlists_.push_back(std::move(p));
    }
}

void PlaylistStore::renamePlaylist(int id, const juce::String& newName)
{
    for (auto& pl : playlists_)
    {
        if (pl.id == id)
        {
            pl.name = newName;
            save();
            if (onPlaylistsChanged) onPlaylistsChanged();
            return;
        }
    }
}

void PlaylistStore::deletePlaylist(int id)
{
    playlists_.erase(
        std::remove_if(playlists_.begin(), playlists_.end(),
                       [id](const Playlist& p) { return p.id == id; }),
        playlists_.end());
    save();
    if (onPlaylistsChanged) onPlaylistsChanged();
}

bool PlaylistStore::containsTrack(int id, const juce::String& path) const
{
    const auto* pl = findById(id);
    if (!pl) return false;
    for (const auto& p : pl->trackPaths)
        if (p == path) return true;
    return false;
}

void PlaylistStore::addTracksToPlaylist(int id, const std::vector<juce::String>& paths)
{
    for (auto& pl : playlists_)
    {
        if (pl.id == id)
        {
            for (const auto& path : paths)
                pl.trackPaths.push_back(path);
            save();
            if (onPlaylistsChanged) onPlaylistsChanged();
            return;
        }
    }
}

void PlaylistStore::setPlaylistTracks(int id, std::vector<juce::String> paths)
{
    for (auto& pl : playlists_)
    {
        if (pl.id == id)
        {
            pl.trackPaths = std::move(paths);
            save();
            if (onPlaylistsChanged) onPlaylistsChanged();
            return;
        }
    }
}

void PlaylistStore::reorderPlaylists(const std::vector<int>& orderedIds)
{
    std::vector<Playlist> reordered;
    reordered.reserve(playlists_.size());
    for (int id : orderedIds)
        for (const auto& pl : playlists_)
            if (pl.id == id) { reordered.push_back(pl); break; }
    for (const auto& pl : playlists_)
    {
        bool listed = false;
        for (int id : orderedIds)
            if (pl.id == id) { listed = true; break; }
        if (!listed)
            reordered.push_back(pl);
    }
    playlists_ = std::move(reordered);
    save();
    if (onPlaylistsChanged) onPlaylistsChanged();
}

} // namespace Stylus
