#pragma once

#include "Constants.h"
#include "audio/AudioEngine.h"
#include "audio/PlayQueue.h"
#include "analysis/AnalysisEngine.h"
#include "analysis/AppleMusicLookup.h"
#include "library/LibraryScanner.h"
#include "library/LibraryCache.h"
#include "library/LibraryTableComponent.h"
#include "library/PlaylistStore.h"
#include "ui/TransportBar.h"
#include "ui/QueueView.h"
#include "ui/SongInfoEditor.h"
#include "ui/SidebarComponent.h"
#include "ui/AnalysisLogWindow.h"
#include "ui/AutoHideViewport.h"
#include "ui/PreferencesWindow.h"
#include "ui/LoadingIndicator.h"
#include <JuceHeader.h>
#include <map>

namespace FoxPlayer
{

class MainComponent : public juce::Component,
                      public juce::DragAndDropContainer,
                      public juce::KeyListener,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;

    // juce::KeyListener
    bool keyPressed(const juce::KeyPress& key, juce::Component* originatingComponent) override;

    // juce::ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform(const ApplicationCommandTarget::InvocationInfo& info) override;

    // juce::MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex,
                                    const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    juce::ApplicationCommandManager& commandManager() { return commandManager_; }

    enum CommandIDs
    {
        cmdShowHidden      = 0x1002,
        cmdShowAnalysisLog = 0x1003,
        cmdPreferences     = 0x1004,
        cmdAlwaysOnTop     = 0x1005,
        cmdFocusSearch     = 0x1006,
    };

private:
    // Prompts the user for a folder and appends it to the library list.
    void showAddFolderChooser();
    // Applies the given folder list: re-scans the library, persists it.
    void setMusicFolders(std::vector<juce::File> folders);
    // Persist / load the set of music-library root folders.
    void saveMusicFolders();
    std::vector<juce::File> loadSavedMusicFolders();
    // setMusicFolders variants: the keepLibrary form skips the
    // clear-fullLibrary step (used after we've loaded the on-disk cache so
    // the cached view stays visible while a refresh scan runs in the
    // background).
    void setMusicFolders(std::vector<juce::File> folders, bool keepLibrary);

    void activateRow(int rowIndex, const std::vector<TrackInfo>& libraryTracks);
    void playCurrentQueueItem();
    void playNext();
    void playPrev();
    void toggleQueue();
    void updateQueueButtonIcon();
    void showEmptyLibraryPrompt(bool show);
    void deleteOldStyleFoxpFiles(const juce::File& folder);
    void showSongInfoEditor(const TrackInfo& track);
    void toggleAnalysisLog();
    void showPreferences();
    void toggleAlwaysOnTop();
    void applyAlwaysOnTop();

    // Library view management
    void updateTrackInLibrary(const TrackInfo& updated);
    void refreshCurrentView();
    void showSidebarItem(int sidebarId);
    // Updates the library table's "now playing" row so it only highlights the
    // track when the active view matches where playback was initiated from.
    void updatePlayingHighlight();
    // After session restore, scroll the sidebar so the restored selection is
    // vertically centred in the viewport. Falls back to the top of the list
    // when the item is too close to the top to actually centre.
    void scrollSelectedSidebarItemIntoView();
    void refreshSidebarPlaylists();
    // Recomputes the ARTISTS sidebar section from fullLibrary_. Each unique
    // artist gets an id in the [2000, 2999] range, assigned by sorted order.
    void refreshSidebarArtists();
    // Recomputes the ALBUMS sidebar section from fullLibrary_. Each unique
    // {artist, album} pair gets an id in the [3000, 3999] range, assigned
    // by sorted "[ARTIST] - [ALBUM]" order.
    void refreshSidebarAlbums();
    void incrementPlayCount(const juce::File& file);

    // Drag-and-drop: add tracks to a playlist, with duplicate warning for single tracks.
    void handleTracksDroppedOnPlaylist(int sidebarId, const juce::StringArray& paths);

    // Returns a human-readable name for the given sidebar item (used for "Playing from:").
    juce::String sourceNameForSidebar(int sidebarId) const;

    // Wires AudioEngine callbacks to UI.
    void setupAudioEngineCallbacks();

    // Session persistence. Saved state covers the queue, active sidebar view,
    // shuffle/repeat toggles, current track, and elapsed playback time.
    void saveSessionState();
    void saveSessionElapsed();
    void restoreSessionState();

