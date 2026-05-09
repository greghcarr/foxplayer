#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace Stylus
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

    // Inline-rename callbacks for the rest of the sidebar's renamable
    // categories. Each fires from a double-click + commit on the matching
    // row when the user's input passes the per-category validation in
    // commitRename(). Receivers are expected to update every track in the
    // group and refresh the affected sidebar sections.
    std::function<void(int sidebarId, juce::String newArtist)> onRenameArtist;
    std::function<void(int sidebarId, juce::String newArtist, juce::String newAlbum)> onRenameAlbum;
    std::function<void(int sidebarId, juce::String newGenre)>   onRenameGenre;
    std::function<void(int sidebarId, juce::String newPodcast)> onRenamePodcast;
    // Fired when the user picks "Create Playlist" from an artist/album/genre item.
    // suggestedName is pre-filled from the item label.
    std::function<void(int sidebarId, juce::String suggestedName)> onCreatePlaylistFromItem;

    // Fired when the user picks "Play Next" or "Add to Queue" from a sidebar item.
    std::function<void(int sidebarId)> onPlayNextFromItem;
    std::function<void(int sidebarId)> onAddToQueueFromItem;

    // Fired when the user presses Right arrow / Tab on a sidebar row,
    // indicating they want to move keyboard focus into the library view
    // (or whichever pane the wiring decides). MainComponent grabs focus
    // on the library table so arrow keys then walk the rows there.
    std::function<void()> onMoveFocusToLibrary;

    // Fired on Shift-Tab from the sidebar - reverse-cycle entry point that
    // lands in the queue (when it has rows; the wiring no-ops otherwise).
    std::function<void()> onMoveFocusToQueue;

    // Fired whenever the sidebar gains JUCE keyboard focus. MainComponent
    // wires it up to clear library / queue selections so the visual mutex
    // ("only one pane is the focused pane") holds whether you got here via
    // mouse, arrow nav, or Tab.
    std::function<void()> onFocusGained;

    // Returns the user's playlists in display order, as (storeId, name)
    // pairs. Used to populate the "Add to Playlist" right-click submenu on
    // sidebar items. If unset or returns empty, the submenu is omitted.
    std::function<std::vector<std::pair<int, juce::String>>()> getPlaylistsForMenu;

    // Fired when the user picks a playlist under "Add to Playlist" from a
    // sidebar item's right-click menu. The receiver should append every
    // track collected from the source sidebar item to the destination
    // playlist (matching drag-and-drop semantics).
    std::function<void(int sourceSidebarId, int destPlaylistStoreId)> onAddToPlaylistFromItem;

    // Fired when the user drags a playlist item to a new position.
    // newOrder contains the playlist sidebar IDs (1000+storeId) in the new order.
    std::function<void(std::vector<int>)> onPlaylistsReordered;

    int  selectedId() const { return selectedId_; }
    void setSelectedId(int id);

    // Focus is the "this row is the user's currently active selection"
    // state, separate from selectedId_'s "this row dictates the library
    // view" state. The active row always renders the indicator bar +
    // white text; the focused row additionally draws the gray overlay.
    // The two are mutually exclusive with library / queue selection: if
    // the user is acting on the library or queue, the sidebar drops its
    // focus so the visible "selection" only ever lives in one of the
    // three panes. Future keyboard nav keys off this state.
    int  focusedId() const { return focusedId_; }
    void clearFocus();

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

    // Total occlusion height at the viewport top for a row at content-y
    // `rowTop`. LIBRARY-section rows return 0 (they're never occluded -
    // they ARE the sticky overlay). Non-LIBRARY rows return the LIBRARY
    // section height plus the active-section header height, which
    // matches the actual sticky overlay you see when scrolled mid-section.
    // Callers use this to nudge a focused row out from behind the
    // sticky overlay rather than parking it underneath.
    int  topStickyHeightFor(int rowTop) const;

    // Just the LIBRARY-section height. Used to identify whether a given
    // row is in the LIBRARY sticky zone (for early-out: those rows are
    // always visible via the overlay so no scroll is needed).
    int  libraryStickyHeight() const;

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
    bool keyPressed(const juce::KeyPress& key) override;
    void focusGained(FocusChangeType) override;
    void focusLost(FocusChangeType) override;

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
    // Focused row id - the one that draws the gray overlay. Defaults to
    // the same value as selectedId_ so the sidebar starts up with focus
    // on the initial Library row.
    int                  focusedId_      { 1 };
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

    // Spinning loading indicator, driven by section.loading flags.
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

} // namespace Stylus
