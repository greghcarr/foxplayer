#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class SidebarComponent : public juce::Component,
                         public juce::TooltipClient,
                         public juce::DragAndDropTarget,
                         private juce::Timer
{
public:
    SidebarComponent();

    // id=1 for Music; id = 1000 + playlistStoreId for playlists.
    std::function<void(int id)> onItemSelected;
    std::function<void()>       onCreatePlaylistRequested;

    // Called when tracks are dropped onto a playlist item.
    // sidebarId is the playlist's sidebar ID (1000 + storeId).
    // paths is a newline-separated list of file paths.
    std::function<void(int sidebarId, juce::StringArray paths)> onTracksDropped;

    // Called when tracks are dropped onto the "+ New Playlist" item.
    std::function<void(juce::StringArray paths)> onNewPlaylistWithTracksRequested;

    std::function<void(int sidebarId, juce::String newName)> onRenamePlaylist;
    std::function<void(int sidebarId)>                       onDeletePlaylist;
    std::function<void(int sidebarId)>                       onDuplicatePlaylist;
    // Fired when the user picks "Create Playlist" from an artist/album/genre item.
    // suggestedName is pre-filled from the item label.
    std::function<void(int sidebarId, juce::String suggestedName)> onCreatePlaylistFromItem;

    // Fired when the user picks "Play Next" or "Add to Queue" from a sidebar item.
    std::function<void(int sidebarId)> onPlayNextFromItem;
    std::function<void(int sidebarId)> onAddToQueueFromItem;

    // Fired when the user drags a playlist item to a new position.
    // newOrder contains the playlist sidebar IDs (1000+storeId) in the new order.
    std::function<void(std::vector<int>)> onPlaylistsReordered;

    int  selectedId() const { return selectedId_; }
    void setSelectedId(int id);

    // Toggles a small spinning indicator next to the LIBRARY heading while
    // the music-folder scanner is running.
    void setLibraryLoading(bool loading);

    // Shows a red error icon on the LIBRARY headings instead of the spinner.
    // Each string in messages describes one inaccessible folder.
    // Pass an empty array to clear the error state.
    void setLibraryErrors(const juce::StringArray& messages);

    juce::String getTooltip() override;

    // Returns the bounds (in this component's coord space) of the currently
    // selected item, or an empty rectangle if no selected item is visible
    // (e.g. it's inside a collapsed section or it has no match).
    juce::Rectangle<int> boundsForSelectedItem() const;

    // Replaces the items shown under the Playlists section.
    // Each pair is { sidebarId, displayName }.
    void setPlaylists(const std::vector<std::pair<int, juce::String>>& playlists);

    // Replaces the items shown under the Artists section.
    // Each pair is { sidebarId, displayName }.
    void setArtists(const std::vector<std::pair<int, juce::String>>& artists);

    // Replaces the items shown under the Albums section.
    // Each pair is { sidebarId, displayName }.
    void setAlbums(const std::vector<std::pair<int, juce::String>>& albums);

    // Replaces the items shown under the Genres section.
    // Each pair is { sidebarId, displayName }.
    void setGenres(const std::vector<std::pair<int, juce::String>>& genres);

    // Replaces the items shown under the Podcasts section.
    // Each pair is { sidebarId, displayName }.
    void setPodcasts(const std::vector<std::pair<int, juce::String>>& podcasts);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    struct Item
    {
        juce::String         label;
        int                  id { 0 };
        juce::Rectangle<int> bounds;
    };

    struct Section
    {
        juce::String         heading;
        bool                 collapsible    { false };
        bool                 collapsed      { false };
        bool                 rightClickable { false };
        bool                 loading        { false };
        bool                 hasError       { false };
        std::vector<Item>    items;
        juce::Rectangle<int> headerBounds;
    };

    void layoutItems();
    void selectId(int id);
    void drawDisclosureTriangle(juce::Graphics& g,
                                float x, int centreY,
                                bool collapsed) const;

    // Draws just the heading row for a section at an arbitrary y coordinate.
    void drawSectionHeader(juce::Graphics& g, const Section& section, int y) const;
    // Draws a single item row at the given bounds (may differ from item.bounds for sticky).
    void drawSectionItem(juce::Graphics& g, const Item& item, juce::Rectangle<int> bounds) const;
    // Computes the current sticky-header layout: scroll offset, library zone height,
    // and the index of the "active" section whose header should also be pinned (-1 if none).
    void getStickyZone(int& outScrollY, int& outLibStickyH, int& outActiveSectionIdx) const;

    // Repaints the visible strip when the component is scrolled by the viewport.
    void moved() override;

    // Returns the sidebar ID of the playlist item at pos, or -1 if none.
    int playlistItemIdAt(juce::Point<int> pos) const;

    void startRename(int sidebarId);
    void commitRename();
    void cancelRename();

    std::vector<Section> sections_;
    int                  selectedId_     { 1 };
    int                  dragOverItemId_ { -1 };
    int                  editingItemId_  { -1 };

    // Playlist drag-reorder state
    int                  reorderDragId_       { -1 };  // sidebar ID being dragged
    int                  reorderInsertBefore_ { -1 };  // insert before this index in items array (-1 = no active drag)
    bool                 reorderActive_       { false };
    juce::Point<int>     reorderDragStart_;
    juce::String         editingOriginalName_;
    std::unique_ptr<juce::TextEditor> inlineEditor_;

    // Pre-loaded SVG drawables for row icons. Tinted lazily per-paint.
    std::unique_ptr<juce::Drawable> musicIconDrawable_;
    std::unique_ptr<juce::Drawable> podcastIconDrawable_;
    std::unique_ptr<juce::Drawable> playlistIconDrawable_;
    std::unique_ptr<juce::Drawable> artistIconDrawable_;
    std::unique_ptr<juce::Drawable> albumIconDrawable_;
    std::unique_ptr<juce::Drawable> genreIconDrawable_;

    // Spinning loading indicator — driven by section.loading flags.
    float  loadingRotation_  { 0.0f };

    // Error state for library sections (inaccessible folders).
    juce::StringArray libraryErrorMessages_;
    mutable std::vector<juce::Rectangle<float>> libraryErrorIconRects_;

    void timerCallback() override;

    static constexpr int sectionHeaderH    = 30;
    static constexpr int itemH             = 36;
    static constexpr int indicatorW        = 3;
    static constexpr int itemPadL          = 16;
    static constexpr int newPlaylistItemId = -2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)
};

} // namespace FoxPlayer
