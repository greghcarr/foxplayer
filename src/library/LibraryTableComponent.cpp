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
    searchBox_.setTextToShowWhenEmpty("Search All Music... (Cmd-F)", Color::textDim);
    searchBox_.setFont(juce::Font(14.0f));
    searchBox_.setColour(juce::TextEditor::backgroundColourId, Color::headerBackground);
    searchBox_.setColour(juce::TextEditor::textColourId,       Color::textPrimary);
    searchBox_.setColour(juce::TextEditor::outlineColourId,    Color::border);
    searchBox_.setColour(juce::TextEditor::focusedOutlineColourId, Color::accent);
    searchBox_.setJustification(juce::Justification::centredLeft);
    searchBox_.onTextChange = [this] { applyFilter(); };
    addAndMakeVisible(searchBox_);

    addAndMakeVisible(table_);
    table_.setModel(this);
    table_.addMouseListener(this, true);
    // Transparent so LibraryTableComponent::paint() can draw the full-height stripes.
    table_.setColour(juce::ListBox::backgroundColourId,      juce::Colours::transparentBlack);
    table_.setColour(juce::TableListBox::backgroundColourId, juce::Colours::transparentBlack);
    table_.setRowHeight(rowHeight);
    table_.setMultipleSelectionEnabled(true);
    table_.getViewport()->setScrollBarsShown(true, true);

    // Give the vertical scrollbar a solid backdrop that matches the sidebar
    // scrollbar's look. Without this the scrollbar's transparent track lets
    // the alternating row colours show through, which creates a jarring
    // static-striped effect while the rows beneath scroll past. Both the
    // background and track colours need overriding because the app-wide
    // LookAndFeel sets both to transparent.
    auto& tableVsb = table_.getVerticalScrollBar();
    tableVsb.setColour(juce::ScrollBar::backgroundColourId, Color::headerBackground);
    tableVsb.setColour(juce::ScrollBar::trackColourId,      Color::headerBackground);
    tableVsb.setColour(juce::ScrollBar::thumbColourId,      Color::scrollbarThumb);

    auto& tableHsb = table_.getHorizontalScrollBar();
    tableHsb.setColour(juce::ScrollBar::backgroundColourId, Color::headerBackground);
    tableHsb.setColour(juce::ScrollBar::trackColourId,      Color::headerBackground);
    tableHsb.setColour(juce::ScrollBar::thumbColourId,      Color::scrollbarThumb);

    buildTable();
    table_.getHeader().addListener(this);
    applyFilter();
}

void LibraryTableComponent::buildTable()
{
    auto& hdr = table_.getHeader();
    hdr.setColour(juce::TableHeaderComponent::backgroundColourId, Color::headerBackground);
    hdr.setColour(juce::TableHeaderComponent::textColourId,       Color::textSecondary);
    hdr.setColour(juce::TableHeaderComponent::outlineColourId,    Color::border);

    const auto flags = juce::TableHeaderComponent::defaultFlags;
    // Title is always visible; stripping appearsOnColumnMenu keeps the user
    // from being able to uncheck it in the header's column chooser menu.
    const auto titleFlags = flags & ~juce::TableHeaderComponent::appearsOnColumnMenu;
    // "#" column is added/removed by setViewMode depending on context. Not
    // added here so it doesn't appear in the column menu in Library/Artist
    // views where no sane "#" value exists.
    hdr.addColumn("Title",  colIdTitle,  colWidthTitle,  80, 600, titleFlags);
    hdr.addColumn("Artist", colIdArtist, colWidthArtist, 60, 400, flags);
    hdr.addColumn("Album",  colIdAlbum,  colWidthAlbum,  60, 400, flags);
    hdr.addColumn("Genre",  colIdGenre,  colWidthGenre,  60, 300, flags);
    hdr.addColumn("Time",   colIdTime,   colWidthTime,   40, 80,  flags);
    hdr.addColumn("BPM",    colIdBpm,    colWidthBpm,    40, 80,  flags);
    hdr.addColumn("Key",    colIdKey,    colWidthKey,    40, 70,  flags);
    hdr.addColumn("Plays",  colIdPlays,  colWidthPlays,  40, 80,  flags);
    hdr.addColumn("Format", colIdFormat, colWidthFormat, 40, 100, flags);
}

