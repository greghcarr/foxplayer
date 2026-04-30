#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class LibraryTableComponent : public juce::Component,
                               public juce::TableListBoxModel,
                               public juce::DragAndDropTarget,
                               public juce::TableHeaderComponent::Listener
{
public:
    LibraryTableComponent();
    ~LibraryTableComponent() override = default;

    void setTracks(const std::vector<TrackInfo>& tracks);
    void appendTracks(const std::vector<TrackInfo>& tracks);
    void clearTracks();

    // Updates the search box placeholder to "Search <viewName>... (Cmd-F)"
    // so it reflects whatever sidebar item the user currently has open.
    void setSearchPlaceholder(const juce::String& viewName);

    // Clears any text in the search box without firing onTextChange. Used by
    // MainComponent when the user switches sidebar views — a search query
    // typed for one view shouldn't leak into another.
    void clearSearch();

    // Current view mode. Controls whether the leftmost "#" column is present
    // at all (added/removed from the header so it doesn't even appear in the
    // column chooser menu), what value that column shows, and whether the
    // user can drag rows to reorder.
    enum class ViewMode { Library, Artist, Album, Playlist, Podcast };
    void setViewMode(ViewMode mode);

    // Forces the table header to sort by the "#" column (ascending). Called
    // from MainComponent whenever the user navigates into a playlist so it
    // opens in the playlist's natural order by default.
    void applyPlaylistDefaultSort();

    // Moves keyboard focus into the search box, hooking Cmd-F from the main menu.
    void focusSearchBox();

    // Updates analysis fields and hidden flag for a track matched by file path.
    void updateTrack(const TrackInfo& updated);

    // Removes tracks whose files no longer exist on disk.
    void removeOrphanedTracks();

    // Sets the currently playing file so its row is highlighted in green.
    // The highlight persists when the view changes (e.g. switching playlists).
    void setPlayingFile(const juce::File& file);

    // Selects and scrolls to the currently playing row, if it is visible.
    void selectAndScrollToPlayingRow();

    // Scrolls to and selects the row for a given file, if it is visible.
    void scrollToFile(const juce::File& file);

    // Wires up ApplicationProperties so column state is saved/restored per view mode.
    void setAppProperties(juce::ApplicationProperties* props);

    // When true, hidden tracks are shown dimmed; when false they are invisible.
    void setShowHidden(bool show);
    bool showHidden() const { return showHidden_; }

    int numTracks()  const { return static_cast<int>(tracks_.size()); }
    int numVisible() const { return static_cast<int>(filteredTracks_.size()); }

    // When true, suppresses the "Empty" centred label so a higher-level prompt
    // (e.g. "No music folder selected") can be shown instead.
    void setSuppressEmptyLabel(bool suppress) { suppressEmptyLabel_ = suppress; repaint(); }

    // Snapshot of currently visible (filtered) tracks in display order.
    std::vector<TrackInfo> visibleTracks() const;

    // Full unfiltered library as currently held by this component.
    const std::vector<TrackInfo>& allTracks() const { return tracks_; }

    // Called when the user activates a row (double-click or Enter on a non-editable column).
    std::function<void(int rowIndex)> onRowActivated;

    // Called after the user commits an in-place cell edit (Enter or focus-loss).
    // The updated TrackInfo has the new value already saved to the foxp sidecar.
    std::function<void(const TrackInfo&)> onInlineEditCommitted;

    // Called when the hidden state of one or more tracks changes.
    std::function<void()> onLibraryChanged;

    // Called when the user requests analysis on a set of tracks.
    std::function<void(std::vector<TrackInfo>)> onAnalyzeRequested;

    // Called when the user wants to edit a single track's metadata.
    // peerList is a snapshot of all visible tracks at right-click time; peerIndex is the row.
    std::function<void(TrackInfo, std::vector<TrackInfo>, int peerIndex)> onEditRequested;

    // Called when the user wants to edit multiple tracks' metadata at once
    // (all same type: all music or all podcasts).
    std::function<void(std::vector<TrackInfo>)> onMultiEditRequested;

    // Called when the user chooses "Clear Info" for a set of tracks.
    std::function<void(std::vector<TrackInfo>)> onClearInfoRequested;

    // Called when the user chooses "Add to Queue" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAddToQueueRequested;

    // Called when the user chooses "Play Next" from the context menu.
    // Receivers should insert the tracks immediately after the currently
    // playing one rather than appending to the end of the queue.
    std::function<void(std::vector<TrackInfo>)> onPlayNextRequested;

    // Navigation callbacks: go to the artist/album/podcast sidebar item for a track.
    std::function<void(TrackInfo)> onGoToArtistRequested;
    std::function<void(TrackInfo)> onGoToAlbumRequested;
    std::function<void(TrackInfo)> onGoToPodcastRequested;

    // Called when the user chooses "Look up on Apple Music" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAppleMusicLookupRequested;

    // Returns true if the given file has a completed Apple Music lookup that
    // can be undone (i.e. a snapshot was stored before the lookup ran).
    std::function<bool(const juce::File&)> isLookupUndoable;

    // Called when the user chooses "Undo Apple Music lookup" from the context menu.
    // Only fires for single-track selections when isLookupUndoable returned true.
    std::function<void(const TrackInfo&)> onAppleMusicUndoRequested;

    // Called when the user chooses "Look up Album Art" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAlbumArtLookupRequested;

    // Returns true if the given file has an album art lookup that can be undone.
    std::function<bool(const juce::File&)> isArtLookupUndoable;

    // Called when the user chooses "Undo Album art lookup" from the context menu.
    // Only fires for single-track selections when isArtLookupUndoable returned true.
    std::function<void(const TrackInfo&)> onAlbumArtUndoRequested;

    // Called when the user chooses "Look up on Podcast Index" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onPodcastLookupRequested;

    // Called when the user presses Delete in a playlist view. Tracks should be
    // removed from the playlist only, not hidden from the library.
    std::function<void(std::vector<TrackInfo>)> onRemoveFromPlaylistRequested;

    // Called when the user drags rows within the table to reorder them. Only
    // fires in playlist mode when the table is sorted by the "#" column
    // (ascending). `paths` are the dragged file paths in dragged order;
    // `targetIndex` is the insert position in the playlist (0 = top, N = end).
    std::function<void(juce::StringArray paths, int targetIndex)> onReorderRequested;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails&) override;
    void itemDragEnter(const SourceDetails&) override;
    void itemDragMove (const SourceDetails&) override;
    void itemDragExit (const SourceDetails&) override;
    void itemDropped  (const SourceDetails&) override;

    void paintOverChildren(juce::Graphics&) override;

    // Fires onEditRequested / onMultiEditRequested for the current selection,
    // as if the user had chosen "Edit Info" from the right-click menu.
    // Does nothing if the selection is empty or contains mixed types.
    void triggerEditInfoForSelection();

    // Returns true if at least one row is selected.
    bool hasSelection() const { return table_.getSelectedRows().size() > 0; }

    // Returns the file paths of all currently selected rows. Used by
    // MainComponent to remember a view's selection across view switches.
    std::vector<juce::File> selectedFiles() const;

    // Selects rows whose file paths match any in the given set, deselecting
    // all others. Files not present in the current filtered view are ignored.
    void setSelectedFiles(const std::vector<juce::File>& files);

    // Clears all selected rows.
    void deselectAll();

    // juce::Component
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // Fired only on user-driven selection changes; programmatic
    // setSelectedFiles() / deselectAll() do not trigger it.
    std::function<void()> onSelectionChanged;

    // juce::TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked(int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override;
    void deleteKeyPressed(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    void backgroundClicked(const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    juce::String getCellTooltip(int row, int col) override;
    Component* refreshComponentForCell(int rowNumber, int columnId, bool isRowSelected,
                                       Component* existingComponentToUpdate) override;

private:
    void buildTable();
    void applyFilter();
    void applySort();
    void refreshPlayingIndex();
    void updateArtistColumnHeader();
    juce::String cellText(int row, int colId) const;

    static bool isEditableColId(int colId);
    void startCellEdit(int row, int colId);
    void commitCellEdit();
    void cancelCellEdit();

    // Hides (or unhides) all currently selected rows.
    void setHiddenForSelection(bool hidden);

    // Returns all selected row indices as a vector.
    std::vector<int> selectedRows() const;

    // Subclassed look-and-feel for the table header: re-implements
    // drawTableHeaderColumn so the sort-direction arrow gets a thin light
    // grey stroke around its filled triangle (default JUCE only fills it).
    struct HeaderLnF : public juce::LookAndFeel_V4
    {
        void drawTableHeaderColumn(juce::Graphics& g,
                                   juce::TableHeaderComponent& header,
                                   const juce::String& columnName, int columnId,
                                   int width, int height,
                                   bool isMouseOver, bool isMouseDown,
                                   int columnFlags) override;
    };
    HeaderLnF                headerLnF_;

    juce::TextEditor         searchBox_;
    juce::TableListBox       table_;

    std::vector<TrackInfo>   tracks_;          // full library
    std::vector<TrackInfo*>  filteredTracks_;  // pointers into tracks_ for current view
    juce::File               playingFile_;     // currently playing file (ground truth for highlight)
    int                      playingIndex_ { -1 };
    bool                     showHidden_         { false };
    bool                     suppressEmptyLabel_ { false };
    int                      sortColumnId_       { 0 };     // 0 = unsorted
    bool                     sortForwards_ { true };
    ViewMode                 viewMode_     { ViewMode::Library };
    // Y position (in this component's coords) of the drop-indicator line
    // shown during a playlist drag-to-reorder. -1 means no indicator.
    int                      dropIndicatorY_ { -1 };

    // Column state persistence
    juce::ApplicationProperties* appProperties_ { nullptr };
    bool                         restoringColumns_ { false };
    void saveColumnStateForMode(ViewMode mode);
    void restoreColumnStateForMode(ViewMode mode);
    static juce::String columnStateKey(ViewMode mode);

    // TableHeaderComponent::Listener
    void tableColumnsChanged(juce::TableHeaderComponent*) override;
    void tableColumnsResized(juce::TableHeaderComponent*) override;
    void tableSortOrderChanged(juce::TableHeaderComponent*) override {}
    void tableColumnDraggingChanged(juce::TableHeaderComponent*, int) override {}

    // In-place cell editing state.
    int               editingRow_       { -1 };
    int               editingColId_     { -1 };
    int               editingSession_   { 0 };  // incremented on each new edit; guards stale async callbacks
    juce::TextEditor* activeCellEditor_ { nullptr }; // raw ptr, lifetime managed by TableListBox

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryTableComponent)
};

} // namespace FoxPlayer
