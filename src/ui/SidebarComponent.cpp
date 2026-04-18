#include "SidebarComponent.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

namespace
{
    std::unique_ptr<juce::Drawable> loadSvg(const char* data, int size)
    {
        const auto xmlStr = juce::String::createStringFromData(data, size);
        if (auto xml = juce::XmlDocument::parse(xmlStr))
            return juce::Drawable::createFromSVG(*xml);
        return nullptr;
    }
}

SidebarComponent::SidebarComponent()
{
    musicIconDrawable_    = loadSvg(BinaryData::musicnotesfill_svg,  BinaryData::musicnotesfill_svgSize);
    podcastIconDrawable_  = loadSvg(BinaryData::microphonefill_svg,  BinaryData::microphonefill_svgSize);
    playlistIconDrawable_ = loadSvg(BinaryData::listbulletsfill_svg, BinaryData::listbulletsfill_svgSize);
    artistIconDrawable_   = loadSvg(BinaryData::userfill_svg,        BinaryData::userfill_svgSize);
    albumIconDrawable_    = loadSvg(BinaryData::vinylrecordfill_svg, BinaryData::vinylrecordfill_svgSize);

    // LIBRARY section: not collapsible
    Section library;
    library.heading       = "LIBRARY";
    library.collapsible   = false;
    library.rightClickable = false;
    library.items.push_back({ "All Music",     1, {} });
    library.items.push_back({ "All Podcasts",  2, {} });
    sections_.push_back(std::move(library));

    // ARTISTS section: collapsible, populated from the library's unique artists.
    Section artists;
    artists.heading        = "ARTISTS";
    artists.collapsible    = true;
    artists.collapsed      = true;   // start collapsed since the list can be long
    artists.rightClickable = false;
    sections_.push_back(std::move(artists));

    // ALBUMS section: collapsible, "[ARTIST] - [ALBUM]" entries.
    Section albums;
    albums.heading        = "ALBUMS";
    albums.collapsible    = true;
    albums.collapsed      = true;
    albums.rightClickable = false;
    sections_.push_back(std::move(albums));

    // PODCASTS section: collapsible, shows one entry per podcast show.
    Section podcasts;
    podcasts.heading        = "PODCASTS";
    podcasts.collapsible    = true;
    podcasts.collapsed      = true;
    podcasts.rightClickable = false;
    sections_.push_back(std::move(podcasts));

    // PLAYLISTS section: collapsible, right-clickable for "Create New Playlist"
    Section playlists;
    playlists.heading        = "PLAYLISTS";
    playlists.collapsible    = true;
    playlists.collapsed      = false;
    playlists.rightClickable = true;
    sections_.push_back(std::move(playlists));
}

void SidebarComponent::setPodcasts(const std::vector<std::pair<int, juce::String>>& podcasts)
{
    auto& section = sections_[3]; // PODCASTS is always index 3
    section.items.clear();
    for (const auto& [id, name] : podcasts)
        section.items.push_back({ name, id, {} });
    layoutItems();
    repaint();
}

void SidebarComponent::setPlaylists(const std::vector<std::pair<int, juce::String>>& playlists)
{
    auto& section = sections_[4]; // PLAYLISTS is always index 4
    section.items.clear();
    for (const auto& [id, name] : playlists)
        section.items.push_back({ name, id, {} });
    section.items.push_back({ "+ New Playlist", newPlaylistItemId, {} });
    layoutItems();
    repaint();
}

void SidebarComponent::setArtists(const std::vector<std::pair<int, juce::String>>& artists)
{
    auto& section = sections_[1]; // ARTISTS is always index 1
    section.items.clear();
    for (const auto& [id, name] : artists)
        section.items.push_back({ name, id, {} });
    layoutItems();
    repaint();
}

void SidebarComponent::setAlbums(const std::vector<std::pair<int, juce::String>>& albums)
{
    auto& section = sections_[2]; // ALBUMS is always index 2
    section.items.clear();
    for (const auto& [id, name] : albums)
        section.items.push_back({ name, id, {} });
    layoutItems();
    repaint();
}