    // juce::Timer: periodic orphan check
    void timerCallback() override;

    AudioEngine            engine_;
    PlayQueue              queue_;
    LibraryScanner         scanner_;
    AnalysisEngine         analysisEngine_;
    AppleMusicLookup       appleMusicLookup_;
    SidebarComponent       sidebar_;
    AutoHideViewport       sidebarViewport_;
    LibraryTableComponent  libraryTable_;
    TransportBar           transportBar_;
    QueueView              queueView_;

    juce::Label            emptyPromptLabel_;
    juce::TextButton       chooseFolderButton_ { "Choose Music Folder" };
    LoadingIndicator       loadingIndicator_;
    juce::DrawableButton   queueButton_ { "queueToggle", juce::DrawableButton::ImageFitted };
    TransportButton        pinButton_;

    // Full-bleed overlay shown while the Preferences window is open. Dims the
    // main content, blocks clicks from reaching anything beneath, and offers
    // two buttons: one to recentre the Preferences window on the main window,
    // one to close Preferences and "unlock" the main window.
    class PrefsLockOverlay : public juce::Component
    {
    public:
        std::function<void()> onRecenterPrefs;
        std::function<void()> onClosePrefs;

        PrefsLockOverlay();
        void paint(juce::Graphics& g) override;
        void resized() override;

    private:
        juce::Label      message_ { {}, "Preferences window open." };
        juce::TextButton openBtn_  { "Open Preferences" };
        juce::TextButton closeBtn_ { "Close Preferences" };
    };
    PrefsLockOverlay      prefsLockOverlay_;

    // Thin draggable component sitting at the right edge of the sidebar. Drag
    // horizontally to resize the sidebar between "icons only" and half window.
    class SidebarDivider : public juce::Component
    {
    public:
        std::function<void(int)> onDragged;     // new proposed sidebar width
        std::function<int()>     currentWidth;

        SidebarDivider()
        {
            setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        }

        void mouseDown(const juce::MouseEvent&) override
        {
            startWidth_ = currentWidth ? currentWidth() : 0;
        }

        void mouseDrag(const juce::MouseEvent& e) override
        {
            if (onDragged)
                onDragged(startWidth_ + e.getDistanceFromDragStartX());
        }

    private:
        int startWidth_ { 0 };
    };
    SidebarDivider        sidebarDivider_;

    bool                   queueVisible_    { false };
    bool                   shuffleOn_       { false };
    int                    repeatMode_      { 0 };   // 0=off, 1=repeat-all, 2=repeat-one
    bool                   alwaysOnTop_     { false };
    int                    activeSidebarId_ { 1 };
    int                    sidebarWidth_    { Constants::sidebarWidth };
    std::vector<TrackInfo> fullLibrary_;
    // While true, scan results accumulate into scanBuffer_ instead of
    // fullLibrary_. At scan complete, fullLibrary_ is swapped with the
    // buffer and the UI is refreshed. Used when fullLibrary_ was already
    // populated from the on-disk cache and we don't want to wipe the visible
    // library while a confirmation scan runs in the background.
    bool                   scanReplacingCachedLibrary_ { false };
    std::vector<TrackInfo> scanBuffer_;
    // sidebarId (2000..2999) -> artist name, rebuilt by refreshSidebarArtists().
    std::map<int, juce::String> artistIdToName_;
    struct AlbumKey { juce::String artist; juce::String album; };
    // sidebarId (3000..3999) -> {artist, album}, rebuilt by refreshSidebarAlbums().
    std::map<int, AlbumKey> albumIdToInfo_;
    std::vector<juce::File> musicFolders_;
    juce::ApplicationProperties   appProperties_;
    // Set while restoreSessionState is running so change callbacks don't try
    // to re-save the half-applied state.
    bool                   sessionRestoring_ { false };
    // Set once restoreSessionState has completed so saveSessionState becomes
    // a no-op until then (prevents the empty startup state from clobbering
    // the persisted session before we've had a chance to read it back).
    bool                   sessionRestored_  { false };

    std::unique_ptr<PlaylistStore>      playlistStore_;
    std::unique_ptr<AnalysisLogWindow>  analysisLogWindow_;
    std::unique_ptr<PreferencesWindow>  preferencesWindow_;
    juce::ApplicationCommandManager     commandManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace FoxPlayer
