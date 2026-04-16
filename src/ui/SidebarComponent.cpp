#include "SidebarComponent.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

SidebarComponent::SidebarComponent()
{
    // LIBRARY section: not collapsible
    Section library;
    library.heading       = "LIBRARY";
    library.collapsible   = false;
    library.rightClickable = false;
    library.items.push_back({ juce::String(juce::CharPointer_UTF8("\xe2\x99\xab   All Music")), 1, {} });
    sections_.push_back(std::move(library));

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
    auto& section = sections_[1]; // PLAYLISTS is always index 1
    section.items.clear();
    const juce::String icon(juce::CharPointer_UTF8("\xf0\x9f\x93\x84   ")); // 📄 + 3 spaces
    for (const auto& [id, name] : playlists)
        section.items.push_back({ icon + name, id, {} });
    layoutItems();
    repaint();
}

void SidebarComponent::setSelectedId(int id)
{
    selectedId_ = id;
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
                                               juce::Rectangle<int> hdr,
                                               bool isCollapsed) const
{
    const float size = 6.0f;
    const float x    = static_cast<float>(hdr.getRight()) - 16.0f;
    const float cy   = static_cast<float>(hdr.getCentreY());

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
        g.setColour(Color::textDim);
        g.setFont(juce::Font(juce::FontOptions().withHeight(10.0f)));
        g.drawText(section.heading,
                   itemPadL, section.headerBounds.getY(),
                   section.headerBounds.getWidth() - itemPadL - 20,
                   section.headerBounds.getHeight(),
                   juce::Justification::centredLeft, false);

        if (section.collapsible)
            drawDisclosureTriangle(g, section.headerBounds, section.collapsed);

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

                // Label
                g.setColour(selected ? Color::textPrimary : Color::textSecondary);
                g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
                g.drawText(item.label,
                           rowBounds.getX() + itemPadL + indicatorW,
                           rowBounds.getY(),
                           rowBounds.getWidth() - itemPadL - indicatorW - 8,
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
    const juce::String icon(juce::CharPointer_UTF8("\xf0\x9f\x93\x84   "));

    for (const auto& section : sections_)
        for (const auto& item : section.items)
            if (item.id == sidebarId)
            {
                rowBounds  = item.bounds;
                plainName  = item.label.substring(icon.length());
                break;
            }

    if (rowBounds.isEmpty()) return;

    editingItemId_       = sidebarId;
    editingOriginalName_ = plainName;

    const juce::Font f(juce::FontOptions().withHeight(13.0f));
    juce::GlyphArrangement ga;
    ga.addLineOfText(f, icon, 0.0f, 0.0f);
    const int iconW = static_cast<int>(ga.getBoundingBox(0, -1, true).getWidth());
    const int edX   = rowBounds.getX() + itemPadL + indicatorW + iconW;
    const int edW   = rowBounds.getRight() - edX - 8;

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