juce::Rectangle<int> SidebarComponent::boundsForSelectedItem() const
{
    for (const auto& section : sections_)
    {
        if (section.collapsible && section.collapsed) continue;
        for (const auto& item : section.items)
            if (item.id == selectedId_)
                return item.bounds;
    }
    return {};
}

void SidebarComponent::setLibraryLoading(bool loading)
{
    if (libraryLoading_ == loading) return;
    libraryLoading_ = loading;
    if (loading) startTimerHz(30);
    else         stopTimer();
    repaint();
}

void SidebarComponent::timerCallback()
{
    loadingRotation_ += juce::MathConstants<float>::pi * 0.08f;
    if (loadingRotation_ > juce::MathConstants<float>::twoPi)
        loadingRotation_ -= juce::MathConstants<float>::twoPi;
    repaint();
}

void SidebarComponent::setSelectedId(int id)
{
    selectedId_ = id;

    // Auto-expand the section that owns the selected item, so the user can
    // actually see the selected row when something navigates them to it
    // (e.g. clicking the "Playing from: <artist>" link).
    for (auto& section : sections_)
    {
        if (! section.collapsible || ! section.collapsed) continue;
        for (const auto& item : section.items)
        {
            if (item.id == id)
            {
                section.collapsed = false;
                layoutItems();
                break;
            }
        }
    }

    repaint();
}

void SidebarComponent::layoutItems()
{
    int y = 0;
    for (int si = 0; si < (int)sections_.size(); ++si)
    {
        if (si > 0) y += 6; // gap before each section except the first
        auto& section = sections_[(size_t)si];
        section.headerBounds = juce::Rectangle<int>(0, y, getWidth(), sectionHeaderH);
        y += sectionHeaderH;

        if (!section.collapsed)
        {
            for (auto& item : section.items)
            {
                item.bounds = juce::Rectangle<int>(0, y, getWidth(), itemH);
                y += itemH;
            }
        }
    }

    // Size the component to fit its content, but never smaller than the viewport,
    // so the background fills the whole visible area even with few playlists.
    const int visibleH = getParentHeight();
    const int targetH  = juce::jmax(y, visibleH);
    if (getHeight() != targetH)
        setSize(getWidth(), targetH);
}

void SidebarComponent::drawDisclosureTriangle(juce::Graphics& g,
                                               float x, int centreY,
                                               bool isCollapsed) const
{
    const float size = 6.0f;
    const float cy   = static_cast<float>(centreY);

    juce::Path tri;
    if (isCollapsed)
    {
        tri.addTriangle(x, cy - size * 0.5f,
                        x, cy + size * 0.5f,
                        x + size, cy);
    }
    else
    {
        tri.addTriangle(x, cy - size * 0.4f,
                        x + size, cy - size * 0.4f,
                        x + size * 0.5f, cy + size * 0.5f);
    }
    g.setColour(Color::textDim);
    g.fillPath(tri);
}