juce::String LibraryTableComponent::columnStateKey(ViewMode mode)
{
    switch (mode)
    {
        case ViewMode::Library:  return "tableColumns.Library";
        case ViewMode::Artist:   return "tableColumns.Artist";
        case ViewMode::Album:    return "tableColumns.Album";
        case ViewMode::Playlist: return "tableColumns.Playlist";
        case ViewMode::Podcast:  return "tableColumns.Podcast";
    }
    return "tableColumns.Library";
}

void LibraryTableComponent::saveColumnStateForMode(ViewMode mode)
{
    if (!appProperties_ || restoringColumns_) return;
    if (auto* s = appProperties_->getUserSettings())
        s->setValue(columnStateKey(mode), table_.getHeader().toString());
}

void LibraryTableComponent::restoreColumnStateForMode(ViewMode mode)
{
    if (!appProperties_) return;
    if (auto* s = appProperties_->getUserSettings())
    {
        const juce::String saved = s->getValue(columnStateKey(mode));
        if (saved.isNotEmpty())
        {
            restoringColumns_ = true;
            table_.getHeader().restoreFromString(saved);
            restoringColumns_ = false;
        }
    }
}

void LibraryTableComponent::setAppProperties(juce::ApplicationProperties* props)
{
    appProperties_ = props;
    restoreColumnStateForMode(viewMode_);
}

void LibraryTableComponent::tableColumnsChanged(juce::TableHeaderComponent*)
{
    saveColumnStateForMode(viewMode_);
}

void LibraryTableComponent::tableColumnsResized(juce::TableHeaderComponent*)
{
    saveColumnStateForMode(viewMode_);
}

void LibraryTableComponent::setViewMode(ViewMode mode)
{
    if (viewMode_ == mode) return;
    saveColumnStateForMode(viewMode_);
    viewMode_ = mode;

    auto& hdr = table_.getHeader();
    const bool wantIndexColumn = (mode == ViewMode::Album || mode == ViewMode::Playlist || mode == ViewMode::Podcast);
    const bool haveIndexColumn = (hdr.getIndexOfColumnId(colIdRow, false) >= 0);

    if (wantIndexColumn && ! haveIndexColumn)
    {
        // Insert "#" as the first column so it lives to the left of Title.
        hdr.addColumn("#", colIdRow, colWidthRow, 30, 80,
                      juce::TableHeaderComponent::defaultFlags, 0);
    }
    else if (! wantIndexColumn && haveIndexColumn)
    {
        hdr.removeColumn(colIdRow);
    }

    // If the user was sorting by "#" and the column just disappeared, drop
    // the sort so rows fall back to their setTracks() order.
    if (! wantIndexColumn && sortColumnId_ == colIdRow)
    {
        sortColumnId_ = 0;
        applyFilter();
    }

    restoreColumnStateForMode(mode);
    table_.repaint();
}

void LibraryTableComponent::applyPlaylistDefaultSort()
{
    if (viewMode_ != ViewMode::Playlist) return;
    table_.getHeader().setSortColumnId(colIdRow, true);
}

bool LibraryTableComponent::isInterestedInDragSource(const SourceDetails& details)
{
    // Only accept drops from this same table, only in playlist mode, and only
    // when the user is currently sorted by the "#" column ascending. Any
    // other sort would make the drop position meaningless. Album view is
    // read-only; dragging doesn't reorder it.
    if (viewMode_ != ViewMode::Playlist) return false;
    if (sortColumnId_ != colIdRow) return false;
    if (! sortForwards_) return false;
    if (details.sourceComponent == nullptr) return false;
    // startDragging passes this (LibraryTableComponent) as the source, so the
    // drop is ours if the source IS us or one of our children.
    return details.sourceComponent.get() == this
        || isParentOf(details.sourceComponent.get());
}

namespace
{
    // Snap a vertical point within a list of equal-height rows to the nearest
    // row boundary. Returns the index that a drop at that point would insert
    // at (0 = before first row, N = after last row).
    int dropIndexFromY(int yFromContentTop, int rowH, int numRows)
    {
        if (rowH <= 0) return numRows;
        if (yFromContentTop < 0) return 0;
        return juce::jlimit(0, numRows, (yFromContentTop + rowH / 2) / rowH);
    }
}

