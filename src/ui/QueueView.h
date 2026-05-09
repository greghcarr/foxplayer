#pragma once

#include "audio/PlayQueue.h"
#include <JuceHeader.h>
#include <functional>

namespace Stylus
{

// Panel that shows the current play queue as a simple list.
// Slides in from the right edge of MainComponent.
class QueueView : public juce::Component,
                  public juce::ListBoxModel,
                  public juce::DragAndDropTarget,
                  public juce::KeyListener
{
public:
    QueueView();
    ~QueueView() override = default;

    void refresh(const PlayQueue& queue);

    // Scrolls so the currently playing row is vertically centred in the
    // visible area; clamped to the valid scroll range so no rows are hidden.
    void centerPlayingRow();

    // juce::Component
    void paint(juce::Graphics& g) override;
    void paintOverChildren(juce::Graphics& g) override;
    void resized() override;

    // juce::ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;
    void deleteKeyPressed(int lastRowSelected) override;

    // Clears the selection without firing onSelectionChanged. Used by
    // MainComponent to enforce mutually exclusive selection between the
    // library table and the queue.
    void deselectAll();

    std::function<void(int queueIndex)> onRowActivated;

    // Fired only on user-driven selection changes (not programmatic clears
    // via deselectAll). MainComponent uses this to clear the library table's
    // selection so the two never highlight rows simultaneously.
    std::function<void()> onSelectionChanged;

    // Fired when the user picks "Remove from Queue" for one or more rows.
    // Indices are in live-queue order; the currently playing index, if
    // included, will be silently skipped by the receiver.
    std::function<void(std::vector<int> queueIndices)> onRemoveTracks;

    // Fired when one or more library tracks are dragged into the queue panel.
    // paths is the newline-separated payload from LibraryTableComponent.
    // insertIndex is the position in the live queue at which to insert
    // (0 = top, items_.size() = append). Negative => append.
    std::function<void(juce::StringArray paths, int insertIndex)> onTracksDropped;

    // Cross-pane keyboard nav callbacks. Fired from the inner ListBox's
    // KeyListener so they run before the list's built-in arrow / Tab
    // handling.
    std::function<void()> onMoveFocusToLibrary;
    std::function<void()> onMoveFocusToSidebar;

    // Pulls JUCE keyboard focus onto the inner ListBox so arrows / Home /
    // End drive its selection. Used to implement cross-pane keyboard nav
    // (Right / Tab from the library or sidebar Shift-Tab).
    void focusList();

    // juce::KeyListener
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    juce::ListBox list_;
    juce::Label   header_;
    bool          suppressSelectionCallback_ { false };

    // Row to select after the next refresh() that follows a delete. Lets the
    // selection slide forward to whatever track moved into the deleted row's
    // slot, or back to the last row if the deletion was at the tail.
    // -1 = no pending.
    int           pendingDeleteTarget_ { -1 };

    std::vector<TrackInfo> items_;
    int playingIndex_ { -1 };
    // -1 when no library drag is active. Otherwise: the live-queue index at
    // which dropped tracks would be inserted (0 = top, items_.size() = end).
    int dropInsertIndex_ { -1 };

    // Computes the insertion index for a drag at the given QueueView-local
    // position. Result is clamped to [0, items_.size()].
    int insertIndexForPosition(juce::Point<int> posInThis) const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QueueView)
};

} // namespace Stylus
