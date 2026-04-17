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
    playlistIconDrawable_ = loadSvg(BinaryData::listbulletsfill_svg, BinaryData::listbulletsfill_svgSize);
    artistIconDrawable_   = loadSvg(BinaryData::userfill_svg,        BinaryData::userfill_svgSize);
    albumIconDrawable_    = loadSvg(BinaryData::vinylrecordfill_svg, BinaryData::vinylrecordfill_svgSize);

    // LIBRARY section: not collapsible
    Section library;
    library.heading       = "LIBRARY";
    library.collapsible   = false;
    library.rightClickable = false;
    library.items.push_back({ "All Music", 1, {} });
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

    // PLAYLISTS section: collapsible, right-clickable for "Create New Playlist"
    Section playlists;
    playlists.heading        = "PLAYLISTS";
    playlists.collapsible    = true;
    playlists.collapsed      = false;
    playlists.rightClickable = true;
    sections_.push_back(std::move(playlists));
}

void SidebarComponent::setPlaylists(const std::vector<std::pair<int, juce::String>>& playlists)
{
    auto& section = sections_[3]; // PLAYLISTS is always index 3
    section.items.clear();
    for (const auto& [id, name] : playlists)
        section.items.push_back({ name, id, {} });
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
    int y = 8;
    for (auto& section : sections_)
    {
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

        y += 6; // gap between sections
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
        // Right-pointing triangle
        tri.addTriangle(x, cy - size * 0.5f,
                        x, cy + size * 0.5f,
                        x + size, cy);
    }
    else
    {
        // Down-pointing triangle
        tri.addTriangle(x, cy - size * 0.4f,
                        x + size, cy - size * 0.4f,
                        x + size * 0.5f, cy + size * 0.5f);
    }
    g.setColour(Color::textDim);
    g.fillPath(tri);
}

void SidebarComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::headerBackground);

    // Right border
    g.setColour(Color::border);
    g.drawVerticalLine(getWidth() - 1, 0.0f, static_cast<float>(getHeight()));

    for (const auto& section : sections_)
    {
        // Section heading
        const juce::Font headingFont(juce::FontOptions().withHeight(12.0f));
        g.setColour(Color::textDim);
        g.setFont(headingFont);
        g.drawText(section.heading,
                   itemPadL, section.headerBounds.getY(),
                   section.headerBounds.getWidth() - itemPadL - 20,
                   section.headerBounds.getHeight(),
                   juce::Justification::centredLeft, false);

        if (section.collapsible)
        {
            // Measure the heading so the triangle sits just to the right of it.
            juce::GlyphArrangement ga;
            ga.addLineOfText(headingFont, section.heading, 0.0f, 0.0f);
            const float textW = ga.getBoundingBox(0, -1, true).getWidth();
            const float triX  = static_cast<float>(itemPadL) + textW + 6.0f;
            drawDisclosureTriangle(g, triX, section.headerBounds.getCentreY(), section.collapsed);
        }

        if (!section.collapsed)
        {
            for (const auto& item : section.items)
            {
                const bool selected = (item.id == selectedId_);
                auto rowBounds = item.bounds;

                // Drop target highlight
                if (item.id == dragOverItemId_)
                {
                    g.setColour(Color::accent.withAlpha(0.25f));
                    g.fillRect(rowBounds);
                }

                // Row background
                if (selected)
                {
                    g.setColour(Color::background);
                    g.fillRect(rowBounds);
                }

                // Left accent bar for selected
                if (selected)
                {
                    g.setColour(Color::accent);
                    g.fillRect(rowBounds.withWidth(indicatorW));
                }

                // Icon: music notes for All Music, list-bullets for playlists.
                constexpr int iconDim = 16;
                constexpr int iconGap = 8;
                // 1 = All Music, 2000..2999 = individual artists, 3000..3999
                // = individual albums, else = playlists.
                juce::Drawable* iconDrawable =
                    (item.id == 1)                      ? musicIconDrawable_.get()
                  : (item.id >= 2000 && item.id < 3000) ? artistIconDrawable_.get()
                  : (item.id >= 3000 && item.id < 4000) ? albumIconDrawable_.get()
                  :                                       playlistIconDrawable_.get();
                const int iconX = rowBounds.getX() + itemPadL + indicatorW;
                const int iconY = rowBounds.getY() + (rowBounds.getHeight() - iconDim) / 2;
                if (iconDrawable)
                {
                    auto tinted = iconDrawable->createCopy();
                    tinted->replaceColour(juce::Colours::black,
                                          selected ? Color::textPrimary : Color::textSecondary);
                    tinted->drawWithin(g,
                                       juce::Rectangle<float>(static_cast<float>(iconX),
                                                              static_cast<float>(iconY),
                                                              static_cast<float>(iconDim),
                                                              static_cast<float>(iconDim)),
                                       juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                                       1.0f);
                }

                // Label
                const int labelX = iconX + iconDim + iconGap;
                g.setColour(selected ? Color::textPrimary : Color::textSecondary);
                g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
                g.drawText(item.label,
                           labelX,
                           rowBounds.getY(),
                           rowBounds.getRight() - labelX - 8,
                           rowBounds.getHeight(),
                           juce::Justification::centredLeft, true);
            }
        }
    }
}

void SidebarComponent::resized()
{
    layoutItems();
}

void SidebarComponent::mouseDown(const juce::MouseEvent& e)
{
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
                    if (e.mods.isPopupMenu() && item.id >= 1000)
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
            if (item.id >= 1000 && item.bounds.contains(pos))
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

    if (targetId < 1000) return;

    juce::StringArray paths;
    paths.addTokens(details.description.toString(), "\n", "");
    paths.removeEmptyStrings();

    if (!paths.isEmpty() && onTracksDropped)
        onTracksDropped(targetId, paths);
}

} // namespace FoxPlayer