void SidebarComponent::drawSectionHeader(juce::Graphics& g, const Section& section, int y) const
{
    const juce::Font headingFont(juce::FontOptions().withHeight(12.0f));
    g.setColour(Color::textDim);
    g.setFont(headingFont);
    g.drawText(section.heading,
               itemPadL, y,
               getWidth() - itemPadL - 20,
               sectionHeaderH,
               juce::Justification::centredLeft, false);

    if (section.collapsible)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(headingFont, section.heading, 0.0f, 0.0f);
        const float textW = ga.getBoundingBox(0, -1, true).getWidth();
        const float triX  = static_cast<float>(itemPadL) + textW + 6.0f;
        drawDisclosureTriangle(g, triX, y + sectionHeaderH / 2, section.collapsed);
    }

    if (libraryLoading_ && section.heading == "LIBRARY")
    {
        const juce::Font f2(juce::FontOptions().withHeight(12.0f));
        juce::GlyphArrangement ga;
        ga.addLineOfText(f2, section.heading, 0.0f, 0.0f);
        const float textW = ga.getBoundingBox(0, -1, true).getWidth();

        const float r  = 5.5f;
        const float cx = static_cast<float>(itemPadL) + textW + 12.0f + r;
        const float cy = static_cast<float>(y + sectionHeaderH / 2);

        g.setColour(Color::textDim);
        g.drawEllipse(cx - r, cy - r, r * 2.0f, r * 2.0f, 1.5f);

        juce::Path arc;
        const float sweep = juce::MathConstants<float>::pi * 1.55f;
        arc.addCentredArc(cx, cy, r, r, 0.0f,
                          loadingRotation_, loadingRotation_ + sweep, true);
        g.setColour(Color::accent);
        g.strokePath(arc, juce::PathStrokeType(1.5f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    }
}

void SidebarComponent::drawSectionItem(juce::Graphics& g,
                                        const Item& item,
                                        juce::Rectangle<int> rowBounds) const
{
    if (item.id == newPlaylistItemId)
    {
        if (item.id == dragOverItemId_)
        {
            g.setColour(Color::accent.withAlpha(0.25f));
            g.fillRect(rowBounds);
        }
        g.setColour(item.id == dragOverItemId_ ? Color::accent : Color::textDim);
        g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
        g.drawText(item.label,
                   itemPadL + indicatorW, rowBounds.getY(),
                   rowBounds.getWidth() - itemPadL - indicatorW - 8,
                   rowBounds.getHeight(),
                   juce::Justification::centredLeft, true);
        return;
    }

    const bool selected = (item.id == selectedId_);

    if (item.id == dragOverItemId_)
    {
        g.setColour(Color::accent.withAlpha(0.25f));
        g.fillRect(rowBounds);
    }
    if (selected)
    {
        g.setColour(Color::background);
        g.fillRect(rowBounds);
    }
    if (selected)
    {
        g.setColour(Color::accent);
        g.fillRect(rowBounds.withWidth(indicatorW));
    }

    constexpr int iconDim = 16;
    constexpr int iconGap = 8;
    juce::Drawable* iconDrawable =
        (item.id == 1)                      ? musicIconDrawable_.get()
      : (item.id == 2)                      ? podcastIconDrawable_.get()
      : (item.id >= 2000 && item.id < 3000) ? artistIconDrawable_.get()
      : (item.id >= 3000 && item.id < 4000) ? albumIconDrawable_.get()
      : (item.id >= 4000 && item.id < 5000) ? podcastIconDrawable_.get()
      :                                       playlistIconDrawable_.get();
    const int iconX = rowBounds.getX() + itemPadL + indicatorW;
    const int iconY = rowBounds.getY() + (rowBounds.getHeight() - iconDim) / 2;
    if (iconDrawable)
    {
        auto tinted = iconDrawable->createCopy();
        tinted->replaceColour(juce::Colours::black,
                              selected ? Color::textPrimary : Color::textSecondary);
        tinted->drawWithin(g,
                           juce::Rectangle<float>((float)iconX, (float)iconY,
                                                  (float)iconDim, (float)iconDim),
                           juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                           1.0f);
    }

    const int labelX = iconX + iconDim + iconGap;
    g.setColour(selected ? Color::textPrimary : Color::textSecondary);
    g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    g.drawText(item.label,
               labelX, rowBounds.getY(),
               rowBounds.getRight() - labelX - 8,
               rowBounds.getHeight(),
               juce::Justification::centredLeft, true);
}

void SidebarComponent::getStickyZone(int& outScrollY,
                                      int& outLibStickyH,
                                      int& outActiveSectionIdx) const
{
    outScrollY = juce::jmax(0, -getY());

    // Use the actual layout position of the first non-Library section as the
    // Library sticky height. This naturally includes any inter-section gap so
    // activeTop aligns exactly with where that section starts -- eliminating
    // the small dead zone that would otherwise appear when scrolling begins.
    if (sections_.size() >= 2)
        outLibStickyH = sections_[1].headerBounds.getY();
    else
    {
        const auto& libSec = sections_[0];
        outLibStickyH = sectionHeaderH;
        if (!libSec.collapsed)
            outLibStickyH += (int)libSec.items.size() * itemH;
    }

    outActiveSectionIdx = -1;
    const int activeTop = outScrollY + outLibStickyH;

    for (int i = 1; i < (int)sections_.size(); ++i)
    {
        const auto& sec = sections_[i];
        if (sec.collapsed || sec.items.empty()) continue;
        const int secHeaderTop     = sec.headerBounds.getY();
        const int secContentBottom = sec.items.back().bounds.getBottom();
        if (secHeaderTop <= activeTop && secContentBottom > activeTop)
        {
            outActiveSectionIdx = i;
            break;
        }
    }
}

void SidebarComponent::moved()
{
    repaint();
}

void SidebarComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::headerBackground);
    g.setColour(Color::border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());

    // Normal (non-sticky) content pass
    for (const auto& section : sections_)
    {
        drawSectionHeader(g, section, section.headerBounds.getY());
        if (!section.collapsed)
            for (const auto& item : section.items)
                drawSectionItem(g, item, item.bounds);
    }

    // Sticky overlay — only needed once we've scrolled past the LIBRARY header
    int scrollY, libStickyH, activeSectionIdx;
    getStickyZone(scrollY, libStickyH, activeSectionIdx);

    if (scrollY <= sections_[0].headerBounds.getY())
        return;

    // Draw sticky LIBRARY zone (background + content)
    g.setColour(Color::headerBackground);
    g.fillRect(0, scrollY, getWidth(), libStickyH);
    g.setColour(Color::border);
    g.drawVerticalLine(getWidth() - 1, (float)scrollY, (float)(scrollY + libStickyH));

    drawSectionHeader(g, sections_[0], scrollY);
    if (!sections_[0].collapsed)
    {
        int y = scrollY + sectionHeaderH;
        for (const auto& item : sections_[0].items)
        {
            drawSectionItem(g, item, { 0, y, getWidth(), itemH });
            y += itemH;
        }
    }

    // Draw sticky active-section header (the section currently being scrolled through)
    int totalStickyH = libStickyH;
    if (activeSectionIdx >= 0)
    {
        const auto& activeSec = sections_[(size_t)activeSectionIdx];
        const int activeY = scrollY + libStickyH;
        g.setColour(Color::headerBackground);
        g.fillRect(0, activeY, getWidth(), sectionHeaderH);
        g.setColour(Color::border);
        g.drawVerticalLine(getWidth() - 1, (float)activeY, (float)(activeY + sectionHeaderH));
        drawSectionHeader(g, activeSec, activeY);
        totalStickyH += sectionHeaderH;
    }

    // Re-draw section headers entering from below the ACTIVE-SECTION part of the sticky
    // zone (i.e. approaching from below activeTop), so they slide OVER the active header
    // rather than under it. Headers passing through the Library zone are excluded — they
    // should disappear behind it, not float above it.
    int visualStickyBottom = scrollY + totalStickyH;
    for (int i = 1; i < (int)sections_.size(); ++i)
    {
        if (i == activeSectionIdx) continue;
        const auto& sec = sections_[(size_t)i];
        const int secTop = sec.headerBounds.getY();
        // Only the transition zone just below activeTop (scrollY + libStickyH).
        if (secTop < scrollY + libStickyH) continue;
        if (secTop >= scrollY + libStickyH + sectionHeaderH) break;
        g.setColour(Color::headerBackground);
        g.fillRect(0, secTop, getWidth(), sectionHeaderH);
        g.setColour(Color::border);
        g.drawVerticalLine(getWidth() - 1, (float)secTop, (float)(secTop + sectionHeaderH));
        drawSectionHeader(g, sec, secTop);
        visualStickyBottom = juce::jmax(visualStickyBottom, secTop + sectionHeaderH);
    }

    // Subtle shadow below the entire visible sticky zone
    g.setColour(juce::Colours::black.withAlpha(0.18f));
    g.fillRect(0, visualStickyBottom, getWidth(), 2);
}

