#include "AnalysisLogWindow.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

namespace
{
    enum ColId
    {
        colStatus = 1,
        colTitle  = 2,
        colArtist = 3,
        colBpm    = 4,
        colKey    = 5,
    };
}

AnalysisLogComponent::AnalysisLogComponent()
{
    addAndMakeVisible(table_);
    table_.setModel(this);
    table_.setColour(juce::ListBox::backgroundColourId,      Color::tableBackground);
    table_.setColour(juce::TableListBox::backgroundColourId, Color::tableBackground);
    table_.setRowHeight(rowHeight);
    table_.getViewport()->setScrollBarsShown(true, false);

    auto& hdr = table_.getHeader();
    hdr.setColour(juce::TableHeaderComponent::backgroundColourId, Color::headerBackground);
    hdr.setColour(juce::TableHeaderComponent::textColourId,       Color::textSecondary);
    hdr.setColour(juce::TableHeaderComponent::outlineColourId,    Color::border);

    const auto nonSortable = juce::TableHeaderComponent::notSortable;
    hdr.addColumn("Status", colStatus, 100, 80,  140, nonSortable);
    hdr.addColumn("Title",  colTitle,  240, 120, 500, nonSortable);
    hdr.addColumn("Artist", colArtist, 180, 80,  400, nonSortable);
    hdr.addColumn("BPM",    colBpm,     70, 50,  100, nonSortable);
    hdr.addColumn("Key",    colKey,     60, 40,  100, nonSortable);
}

int AnalysisLogComponent::findEntryIndex(const juce::File& file) const
{
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i)
        if (entries_[static_cast<size_t>(i)].file == file)
            return i;
    return -1;
}

void AnalysisLogComponent::scrollToRow(int row)
{
    if (row >= 0)
        table_.scrollToEnsureRowIsOnscreen(row);
}

void AnalysisLogComponent::trackQueued(const TrackInfo& track)
{
    if (findEntryIndex(track.file) >= 0) return;

    Entry e;
    e.file   = track.file;
    e.title  = track.displayTitle();
    e.artist = track.artist;
    e.status = Status::Queued;
    e.bpm    = track.bpm;
    e.key    = track.musicalKey;
    entries_.push_back(std::move(e));

    table_.updateContent();
    table_.repaint();
}

void AnalysisLogComponent::trackStarted(const TrackInfo& track)
{
    const int idx = findEntryIndex(track.file);
    if (idx < 0) return;

    entries_[static_cast<size_t>(idx)].status = Status::Analyzing;
    table_.repaint();
    scrollToRow(idx);
}

void AnalysisLogComponent::trackAnalysed(const TrackInfo& track)
{
    const int idx = findEntryIndex(track.file);
    if (idx < 0) return;

    auto& e = entries_[static_cast<size_t>(idx)];
    e.status = Status::Done;
    e.bpm    = track.bpm;
    e.key    = track.musicalKey;
    table_.repaint();
}

void AnalysisLogComponent::resized()
{
    table_.setBounds(getLocalBounds());
}

void AnalysisLogComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::tableBackground);
}

int AnalysisLogComponent::getNumRows()
{
    return static_cast<int>(entries_.size());
}

void AnalysisLogComponent::paintRowBackground(juce::Graphics& g,
                                               int row, int w, int h,
                                               bool selected)
{
    juce::ignoreUnused(w, h);
    if (selected)
        g.fillAll(Color::tableSelected);
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);
}

void AnalysisLogComponent::paintCell(juce::Graphics& g,
                                      int row, int col,
                                      int w, int h,
                                      bool selected)
{
    juce::ignoreUnused(selected);
    if (row < 0 || row >= static_cast<int>(entries_.size())) return;

    const auto& e = entries_[static_cast<size_t>(row)];
    g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));

    switch (col)
    {
        case colStatus:
        {
            juce::String label;
            juce::Colour colour;
            switch (e.status)
            {
                case Status::Queued:    label = "Queued";       colour = Color::textSecondary; break;
                case Status::Analyzing: label = "Analyzing...";  colour = Color::accent;        break;
                case Status::Done:      label = "Done";          colour = Color::playingGreen;  break;
            }
            g.setColour(colour);
            g.drawText(label, 8, 0, w - 16, h, juce::Justification::centredLeft, true);
            break;
        }

        case colTitle:
            g.setColour(Color::textPrimary);
            g.drawText(e.title, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
            break;

        case colArtist:
            g.setColour(Color::textSecondary);
            g.drawText(e.artist, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
            break;

        case colBpm:
            g.setColour(Color::textPrimary);
            if (e.bpm > 0.0)
                g.drawText(juce::String(e.bpm, 1), 4, 0, w - 8, h, juce::Justification::centredLeft, true);
            break;

        case colKey:
            g.setColour(Color::textPrimary);
            g.drawText(e.key, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
            break;

        default: break;
    }
}

// ----------------------------------------------------------------------------
// AnalysisLogWindow
// ----------------------------------------------------------------------------

AnalysisLogWindow::AnalysisLogWindow()
    : juce::DocumentWindow("Analysis Log",
                           Color::background,
                           juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(400, 200, 2000, 2000);

    auto* c = new AnalysisLogComponent();
    logComponent_ = c;
    setContentOwned(c, true);

    centreWithSize(700, 420);
    setVisible(false);
}

void AnalysisLogWindow::closeButtonPressed()
{
    setVisible(false);
}

AnalysisLogComponent& AnalysisLogWindow::log()
{
    jassert(logComponent_ != nullptr);
    return *logComponent_;
}

} // namespace FoxPlayer
