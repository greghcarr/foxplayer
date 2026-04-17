#pragma once

#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class SidebarComponent : public juce::Component,
                         public juce::DragAndDropTarget
{
public:
    SidebarComponent();

    // id=1 for Music; id = 1000 + playlistStoreId for playlists.
    std::function<void(int id)> onItemSelected;
    std::function<void()>       onCreatePlaylistRequested;

    // Called when tracks are dropped onto a playlist item.
    // sidebarId is the playlist's sidebar ID (1000 + storeId).
    // paths is a newline-separated list of file paths.
    std::function<void(int sidebarId, juce::StringArray paths)> onTracksDropped;

    std::function<void(int sidebarId, juce::String newName)> onRenamePlaylist;
    std::function<void(int sidebarId)>                       onDeletePlaylist;

    int  selectedId() const { return selectedId_; }
    void setSelectedId(int id);

    // Replaces the items shown under the Playlists section.
    // Each pair is { sidebarId, displayName }.
    void setPlaylists(const std::vector<std::pair<int, juce::String>>& playlists);

    // Replaces the items shown under the Artists section.
    // Each pair is { sidebarId, displayName }.
    void setArtists(const std::vector<std::pair<int, juce::String>>& artists);

    // Replaces the items shown under the Albums section.
    // Each pair is { sidebarId, displayName }.
    void setAlbums(const std::vector<std::pair<int, juce::String>>& albums);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // juce::DragAndDropTarget
    bool isInterestedInDragSource(const SourceDetails& details) override;
    void itemDragEnter(const SourceDetails& details) override;
    void itemDragMove(const SourceDetails& details) override;
    void itemDragExit(const SourceDetails& details) override;
    void itemDropped(const SourceDetails& details) override;

private:
    struct Item
    {
        juce::String         label;
        int                  id { 0 };
        juce::Rectangle<int> bounds;
    };

    struct Section
    {
        juce::String         heading;
        bool                 collapsible    { false };
        bool                 collapsed      { false };
        bool                 rightClickable { false };
        std::vector<Item>    items;
        juce::Rectangle<int> headerBounds;
    };

    void layoutItems();
    void selectId(int id);
    void drawDisclosureTriangle(juce::Graphics& g,
                                float x, int centreY,
                                bool collapsed) const;

    // Returns the sidebar ID of the playlist item at pos, or -1 if none.
    int playlistItemIdAt(juce::Point<int> pos) const;

    void startRename(int sidebarId);
    void commitRename();
    void cancelRename();

    std::vector<Section> sections_;
    int                  selectedId_     { 1 };
    int                  dragOverItemId_ { -1 };
    int                  editingItemId_  { -1 };
    juce::String         editingOriginalName_;
    std::unique_ptr<juce::TextEditor> inlineEditor_;

    // Pre-loaded SVG drawables for row icons. Tinted lazily per-paint.
    std::unique_ptr<juce::Drawable> musicIconDrawable_;
    std::unique_ptr<juce::Drawable> playlistIconDrawable_;
    std::unique_ptr<juce::Drawable> artistIconDrawable_;
    std::unique_ptr<juce::Drawable> albumIconDrawable_;

    static constexpr int sectionHeaderH = 30;
    static constexpr int itemH          = 36;
    static constexpr int indicatorW     = 3;
    static constexpr int itemPadL       = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SidebarComponent)
};

} // namespace FoxPlayer