void LibraryTableComponent::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void LibraryTableComponent::itemDragMove(const SourceDetails& details)
{
    // Pixel Y of the boundary between the row before the drop target and the
    // one after, drawn so the user can see exactly where the dragged rows
    // would land if they released now.
    const auto posInTable = table_.getLocalPoint(this, details.localPosition);
    const int rowH    = table_.getRowHeight();
    const int headerH = table_.getHeader().getHeight();
    const int viewY   = table_.getViewport()->getViewPositionY();
    const int yFromContent = posInTable.getY() - headerH + viewY;
    const int idx     = dropIndexFromY(yFromContent,
                                       rowH,
                                       static_cast<int>(filteredTracks_.size()));

    // Convert the drop index back into a y in our local coords.
    const int indicatorInTable = headerH + idx * rowH - viewY;
    const int indicatorHere    = table_.getY() + indicatorInTable;
    // Clamp to visible table region so the line doesn't float over the header
    // or under the last row beyond the table bounds.
    const int minY = table_.getY() + headerH;
    const int maxY = table_.getBottom();
    dropIndicatorY_ = juce::jlimit(minY, maxY, indicatorHere);
    repaint();
}

void LibraryTableComponent::itemDragExit(const SourceDetails&)
{
    dropIndicatorY_ = -1;
    repaint();
}

void LibraryTableComponent::paintOverChildren(juce::Graphics& g)
{
    if (dropIndicatorY_ < 0) return;

    // Horizontal drop indicator spanning the table's body width.
    const int x1 = table_.getX();
    const int x2 = table_.getRight();
    g.setColour(Color::accent);
    g.fillRect(x1, dropIndicatorY_ - 1, x2 - x1, 2);
}

void LibraryTableComponent::itemDropped(const SourceDetails& details)
{
    dropIndicatorY_ = -1;
    repaint();

    if (! onReorderRequested) return;

    juce::StringArray paths;
    paths.addTokens(details.description.toString(), "\n", "");
    if (paths.isEmpty()) return;

    const auto posInTable = table_.getLocalPoint(this, details.localPosition);
    const int rowH    = table_.getRowHeight();
    const int headerH = table_.getHeader().getHeight();
    const int yFromContent = posInTable.getY() - headerH + table_.getViewport()->getViewPositionY();
    const int targetIndex  = dropIndexFromY(yFromContent,
                                            rowH,
                                            static_cast<int>(filteredTracks_.size()));

    onReorderRequested(paths, targetIndex);
}

void LibraryTableComponent::updateArtistColumnHeader()
{
    bool hasMusic   = false;
    bool hasPodcast = false;
    for (const auto* t : filteredTracks_)
    {
        if (t->isPodcast) hasPodcast = true;
        else              hasMusic   = true;
        if (hasMusic && hasPodcast) break;
    }
    juce::String label = "Artist";
    if (hasMusic && hasPodcast) label = "Artist/Podcast";
    else if (hasPodcast)        label = "Podcast";
    table_.getHeader().setColumnName(colIdArtist, label);
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
            || t.album.toLowerCase().contains(query)
            || t.podcast.toLowerCase().contains(query))
        {
            filteredTracks_.push_back(&t);
        }
    }

    updateArtistColumnHeader();
    applySort();
    refreshPlayingIndex();
    table_.updateContent();
    table_.repaint();
}

void LibraryTableComponent::refreshPlayingIndex()
{
    playingIndex_ = -1;
    if (playingFile_.getFullPathName().isEmpty()) return;

    for (int i = 0; i < static_cast<int>(filteredTracks_.size()); ++i)
    {
        if (filteredTracks_[static_cast<size_t>(i)]->file == playingFile_)
        {
            playingIndex_ = i;
            return;
        }
    }
}

