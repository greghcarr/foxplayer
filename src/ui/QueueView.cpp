#include "QueueView.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

QueueView::QueueView()
{
    header_.setText("Play Queue", juce::dontSendNotification);
    header_.setFont(juce::Font(14.0f, juce::Font::bold));
    header_.setColour(juce::Label::textColourId,       Color::textPrimary);
    header_.setColour(juce::Label::backgroundColourId, Color::headerBackground);
    header_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(header_);

    list_.setModel(this);
    list_.setColour(juce::ListBox::backgroundColourId, Color::tableBackground);
    list_.setRowHeight(rowHeight);
    list_.setMultipleSelectionEnabled(true);
    list_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,      Color::scrollbarThumb);
    list_.getVerticalScrollBar().setColour(juce::ScrollBar::backgroundColourId, Color::tableBackground);
    list_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,      Color::tableBackground);
    addAndMakeVisible(list_);
}

void QueueView::refresh(const PlayQueue& queue)
{
    const auto& newItems = queue.tracks();
    const int   newCount = static_cast<int>(newItems.size());
    const int   oldCount = static_cast<int>(items_.size());

    // Drop the row selection when it's no longer meaningful: row count changed
    // (insert / remove) or any selected row now points at a different file
    // (shuffle / reorder). A pure index change (next / prev) leaves files at
    // selected rows unchanged, so the user's selection survives that.
    bool selectionStillValid = (newCount == oldCount);
    if (selectionStillValid)
    {
        const auto& sel = list_.getSelectedRows();
        for (int i = 0; i < sel.size() && selectionStillValid; ++i)
        {
            const int row = sel[i];
            if (row < 0 || row >= oldCount || row >= newCount
                || items_[(size_t) row].file != newItems[(size_t) row].file)
                selectionStillValid = false;
        }
    }

    items_        = newItems;
    playingIndex_ = queue.currentIndex();

    if (! selectionStillValid)
        list_.setSelectedRows(juce::SparseSet<int>{}, juce::dontSendNotification);

    list_.updateContent();
    list_.repaint();

    if (playingIndex_ >= 0)
        list_.scrollToEnsureRowIsOnscreen(playingIndex_);
}

void QueueView::centerPlayingRow()
{
    if (playingIndex_ < 0) return;

    auto* viewport = list_.getViewport();
    if (viewport == nullptr) return;

    const int rowH      = list_.getRowHeight();
    const int rowY      = playingIndex_ * rowH;
    const int viewportH = viewport->getViewHeight();

    // Centre the row vertically. juce::Viewport::setViewPosition clamps to
    // valid bounds, so when the playing row is near the top or bottom the
    // scroll stops at the edge instead of overshooting.
    const int desiredY = rowY - (viewportH - rowH) / 2;
    viewport->setViewPosition(0, juce::jmax(0, desiredY));
}

void QueueView::paint(juce::Graphics& g)
{
    g.fillAll(Color::tableBackground);
    g.setColour(Color::border);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
}

void QueueView::paintOverChildren(juce::Graphics& g)
{
    if (dropInsertIndex_ < 0) return;

    const int rowH = list_.getRowHeight();
    juce::Rectangle<int> rb;
    if (dropInsertIndex_ < static_cast<int>(items_.size()))
        rb = list_.getRowPosition(dropInsertIndex_, true);
    else if (! items_.empty())
    {
        const auto last = list_.getRowPosition(static_cast<int>(items_.size()) - 1, true);
        rb = last.withY(last.getBottom());
    }
    else
        rb = juce::Rectangle<int>(0, 0, list_.getWidth(), rowH);

    const int lineY = rb.getY() + list_.getY();

    g.setColour(Color::accent);
    g.fillRect(juce::Rectangle<int>(list_.getX() + 4, lineY - 1,
                                    list_.getWidth() - 8, 2));
}

void QueueView::resized()
{
    auto bounds = getLocalBounds();
    header_.setBounds(bounds.removeFromTop(30).reduced(8, 0));
    list_.setBounds(bounds);
    list_.updateContent(); // ghost row count depends on list height
}

int QueueView::getNumRows()
{
    const int numItems = static_cast<int>(items_.size());
    const int listH    = list_.getHeight();
    if (listH <= 0) return numItems;
    // Pad with ghost rows so the alternating stripe pattern extends into the
    // empty space below the last real item, but cap at the number of complete
    // rows that fit in the visible area — going one over would push content
    // height past the viewport and let the user scroll an empty queue.
    return juce::jmax(numItems, listH / rowHeight);
}

