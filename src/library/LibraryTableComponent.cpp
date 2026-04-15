#include "LibraryTableComponent.h"
#include "Constants.h"
#include "audio/FoxpFile.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int searchBarHeight = 30;

LibraryTableComponent::LibraryTableComponent()
{
    // Search box
    searchBox_.setTextToShowWhenEmpty("Search library...", Color::textDim);
    searchBox_.setFont(juce::Font(13.0f));
    searchBox_.setColour(juce::TextEditor::backgroundColourId, Color::headerBackground);
    searchBox_.setColour(juce::TextEditor::textColourId,       Color::textPrimary);
    searchBox_.setColour(juce::TextEditor::outlineColourId,    Color::border);
    searchBox_.setColour(juce::TextEditor::focusedOutlineColourId, Color::accent);
    searchBox_.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(searchBox_);

    addAndMakeVisible(table_);
    table_.setModel(this);
    table_.addMouseListener(this, true);
    table_.setColour(juce::ListBox::backgroundColourId,      Color::tableBackground);
    table_.setColour(juce::TableListBox::backgroundColourId, Color::tableBackground);
    table_.setRowHeight(rowHeight);
    table_.setMultipleSelectionEnabled(true);
    table_.getViewport()->setScrollBarsShown(true, false);

    buildTable();
    applyFilter();
}

void LibraryTableComponent::buildTable()
{
    auto& hdr = table_.getHeader();
    hdr.setColour(juce::TableHeaderComponent::backgroundColourId, Color::headerBackground);
    hdr.setColour(juce::TableHeaderComponent::textColourId,       Color::textSecondary);
    hdr.setColour(juce::TableHeaderComponent::outlineColourId,    Color::border);

    hdr.addColumn("Title",  colIdTitle,  colWidthTitle,  80, 600,
                  juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("Artist", colIdArtist, colWidthArtist, 60, 400,
                  juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("Album",  colIdAlbum,  colWidthAlbum,  60, 400,
                  juce::TableHeaderComponent::defaultFlags);
    hdr.addColumn("Time",   colIdTime,   colWidthTime,   40, 80,
                  juce::TableHeaderComponent::notSortable);
    hdr.addColumn("BPM",    colIdBpm,    colWidthBpm,    40, 80,
                  juce::TableHeaderComponent::notSortable);
    hdr.addColumn("Key",    colIdKey,    colWidthKey,    40, 70,
                  juce::TableHeaderComponent::notSortable);
    hdr.addColumn("Plays",  colIdPlays,  colWidthPlays,  40, 80,
                  juce::TableHeaderComponent::notSortable);
}

void LibraryTableComponent::applyFilter()
{
    const juce::String query = searchBox_.getText().trim().toLowerCase();

    filteredTracks_.clear();
    filteredTracks_.reserve(tracks_.size());

    for (auto& t : tracks_)
    {
        // Skip hidden tracks entirely when showHidden_ is false.
        if (t.hidden && !showHidden_)
            continue;

        if (query.isEmpty()
            || t.displayTitle().toLowerCase().contains(query)
            || t.artist.toLowerCase().contains(query)
            || t.album.toLowerCase().contains(query))
        {
            filteredTracks_.push_back(&t);
        }
    }

    playingIndex_ = -1;
    table_.updateContent();
    table_.repaint();
}

void LibraryTableComponent::setTracks(const std::vector<TrackInfo>& tracks)
{
    tracks_ = tracks;
    applyFilter();
}

void LibraryTableComponent::appendTracks(const std::vector<TrackInfo>& tracks)
{
    tracks_.insert(tracks_.end(), tracks.begin(), tracks.end());
    applyFilter();
}

void LibraryTableComponent::updateTrack(const TrackInfo& updated)
{
    for (auto& t : tracks_)
    {
        if (t.file == updated.file)
        {
            t.title       = updated.title;
            t.artist      = updated.artist;
            t.album       = updated.album;
            t.genre       = updated.genre;
            t.year        = updated.year;
            t.trackNumber = updated.trackNumber;
            t.bpm         = updated.bpm;
            t.musicalKey  = updated.musicalKey;
            t.lufs        = updated.lufs;
            t.hidden      = updated.hidden;
            t.playCount   = updated.playCount;
            applyFilter();
            return;
        }
    }
}

void LibraryTableComponent::removeOrphanedTracks()
{
    const size_t before = tracks_.size();

    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(),
                       [](const TrackInfo& t) { return !t.file.existsAsFile(); }),
        tracks_.end());

    if (tracks_.size() != before)
        applyFilter();
}

