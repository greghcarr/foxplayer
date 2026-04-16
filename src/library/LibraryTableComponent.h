#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class LibraryTableComponent : public juce::Component,
                               public juce::TableListBoxModel
{
public:
    LibraryTableComponent();
    ~LibraryTableComponent() override = default;

    void setTracks(const std::vector<TrackInfo>& tracks);
    void appendTracks(const std::vector<TrackInfo>& tracks);
    void clearTracks();

    // Updates the search box placeholder to "Search <viewName>..." so it
    // reflects whatever sidebar item the user currently has open.
    void setSearchPlaceholder(const juce::String& viewName);

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

    // Called when the user chooses "Add to Queue" from the context menu.
    std::function<void(std::vector<TrackInfo>)> onAddToQueueRequested;

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
    bool                     showHidden_   { false };
    int                      sortColumnId_ { 0 };     // 0 = unsorted
    bool                     sortForwards_ { true };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryTableComponent)
};

} // namespace FoxPlayer