void QueueView::paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0) return;

    // Ghost rows below the last item — stripe pattern only, no content.
    if (row >= static_cast<int>(items_.size()))
    {
        if (row % 2 != 0)
            g.fillAll(Color::tableRowAlt);
        return;
    }

    const bool isPlaying = (row == playingIndex_);

    if (selected)
        g.fillAll(Color::tableSelected);
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);

    const auto& t = items_[static_cast<size_t>(row)];
    g.setColour(isPlaying ? Color::playingHighlight : Color::textPrimary);
    g.setFont(juce::Font(14.0f));

    const juce::String line = t.displayTitle() +
                               (t.artist.isNotEmpty() ? " - " + t.artist : "");
    g.drawText(line, 8, 0, w - 16, h, juce::Justification::centredLeft, true);
}

void QueueView::listBoxItemClicked(int row, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(items_.size())) return;
    if (! e.mods.isPopupMenu()) return;

    // If the right-clicked row isn't part of an existing multi-selection,
    // replace the selection with just that row. Otherwise the menu acts on
    // the whole multi-selection the user already built.
    if (! list_.isRowSelected(row))
        list_.selectRow(row, true, true);

    const auto& sel = list_.getSelectedRows();
    std::vector<int> selectedRows;
    for (int i = 0; i < sel.size(); ++i)
        selectedRows.push_back(sel[i]);

    bool hasRemovable = false;
    for (int r : selectedRows)
        if (r != playingIndex_) { hasRemovable = true; break; }

    const juce::String label = (selectedRows.size() > 1)
                                   ? "Remove from Queue (" + juce::String((int) selectedRows.size()) + ")"
                                   : "Remove from Queue";

    juce::PopupMenu menu;
    menu.addItem(1, label, hasRemovable);

    juce::Component::SafePointer<QueueView> safe(this);
    const auto anchor = juce::Rectangle<int>(e.getScreenPosition(), e.getScreenPosition()).expanded(1);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetScreenArea(anchor),
        [safe, rows = std::move(selectedRows)](int result) {
            if (result == 1 && safe && safe->onRemoveTracks)
                safe->onRemoveTracks(rows);
        });
}

void QueueView::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int>(items_.size()))
        if (onRowActivated) onRowActivated(row);
}

bool QueueView::isInterestedInDragSource(const SourceDetails& details)
{
    // Accept any text-payload drag (library uses newline-separated paths).
    return details.description.isString();
}

int QueueView::insertIndexForPosition(juce::Point<int> posInThis) const
{
    const int n = static_cast<int>(items_.size());
    if (n == 0) return 0;

    // Convert to ListBox-local coordinates.
    const auto posInList = posInThis - list_.getPosition();
    const int row = list_.getRowContainingPosition(posInList.x, posInList.y);
    if (row < 0)
        return n;  // below all rows -> append

    const auto rb = list_.getRowPosition(row, true);
    const int relY = posInList.y - rb.getY();
    return (relY < rb.getHeight() / 2) ? row : row + 1;
}

void QueueView::itemDragEnter(const SourceDetails& details)
{
    dropInsertIndex_ = insertIndexForPosition(details.localPosition.toInt());
    repaint();
}

void QueueView::itemDragMove(const SourceDetails& details)
{
    const int idx = insertIndexForPosition(details.localPosition.toInt());
    if (idx != dropInsertIndex_)
    {
        dropInsertIndex_ = idx;
        repaint();
    }
}

void QueueView::itemDragExit(const SourceDetails&)
{
    if (dropInsertIndex_ != -1)
    {
        dropInsertIndex_ = -1;
        repaint();
    }
}

void QueueView::itemDropped(const SourceDetails& details)
{
    const int idx = insertIndexForPosition(details.localPosition.toInt());
    dropInsertIndex_ = -1;
    repaint();

    juce::StringArray paths;
    paths.addTokens(details.description.toString(), "\n", "");
    paths.removeEmptyStrings();
    if (paths.isEmpty()) return;

    if (onTracksDropped) onTracksDropped(paths, idx);
}

} // namespace FoxPlayer