void SidebarComponent::resized()
{
    layoutItems();
}

void SidebarComponent::mouseDown(const juce::MouseEvent& e)
{
    // Check sticky zones before the normal layout-based hit test.
    int scrollY, libStickyH, activeSectionIdx;
    getStickyZone(scrollY, libStickyH, activeSectionIdx);

    if (scrollY > sections_[0].headerBounds.getY())
    {
        // Click inside the sticky LIBRARY zone?
        if (e.y >= scrollY && e.y < scrollY + libStickyH)
        {
            if (e.y >= scrollY + sectionHeaderH && !sections_[0].collapsed)
            {
                const int idx = (e.y - (scrollY + sectionHeaderH)) / itemH;
                if (idx >= 0 && idx < (int)sections_[0].items.size())
                    if (!e.mods.isPopupMenu())
                        selectId(sections_[0].items[(size_t)idx].id);
            }
            // LIBRARY header itself: non-collapsible, no action.
            return;
        }

        // Click inside the sticky active-section header?
        if (activeSectionIdx >= 0)
        {
            const int activeY = scrollY + libStickyH;
            if (e.y >= activeY && e.y < activeY + sectionHeaderH)
            {
                auto& sec = sections_[(size_t)activeSectionIdx];
                if (e.mods.isPopupMenu() && sec.rightClickable)
                {
                    juce::PopupMenu menu;
                    menu.addItem(1, "Create New Playlist");
                    menu.showMenuAsync(
                        juce::PopupMenu::Options{}.withTargetScreenArea(
                            juce::Rectangle<int>().withPosition(e.getScreenPosition())),
                        [this](int result) {
                            if (result == 1 && onCreatePlaylistRequested)
                                onCreatePlaylistRequested();
                        });
                }
                else if (!e.mods.isPopupMenu() && sec.collapsible)
                {
                    sec.collapsed = !sec.collapsed;
                    layoutItems();
                    repaint();
                }
                return;
            }
        }
    }

    // Normal layout-based hit test
    for (auto& section : sections_)
    {
        if (section.headerBounds.contains(e.x, e.y))
        {
            if (e.mods.isPopupMenu() && section.rightClickable)
            {
                juce::PopupMenu menu;
                menu.addItem(1, "Create New Playlist");
                menu.showMenuAsync(
                    juce::PopupMenu::Options{}.withTargetScreenArea(
                        juce::Rectangle<int>().withPosition(e.getScreenPosition())),
                    [this](int result) {
                        if (result == 1 && onCreatePlaylistRequested)
                            onCreatePlaylistRequested();
                    });
            }
            else if (!e.mods.isPopupMenu() && section.collapsible)
            {
                section.collapsed = !section.collapsed;
                layoutItems();
                repaint();
            }
            return;
        }

        if (!section.collapsed)
        {
            for (const auto& item : section.items)
            {
                if (item.bounds.contains(e.x, e.y))
                {
                    if (item.id == newPlaylistItemId)
                    {
                        if (!e.mods.isPopupMenu() && onCreatePlaylistRequested)
                            onCreatePlaylistRequested();
                    }
                    else if (e.mods.isPopupMenu() && item.id >= 1000 && item.id < 2000)
                    {
                        juce::PopupMenu menu;
                        menu.addItem(1, "Rename Playlist");
                        menu.addItem(2, "Delete Playlist");
                        const int capturedId = item.id;
                        menu.showMenuAsync(
                            juce::PopupMenu::Options{}.withTargetScreenArea(
                                juce::Rectangle<int>().withPosition(e.getScreenPosition())),
                            [this, capturedId](int result) {
                                if (result == 1)
                                    startRename(capturedId);
                                else if (result == 2 && onDeletePlaylist)
                                    onDeletePlaylist(capturedId);
                            });
                    }
                    else if (!e.mods.isPopupMenu())
                    {
                        selectId(item.id);
                    }
                    return;
                }
            }
        }
    }
}