void LibraryTableComponent::applySort()
{
    if (filteredTracks_.size() < 2) return;

    // Default sort (no column selected): for music views, order by artist →
    // album → track number → title so albums always appear in track order.
    // Playlists and podcasts keep their natural insertion order.
    if (sortColumnId_ == 0)
    {
        if (viewMode_ == ViewMode::Library ||
            viewMode_ == ViewMode::Artist  ||
            viewMode_ == ViewMode::Album)
        {
            std::stable_sort(filteredTracks_.begin(), filteredTracks_.end(),
                [](const TrackInfo* a, const TrackInfo* b) {
                    if (int c = a->artist.compareIgnoreCase(b->artist)) return c < 0;
                    if (int c = a->album.compareIgnoreCase(b->album))   return c < 0;
                    if (a->trackNumber != b->trackNumber)
                    {
                        // Tracks with no number (0) sort after numbered ones.
                        if (a->trackNumber == 0) return false;
                        if (b->trackNumber == 0) return true;
                        return a->trackNumber < b->trackNumber;
                    }
                    return a->displayTitle().compareIgnoreCase(b->displayTitle()) < 0;
                });
        }
        return;
    }

    const int  col = sortColumnId_;
    const bool fwd = sortForwards_;

    auto cmp = [col, this](const TrackInfo* a, const TrackInfo* b) -> int {
        switch (col)
        {
            case colIdRow:
            {
                // Playlist view: sort by each track's position in the backing
                // tracks_ vector (setTracks() fills it in playlist order).
                // Album view: sort by the static trackNumber tag.
                if (viewMode_ == ViewMode::Album)
                {
                    return (a->trackNumber < b->trackNumber) ? -1
                         : (a->trackNumber > b->trackNumber) ?  1 : 0;
                }
                const auto* base = tracks_.data();
                const int ai = static_cast<int>(a - base);
                const int bi = static_cast<int>(b - base);
                return (ai < bi) ? -1 : (ai > bi) ? 1 : 0;
            }
            case colIdTitle:  return a->displayTitle().compareIgnoreCase(b->displayTitle());
            case colIdArtist:
            {
                const juce::String& av = a->isPodcast ? a->podcast : a->artist;
                const juce::String& bv = b->isPodcast ? b->podcast : b->artist;
                return av.compareIgnoreCase(bv);
            }
            case colIdAlbum:  return a->album.compareIgnoreCase(b->album);
            case colIdGenre:  return a->genre.compareIgnoreCase(b->genre);
            case colIdTime:   return (a->durationSecs < b->durationSecs) ? -1
                                  : (a->durationSecs > b->durationSecs) ?  1 : 0;
            case colIdBpm:    return (a->bpm          < b->bpm)          ? -1
                                  : (a->bpm          > b->bpm)          ?  1 : 0;
            case colIdKey:    return a->musicalKey.compareIgnoreCase(b->musicalKey);
            case colIdPlays:  return (a->playCount    < b->playCount)    ? -1
                                  : (a->playCount    > b->playCount)    ?  1 : 0;
            case colIdFormat: return a->file.getFileExtension().compareIgnoreCase(b->file.getFileExtension());
            default:          return 0;
        }
    };

    std::stable_sort(filteredTracks_.begin(), filteredTracks_.end(),
        [&cmp, fwd](const TrackInfo* a, const TrackInfo* b) {
            const int c = cmp(a, b);
            return fwd ? (c < 0) : (c > 0);
        });
}

