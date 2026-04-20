#include "QueueView.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

QueueView::QueueView()
{
    header_.setText("Up Next", juce::dontSendNotification);
    header_.setFont(juce::Font(14.0f, juce::Font::bold));
    header_.setColour(juce::Label::textColourId,       Color::textPrimary);
    header_.setColour(juce::Label::backgroundColourId, Color::headerBackground);
    header_.setJustificationType(juce::Justification::centredLeft);
    addAndMakeVisible(header_);

    list_.setModel(this);
    list_.setColour(juce::ListBox::backgroundColourId, Color::tableBackground);
    list_.setRowHeight(rowHeight);
    addAndMakeVisible(list_);
}

void QueueView::refresh(const PlayQueue& queue)
{
    items_        = queue.tracks();
    playingIndex_ = queue.currentIndex();
    list_.updateContent();
    list_.repaint();

    if (playingIndex_ >= 0)
        list_.scrollToEnsureRowIsOnscreen(playingIndex_);
}

void QueueView::paint(juce::Graphics& g)
{
    g.fillAll(Color::tableBackground);
    g.setColour(Color::border);
    g.drawVerticalLine(0, 0.0f, static_cast<float>(getHeight()));
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
    // Return enough rows to fill the visible area so the stripe pattern extends
    // into the empty space below the last real item.
    return juce::jmax(numItems, listH / rowHeight + 1);
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
    else if (isPlaying)
        g.fillAll(Color::tableSelected.withAlpha(0.5f));
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);

    const auto& t = items_[static_cast<size_t>(row)];
    g.setColour(isPlaying ? Color::playingHighlight : Color::textPrimary);
    g.setFont(juce::Font(14.0f));

    const juce::String line = t.displayTitle() +
                               (t.artist.isNotEmpty() ? " - " + t.artist : "");
    g.drawText(line, 8, 0, w - 16, h, juce::Justification::centredLeft, true);
}

void QueueView::listBoxItemDoubleClicked(int row, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int>(items_.size()))
        if (onRowActivated) onRowActivated(row);
}

} // namespace FoxPlayer
