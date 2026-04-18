#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class LibraryTableComponent : public juce::Component,
                               public juce::TableListBoxModel,
                               public juce::DragAndDropTarget
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

    // Called when the user activates a row (double-click or Enter).
    std::function<void(int rowIndex)> onRowActivated;

    // Called when the hidden state of one or more tracks changes.
    std::function<void()> onLibraryChanged;

    // Called when the user requests analysis on a set of tracks.
    std::function<void(std::vector<TrackInfo>)> onAnalyzeRequested;

    // Called when the user wants to edit a single track's metadata.
    std::function<void(TrackInfo)> onEditRequested;

    // Called when the user chooses "Clear Song Info" for a set of tracks.
    std::function<void(std::vector<TrackInfo>)> onClearInfoRequested;

    // Called when the user chooses "Add to Queue" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAddToQueueRequested;

    // Navigation callbacks: go to the artist/album/podcast sidebar item for a track.
    std::function<void(TrackInfo)> onGoToArtistRequested;
    std::function<void(TrackInfo)> onGoToAlbumRequested;
    std::function<void(TrackInfo)> onGoToPodcastRequested;

    // Called when the user chooses "Look up on Apple Music" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAppleMusicLookupRequested;

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

    // juce::Component
    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseDrag(const juce::MouseEvent& e) override;

    // juce::TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selected) override;
    void cellClicked(int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked(int row, int col, const juce::MouseEvent&) override;
    void deleteKeyPressed(int lastRowSelected) override;
    void returnKeyPressed(int lastRowSelected) override;
    void backgroundClicked(const juce::MouseEvent&) override;
    void sortOrderChanged(int newSortColumnId, bool isForwards) override;
    juce::String getCellTooltip(int row, int col) override;

private:
    void buildTable();
    void applyFilter();
    void applySort();
    void refreshPlayingIndex();
    void updateArtistColumnHeader();
    juce::String cellText(int row, int colId) const;

    // Hides (or unhides) all currently selected rows.
    void setHiddenForSelection(bool hidden);

    // Returns all selected row indices as a vector.
    std::vector<int> selectedRows() const;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryTableComponent)
};

} // namespace FoxPlayer