void LibraryTableComponent::sortOrderChanged(int newSortColumnId, bool isForwards)
{
    sortColumnId_ = newSortColumnId;
    sortForwards_ = isForwards;
    applySort();
    refreshPlayingIndex();
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

void LibraryTableComponent::setPlayingFile(const juce::File& file)
{
    playingFile_ = file;
    refreshPlayingIndex();
    table_.repaint();
}

void LibraryTableComponent::selectAndScrollToPlayingRow()
{
    if (playingIndex_ < 0) return;
    table_.selectRow(playingIndex_, false, true);
    table_.scrollToEnsureRowIsOnscreen(playingIndex_);
}

void LibraryTableComponent::scrollToFile(const juce::File& file)
{
    for (int i = 0; i < (int)filteredTracks_.size(); ++i)
    {
        if (filteredTracks_[(size_t)i]->file == file)
        {
            table_.selectRow(i, false, true);
            table_.scrollToEnsureRowIsOnscreen(i);
            return;
        }
    }
}

void LibraryTableComponent::setShowHidden(bool show)
{
    showHidden_ = show;
    applyFilter();
}

void LibraryTableComponent::setSearchPlaceholder(const juce::String& viewName)
{
    searchBox_.setTextToShowWhenEmpty("Search " + viewName + "... (Cmd-F)", Color::textDim);
    searchBox_.repaint();
}

void LibraryTableComponent::focusSearchBox()
{
    searchBox_.grabKeyboardFocus();
    searchBox_.selectAll();
}

void LibraryTableComponent::resized()
{
    auto bounds = getLocalBounds();

    auto searchRow = bounds.removeFromTop(searchBarHeight).reduced(0, 2);
    searchBox_.setBounds(searchRow);

    table_.setBounds(bounds);
}

void LibraryTableComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::tableBackground);

    // Draw full-height alternating stripes behind the table so the pattern
    // continues into the empty area below the last row. Actual row backgrounds
    // painted by paintRowBackground() are opaque and sit on top of these stripes,
    // but where no rows exist the stripes show through the transparent table background.
    // Stripes stop at the scrollbar's left edge: the scrollbar area gets a
    // solid backdrop instead, otherwise these static stripes visibly fail to
    // move when the user scrolls the rows beneath.
    const int headerH     = table_.getHeader().getHeight();
    const int stripeTop   = table_.getY() + headerH;
    const int sbW         = table_.getVerticalScrollBar().getWidth();
    const int stripeRight = table_.getRight() - sbW;
    const int stripeW     = stripeRight - table_.getX();

    int y = stripeTop;
    int row = 0;
    while (y < getHeight())
    {
        g.setColour(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);
        g.fillRect(table_.getX(), y, stripeW, rowHeight);
        y += rowHeight;
        ++row;
    }

    if (sbW > 0)
    {
        g.setColour(Color::headerBackground);
        g.fillRect(stripeRight, stripeTop, sbW, getHeight() - stripeTop);
    }

    if (filteredTracks_.empty() && !suppressEmptyLabel_)
    {
        g.setColour(Color::textDim);
        g.setFont(juce::Font(juce::FontOptions().withHeight(16.0f)));
        g.drawText("Empty", table_.getX(), stripeTop, stripeW, getHeight() - stripeTop,
                   juce::Justification::centred, false);
    }
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
    // Green on *every* row whose file is the currently-playing file, so duplicate
    // entries in a playlist all show as playing. The blue row-background highlight
    // (in paintRowBackground) still only marks the first match, via playingIndex_.
    const bool isPlaying = (playingFile_.getFullPathName().isNotEmpty()
                            && t.file == playingFile_);

    if (isPlaying)
        g.setColour(Color::playingHighlight);
    else if (t.hidden)
        g.setColour(Color::textDim);
    else
        g.setColour(Color::textPrimary);

    g.setFont(juce::Font(15.0f));

    const juce::String text = cellText(row, col);
    g.drawText(text, 4, 0, w - 8, h, juce::Justification::centredLeft, true);
}

