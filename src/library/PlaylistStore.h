#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

struct Playlist
{
    int          id   { 0 };
    juce::String name;
    std::vector<juce::String> trackPaths; // ordered file paths
};

class PlaylistStore
{
public:
    explicit PlaylistStore(juce::ApplicationProperties& props);

    const std::vector<Playlist>& all()          const { return playlists_; }
    const Playlist*              findById(int id) const;

    // Creates a new playlist and returns its id.
    int  createPlaylist(const juce::String& name);

    void renamePlaylist(int id, const juce::String& newName);
    void deletePlaylist(int id);

    // Returns true if the playlist already contains the given file path.
    bool containsTrack(int id, const juce::String& path) const;

    // Appends paths to the playlist (no duplicate filtering — callers decide).
    void addTracksToPlaylist(int id, const std::vector<juce::String>& paths);

    // Replaces the playlist's track list with the given ordered paths. Used
    // by the library view to persist drag-reordering within a playlist.
    void setPlaylistTracks(int id, std::vector<juce::String> paths);

    // Reorders the playlist list to match the given sequence of playlist IDs.
    // IDs not present in orderedIds are appended at the end.
    void reorderPlaylists(const std::vector<int>& orderedIds);

    std::function<void()> onPlaylistsChanged;

private:
    void save();
    void load();

    juce::ApplicationProperties& props_;
    std::vector<Playlist>        playlists_;
    int                          nextId_ { 1 };
};

} // namespace FoxPlayer