std::vector<TrackInfo> LibraryTableComponent::visibleTracks() const
{
    std::vector<TrackInfo> result;
    result.reserve(filteredTracks_.size());
    for (const auto* p : filteredTracks_)
        result.push_back(*p);
    return result;
}

void LibraryTableComponent::clearTracks()
{
    tracks_.clear();
    filteredTracks_.clear();
    playingIndex_ = -1;
    table_.updateContent();
    table_.repaint();
}

void LibraryTableComponent::setPlayingIndex(int index)
{
    playingIndex_ = index;
    table_.repaint();
}

void LibraryTableComponent::setShowHidden(bool show)
{
    showHidden_ = show;
    applyFilter();
}

void LibraryTableComponent::resized()
{
    auto bounds = getLocalBounds();
    searchBox_.setBounds(bounds.removeFromTop(searchBarHeight).reduced(0, 2));
    table_.setBounds(bounds);
}

void LibraryTableComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::tableBackground);
}

int LibraryTableComponent::getNumRows()
{
    return static_cast<int>(filteredTracks_.size());
}

void LibraryTableComponent::paintRowBackground(juce::Graphics& g,
                                                int row, int w, int h,
                                                bool selected)
{
    juce::ignoreUnused(w, h);
    if (selected)
        g.fillAll(Color::tableSelected);
    else if (row == playingIndex_)
        g.fillAll(Color::tableSelected.withAlpha(0.5f));
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);
}

void LibraryTableComponent::paintCell(juce::Graphics& g,
                                       int row, int col,
                                       int w, int h,
                                       bool selected)
{
    juce::ignoreUnused(selected);
    if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) return;

    const auto& t = *filteredTracks_[static_cast<size_t>(row)];
    const bool isPlaying = (row == playingIndex_);

    if (isPlaying)
        g.setColour(Color::playingGreen);
    else if (t.hidden)
        g.setColour(Color::textDim);
    else
        g.setColour(Color::textPrimary);

    g.setFont(juce::Font(13.0f));

    const juce::String text = cellText(row, col);
    g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
}

juce::String LibraryTableComponent::cellText(int row, int colId) const
{
    const auto& t = *filteredTracks_[static_cast<size_t>(row)];
    switch (colId)
    {
        case colIdTitle:  return t.displayTitle();
        case colIdArtist: return t.artist;
        case colIdAlbum:  return t.album;
        case colIdTime:   return t.formattedDuration();
        case colIdBpm:    return t.bpm > 0.0 ? juce::String(t.bpm, 1) : juce::String();
        case colIdKey:    return t.musicalKey;
        case colIdPlays:  return t.playCount > 0 ? juce::String(t.playCount) : juce::String();
        default:          return {};
    }
}

std::vector<int> LibraryTableComponent::selectedRows() const
{
    std::vector<int> rows;
    const auto& sparse = table_.getSelectedRows();
    for (int i = 0; i < sparse.size(); ++i)
        rows.push_back(sparse[i]);
    return rows;
}

void LibraryTableComponent::setHiddenForSelection(bool hidden)
{
    const auto rows = selectedRows();
    if (rows.empty()) return;

    bool changed = false;
    for (int row : rows)
    {
        if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) continue;
        auto* track = filteredTracks_[static_cast<size_t>(row)];
        if (track->hidden != hidden)
        {
            track->hidden = hidden;
            FoxpFile::save(*track);
            changed = true;
        }
    }

    if (changed)
    {
        applyFilter();
        if (onLibraryChanged) onLibraryChanged();
    }
}