juce::String LibraryTableComponent::cellText(int row, int colId) const
{
    const auto& t = *filteredTracks_[static_cast<size_t>(row)];
    switch (colId)
    {
        case colIdRow:
        {
            // Album/Podcast view: show the static track number tag. Playlist view:
            // show the 1-based index in the backing tracks_ vector (set by
            // setTracks in playlist order).
            if (viewMode_ == ViewMode::Album || viewMode_ == ViewMode::Podcast)
                return t.trackNumber > 0 ? juce::String(t.trackNumber) : juce::String();

            const auto* base = tracks_.data();
            const int idx = static_cast<int>(&t - base) + 1;
            return juce::String(idx);
        }
        case colIdTitle:  return t.displayTitle();
        case colIdArtist: return t.isPodcast ? t.podcast : t.artist;
        case colIdAlbum:  return t.album;
        case colIdGenre:  return t.genre;
        case colIdTime:   return t.formattedDuration();
        case colIdBpm:    return t.bpm > 0.0 ? juce::String(t.bpm, 1) : juce::String();
        case colIdKey:    return t.musicalKey;
        case colIdPlays:  return t.playCount > 0 ? juce::String(t.playCount) : juce::String();
        case colIdFormat:
        {
            // Strip the leading dot and lowercase, e.g. ".MP3" -> "mp3".
            juce::String ext = t.file.getFileExtension();
            if (ext.startsWithChar('.'))
                ext = ext.substring(1);
            return ext.toLowerCase();
        }
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

void LibraryTableComponent::cellClicked(int row, int col, const juce::MouseEvent& e)
{
    if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) return;

    if (e.getNumberOfClicks() == 3 && isEditableColId(col) && !e.mods.isPopupMenu())
    {
        if (editingRow_ == row && editingColId_ == col) return;
        if (editingRow_ >= 0) commitCellEdit();
        startCellEdit(row, col);
        return;
    }

    if (!e.mods.isPopupMenu()) return;

    // Ensure the right-clicked row is selected if it isn't already.
    if (!table_.isRowSelected(row))
        table_.selectRow(row);

    const auto selRows = selectedRows();
    const bool singleSelect = (selRows.size() == 1);

    std::vector<TrackInfo> selectedTracks;
    selectedTracks.reserve(selRows.size());
    for (int r : selRows)
        if (r >= 0 && r < static_cast<int>(filteredTracks_.size()))
            selectedTracks.push_back(*filteredTracks_[static_cast<size_t>(r)]);
    if (selectedTracks.empty()) return;

    bool hasMusic = false, hasPodcast = false;
    for (const auto& t : selectedTracks) {
        if (t.isPodcast) hasPodcast = true;
        else             hasMusic   = true;
    }
    const bool mixedType  = hasMusic && hasPodcast;
    const bool allPodcast = hasPodcast && !hasMusic;
    const bool allMusic   = hasMusic   && !hasPodcast;

    bool anyHidden  = false, anyVisible = false;
    for (const auto& t : selectedTracks) {
        if (t.hidden)  anyHidden  = true;
        else           anyVisible = true;
    }

    const juce::File file        = filteredTracks_[static_cast<size_t>(row)]->file;
    const TrackInfo clickedTrack = *filteredTracks_[static_cast<size_t>(row)];

    const bool canGoToArtist  = singleSelect && !clickedTrack.isPodcast && clickedTrack.artist.isNotEmpty();
    const bool canGoToAlbum   = singleSelect && !clickedTrack.isPodcast && clickedTrack.album.isNotEmpty();
    const bool canGoToPodcast = singleSelect &&  clickedTrack.isPodcast && clickedTrack.podcast.isNotEmpty();

    juce::PopupMenu menu;
    menu.addItem(6, "Add to Queue");
    menu.addSeparator();
    menu.addItem(5, "Edit Info",  !mixedType);
    menu.addItem(8, "Clear Info", !mixedType);
    const bool showUndo    = singleSelect && allMusic
                             && isLookupUndoable && isLookupUndoable(clickedTrack.file);
    const bool showArtUndo = singleSelect && allMusic
                             && isArtLookupUndoable && isArtLookupUndoable(clickedTrack.file);
    if (allMusic) {
        menu.addItem(7,  showUndo    ? "Undo Apple Music lookup"  : "Look up on Apple Music");
        menu.addItem(13, showArtUndo ? "Undo Album art lookup"    : "Look up Album Art");
        menu.addItem(4,  "Analyze for Key and BPM");
    }
    if (allPodcast)
        menu.addItem(12, "Look up on Podcast Index");

    menu.addSeparator();
    if (canGoToArtist)  menu.addItem(9,  "Go to Artist");
    if (canGoToAlbum)   menu.addItem(10, "Go to Album");
    if (canGoToPodcast) menu.addItem(11, "Go to Podcast");
    if (anyVisible) menu.addItem(2, "Hide from Library");
    if (anyHidden)  menu.addItem(3, "Unhide from Library");
    if (singleSelect)
    {
        menu.addSeparator();
        menu.addItem(1, "Show in Finder");
    }

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetScreenArea(
        juce::Rectangle<int>().withPosition(e.getScreenPosition())),
        [this, file, selectedTracks, clickedTrack, singleSelect, showUndo, showArtUndo](int result) {
            if (result == 1)
                file.revealToUser();
            else if (result == 2)
                setHiddenForSelection(true);
            else if (result == 3)
                setHiddenForSelection(false);
            else if (result == 4)
                { if (onAnalyzeRequested) onAnalyzeRequested(selectedTracks); }
            else if (result == 5) {
                if (singleSelect) { if (onEditRequested)      onEditRequested(clickedTrack); }
                else              { if (onMultiEditRequested) onMultiEditRequested(selectedTracks); }
            }
            else if (result == 6)
                { if (onAddToQueueRequested) onAddToQueueRequested(selectedTracks); }
            else if (result == 7) {
                if (showUndo) { if (onAppleMusicUndoRequested)    onAppleMusicUndoRequested(clickedTrack); }
                else          { if (onAppleMusicLookupRequested) onAppleMusicLookupRequested(selectedTracks); }
            }
            else if (result == 8)
                { if (onClearInfoRequested) onClearInfoRequested(selectedTracks); }
            else if (result == 9)
                { if (onGoToArtistRequested) onGoToArtistRequested(clickedTrack); }
            else if (result == 10)
                { if (onGoToAlbumRequested) onGoToAlbumRequested(clickedTrack); }
            else if (result == 11)
                { if (onGoToPodcastRequested) onGoToPodcastRequested(clickedTrack); }
            else if (result == 12)
                { if (onPodcastLookupRequested) onPodcastLookupRequested(selectedTracks); }
            else if (result == 13) {
                if (showArtUndo) { if (onAlbumArtUndoRequested)    onAlbumArtUndoRequested(clickedTrack); }
                else             { if (onAlbumArtLookupRequested) onAlbumArtLookupRequested(selectedTracks); }
            }
        });
}