void SidebarComponent::selectId(int id)
{
    if (id == selectedId_) return;
    selectedId_ = id;
    repaint();
    if (onItemSelected) onItemSelected(id);
}

void SidebarComponent::startRename(int sidebarId)
{
    if (inlineEditor_)
        commitRename();

    juce::Rectangle<int> rowBounds;
    juce::String plainName;

    for (const auto& section : sections_)
        for (const auto& item : section.items)
            if (item.id == sidebarId)
            {
                rowBounds = item.bounds;
                plainName = item.label;
                break;
            }

    if (rowBounds.isEmpty()) return;

    editingItemId_       = sidebarId;
    editingOriginalName_ = plainName;

    const juce::Font f(juce::FontOptions().withHeight(15.0f));
    // Must match the label offset used in paint() (icon + gap).
    constexpr int labelIconOffset = 16 + 8;
    const int edX = rowBounds.getX() + itemPadL + indicatorW + labelIconOffset;
    const int edW = rowBounds.getRight() - edX - 8;

    inlineEditor_ = std::make_unique<juce::TextEditor>();
    inlineEditor_->setFont(f);
    inlineEditor_->setText(plainName, false);
    inlineEditor_->selectAll();
    inlineEditor_->setColour(juce::TextEditor::backgroundColourId,      Color::headerBackground);
    inlineEditor_->setColour(juce::TextEditor::textColourId,            Color::textPrimary);
    inlineEditor_->setColour(juce::TextEditor::outlineColourId,         Color::accent);
    inlineEditor_->setColour(juce::TextEditor::focusedOutlineColourId,  Color::accent);
    inlineEditor_->setJustification(juce::Justification::centredLeft);
    inlineEditor_->setBounds(edX, rowBounds.getY() + 4, edW, rowBounds.getHeight() - 8);

    inlineEditor_->onReturnKey = [this] {
        juce::MessageManager::callAsync([this] { commitRename(); });
    };
    inlineEditor_->onEscapeKey = [this] {
        juce::MessageManager::callAsync([this] { cancelRename(); });
    };
    inlineEditor_->onFocusLost = [this] {
        juce::MessageManager::callAsync([this] { commitRename(); });
    };

    addAndMakeVisible(*inlineEditor_);
    inlineEditor_->grabKeyboardFocus();
    repaint();
}