void LibraryTableComponent::cellClicked(int row, int /*col*/, const juce::MouseEvent& e)
{
    if (!e.mods.isPopupMenu()) return;
    if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) return;

    // Ensure the right-clicked row is selected if it isn't already.
    if (!table_.isRowSelected(row))
        table_.selectRow(row);

    const bool anyHidden   = [&] {
        for (int r : selectedRows())
            if (r >= 0 && r < static_cast<int>(filteredTracks_.size())
                && filteredTracks_[static_cast<size_t>(r)]->hidden)
                return true;
        return false;
    }();

    const bool anyVisible  = [&] {
        for (int r : selectedRows())
            if (r >= 0 && r < static_cast<int>(filteredTracks_.size())
                && !filteredTracks_[static_cast<size_t>(r)]->hidden)
                return true;
        return false;
    }();

    const juce::File file = filteredTracks_[static_cast<size_t>(row)]->file;

    // Snapshot selected tracks for analysis before the menu closes.
    std::vector<TrackInfo> selectedTracks;
    for (int r : selectedRows())
        if (r >= 0 && r < static_cast<int>(filteredTracks_.size()))
            selectedTracks.push_back(*filteredTracks_[static_cast<size_t>(r)]);

    const TrackInfo clickedTrack = *filteredTracks_[static_cast<size_t>(row)];

    juce::PopupMenu menu;
    menu.addItem(1, "Show in Finder");
    menu.addSeparator();
    if (anyVisible)  menu.addItem(2, "Hide from Library");
    if (anyHidden)   menu.addItem(3, "Unhide from Library");
    menu.addSeparator();
    menu.addItem(4, "Analyze for Key and BPM");
    menu.addItem(5, "Edit Song Info");

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetScreenArea(
        juce::Rectangle<int>().withPosition(e.getScreenPosition())),
        [this, file, selectedTracks, clickedTrack](int result) {
            if (result == 1)
                file.revealToUser();
            else if (result == 2)
                setHiddenForSelection(true);
            else if (result == 3)
                setHiddenForSelection(false);
            else if (result == 4)
                { if (onAnalyzeRequested) onAnalyzeRequested(selectedTracks); }
            else if (result == 5)
                { if (onEditRequested) onEditRequested(clickedTrack); }
        });
}

void LibraryTableComponent::cellDoubleClicked(int row, int /*col*/, const juce::MouseEvent&)
{
    if (row >= 0 && row < static_cast<int>(filteredTracks_.size()))
        if (onRowActivated) onRowActivated(row);
}

void LibraryTableComponent::deleteKeyPressed(int /*lastRowSelected*/)
{
    setHiddenForSelection(true);
}

void LibraryTableComponent::returnKeyPressed(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(filteredTracks_.size()))
        if (onRowActivated) onRowActivated(lastRowSelected);
}

void LibraryTableComponent::mouseDrag(const juce::MouseEvent& e)
{
    if (e.getDistanceFromDragStart() < 8) return;

    const auto rows = selectedRows();
    if (rows.empty()) return;

    juce::StringArray paths;
    for (int r : rows)
        if (r >= 0 && r < static_cast<int>(filteredTracks_.size()))
            paths.add(filteredTracks_[static_cast<size_t>(r)]->file.getFullPathName());

    if (paths.isEmpty()) return;

    // Build a compact drag image showing track titles (up to 3, then "+ N more").
    constexpr int lineH  = 22;
    constexpr int padX   = 10;
    constexpr int padY   = 6;
    constexpr int imgW   = 220;

    juce::StringArray labels;
    for (int r : rows)
    {
        if (r >= 0 && r < static_cast<int>(filteredTracks_.size()))
            labels.add(filteredTracks_[static_cast<size_t>(r)]->displayTitle());
        if (labels.size() == 3 && rows.size() > 3)
        {
            labels.add("+ " + juce::String(static_cast<int>(rows.size()) - 3) + " more");
            break;
        }
    }

    const int imgH = padY * 2 + lineH * labels.size();
    juce::Image dragImg(juce::Image::ARGB, imgW, imgH, true);
    {
        juce::Graphics g(dragImg);
        g.setColour(juce::Colour(0xe0242424));
        g.fillRoundedRectangle(dragImg.getBounds().toFloat(), 5.0f);
        g.setColour(juce::Colour(0xff4a9eff));
        g.drawRoundedRectangle(dragImg.getBounds().toFloat().reduced(0.5f), 5.0f, 1.0f);
        g.setColour(juce::Colours::white);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        for (int i = 0; i < labels.size(); ++i)
            g.drawText(labels[i], padX, padY + i * lineH, imgW - padX * 2, lineH,
                       juce::Justification::centredLeft, true);
    }

    if (auto* container = juce::DragAndDropContainer::findParentDragContainerFor(this))
    {
        const juce::Point<int> imageOffset(0, 0);
        container->startDragging(paths.joinIntoString("\n"), this,
                                 juce::ScaledImage(dragImg), false, &imageOffset);
    }
}

void LibraryTableComponent::backgroundClicked(const juce::MouseEvent&)
{
    table_.deselectAllRows();
}

juce::String LibraryTableComponent::getCellTooltip(int row, int col)
{
    juce::ignoreUnused(col);
    if (row >= 0 && row < static_cast<int>(filteredTracks_.size()))
        return filteredTracks_[static_cast<size_t>(row)]->file.getFullPathName();
    return {};
}

} // namespace FoxPlayer