bool LibraryTableComponent::isEditableColId(int colId)
{
    return colId == colIdTitle  || colId == colIdArtist
        || colId == colIdAlbum  || colId == colIdGenre;
}

void LibraryTableComponent::cellDoubleClicked(int row, int /*col*/, const juce::MouseEvent&)
{
    if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) return;
    if (editingRow_ >= 0) cancelCellEdit();
    if (onRowActivated) onRowActivated(row);
}

void LibraryTableComponent::startCellEdit(int row, int colId)
{
    editingRow_   = row;
    editingColId_ = colId;
    ++editingSession_;

    table_.getViewport()->setScrollBarsShown(false, false);
    table_.updateContent();
    table_.scrollToEnsureRowIsOnscreen(row);

    if (activeCellEditor_)
    {
        activeCellEditor_->grabKeyboardFocus();
        activeCellEditor_->selectAll();
    }
}

Component* LibraryTableComponent::refreshComponentForCell(int rowNumber, int columnId,
                                                           bool /*isRowSelected*/,
                                                           Component* existing)
{
    if (rowNumber != editingRow_ || columnId != editingColId_)
    {
        if (existing == activeCellEditor_) activeCellEditor_ = nullptr;
        delete existing;
        return nullptr;
    }

    if (auto* editor = dynamic_cast<juce::TextEditor*>(existing))
    {
        activeCellEditor_ = editor;
        return editor;
    }
    delete existing;

    juce::String initialText;
    if (rowNumber >= 0 && rowNumber < static_cast<int>(filteredTracks_.size()))
    {
        const auto& t = *filteredTracks_[static_cast<size_t>(rowNumber)];
        switch (columnId)
        {
            case colIdTitle:  initialText = t.title.isNotEmpty() ? t.title : t.displayTitle(); break;
            case colIdArtist: initialText = t.isPodcast ? t.podcast : t.artist; break;
            case colIdAlbum:  initialText = t.album; break;
            case colIdGenre:  initialText = t.genre;  break;
            default: break;
        }
    }

    const int session = editingSession_;
    auto* editor = new juce::TextEditor();
    activeCellEditor_ = editor;

    editor->setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    editor->setText(initialText, false);
    editor->setColour(juce::TextEditor::backgroundColourId,      Color::tableSelected);
    editor->setColour(juce::TextEditor::textColourId,            Color::textPrimary);
    editor->setColour(juce::TextEditor::outlineColourId,         Color::accent);
    editor->setColour(juce::TextEditor::focusedOutlineColourId,  Color::accent);
    editor->setColour(juce::TextEditor::highlightColourId,       Color::accent.withAlpha(0.35f));
    editor->setJustification(juce::Justification::centredLeft);
    editor->setIndents(4, 0);

    editor->onReturnKey = [this, session] {
        juce::MessageManager::callAsync([this, session] {
            if (editingSession_ == session) commitCellEdit();
        });
    };
    editor->onEscapeKey = [this, session] {
        juce::MessageManager::callAsync([this, session] {
            if (editingSession_ == session) cancelCellEdit();
        });
    };
    editor->onFocusLost = [this, session] {
        juce::MessageManager::callAsync([this, session] {
            if (editingSession_ == session) commitCellEdit();
        });
    };

    return editor;
}