void SidebarComponent::commitRename()
{
    if (!inlineEditor_) return;

    const juce::String newName = inlineEditor_->getText().trim();
    const int id = editingItemId_;
    editingItemId_ = -1;
    inlineEditor_.reset();
    repaint();

    if (newName.isNotEmpty() && onRenamePlaylist)
        onRenamePlaylist(id, newName);
    // empty input: silently revert (store is unchanged)
}

void SidebarComponent::cancelRename()
{
    if (!inlineEditor_) return;
    editingItemId_ = -1;
    inlineEditor_.reset();
    repaint();
}

int SidebarComponent::playlistItemIdAt(juce::Point<int> pos) const
{
    for (const auto& section : sections_)
    {
        if (section.collapsed) continue;
        for (const auto& item : section.items)
            if (((item.id >= 1000 && item.id < 2000) || item.id == newPlaylistItemId)
                && item.bounds.contains(pos))
                return item.id;
    }
    return -1;
}

bool SidebarComponent::isInterestedInDragSource(const SourceDetails& /*details*/)
{
    return true; // refined to playlist items in move/drop handlers
}

void SidebarComponent::itemDragEnter(const SourceDetails& details)
{
    itemDragMove(details);
}

void SidebarComponent::itemDragMove(const SourceDetails& details)
{
    const int newId = playlistItemIdAt(details.localPosition.toInt());
    if (newId != dragOverItemId_)
    {
        dragOverItemId_ = newId;
        repaint();
    }
}

void SidebarComponent::itemDragExit(const SourceDetails& /*details*/)
{
    if (dragOverItemId_ != -1)
    {
        dragOverItemId_ = -1;
        repaint();
    }
}

void SidebarComponent::itemDropped(const SourceDetails& details)
{
    const int targetId = playlistItemIdAt(details.localPosition.toInt());
    dragOverItemId_ = -1;
    repaint();

    juce::StringArray paths;
    paths.addTokens(details.description.toString(), "\n", "");
    paths.removeEmptyStrings();

    if (targetId == newPlaylistItemId)
    {
        if (!paths.isEmpty() && onNewPlaylistWithTracksRequested)
            onNewPlaylistWithTracksRequested(paths);
        return;
    }

    if (targetId < 1000) return;

    if (!paths.isEmpty() && onTracksDropped)
        onTracksDropped(targetId, paths);
}

} // namespace FoxPlayer