void LibraryTableComponent::commitCellEdit()
{
    if (editingRow_ < 0) return;

    const juce::String text = activeCellEditor_ ? activeCellEditor_->getText().trim() : juce::String{};
    const int row = editingRow_;
    const int col = editingColId_;

    activeCellEditor_ = nullptr; // clear before updateContent() deletes the editor
    editingRow_   = -1;
    editingColId_ = -1;

    table_.getViewport()->setScrollBarsShown(true, true);
    table_.updateContent();
    table_.repaint();

    if (row < 0 || row >= static_cast<int>(filteredTracks_.size())) return;
    if (text.isEmpty()) return; // empty input: no change (silently revert)

    auto* track = filteredTracks_[static_cast<size_t>(row)];
    bool changed = false;

    switch (col)
    {
        case colIdTitle:
            if (track->title != text) { track->title = text; changed = true; }
            break;
        case colIdArtist:
            if (track->isPodcast) {
                if (track->podcast != text) { track->podcast = text; changed = true; }
            } else {
                if (track->artist != text) { track->artist = text; changed = true; }
            }
            break;
        case colIdAlbum:
            if (track->album != text) { track->album = text; changed = true; }
            break;
        case colIdGenre:
            if (track->genre != text) { track->genre = text; changed = true; }
            break;
        default: break;
    }

    if (changed)
    {
        FoxpFile::save(*track);
        if (onInlineEditCommitted) onInlineEditCommitted(*track);
    }
}

void LibraryTableComponent::cancelCellEdit()
{
    if (editingRow_ < 0) return;

    activeCellEditor_ = nullptr;
    editingRow_   = -1;
    editingColId_ = -1;

    table_.getViewport()->setScrollBarsShown(true, true);
    table_.updateContent();
    table_.repaint();
}

void LibraryTableComponent::deleteKeyPressed(int /*lastRowSelected*/)
{
    const auto rows = selectedRows();
    if (rows.empty()) return;

    std::vector<TrackInfo> selected;
    for (int r : rows)
        if (r >= 0 && r < static_cast<int>(filteredTracks_.size()))
            selected.push_back(*filteredTracks_[static_cast<size_t>(r)]);
    if (selected.empty()) return;

    if (viewMode_ == ViewMode::Playlist)
    {
        if (onRemoveFromPlaylistRequested)
            onRemoveFromPlaylistRequested(selected);
        return;
    }

    // Library / Artist / Album / Podcast views: confirm before hiding.
    juce::String message;
    if (selected.size() == 1)
    {
        const auto& t = selected.front();
        const juce::String label = (t.artist.isNotEmpty() && t.title.isNotEmpty())
            ? t.artist + " - " + t.title
            : (t.title.isNotEmpty() ? t.title : t.file.getFileNameWithoutExtension());
        message = "Are you sure you want to delete \"" + label + "\" from your Library?";
    }
    else
    {
        message = "Are you sure you want to delete "
                  + juce::String(static_cast<int>(selected.size()))
                  + " tracks from your Library?";
    }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Delete from Library")
            .withMessage(message)
            .withButton("Delete")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [this](int result) {
            if (result == 1)
                setHiddenForSelection(true);
        });
}

void LibraryTableComponent::returnKeyPressed(int lastRowSelected)
{
    if (lastRowSelected >= 0 && lastRowSelected < static_cast<int>(filteredTracks_.size()))
        if (onRowActivated) onRowActivated(lastRowSelected);
}

void LibraryTableComponent::mouseDrag(const juce::MouseEvent& e)
{
    // Don't intercept drags that started on the column header (resize handles, etc.).
    auto& hdr = table_.getHeader();
    if (e.originalComponent != nullptr
        && (e.originalComponent == &hdr || hdr.isParentOf(e.originalComponent)))
        return;

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
        g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
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
    if (editingRow_ >= 0) commitCellEdit();
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
