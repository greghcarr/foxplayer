#pragma once

#include "audio/AudioEngine.h"
#include "audio/PlayQueue.h"
#include "analysis/AnalysisEngine.h"
#include "library/LibraryScanner.h"
#include "library/LibraryTableComponent.h"
#include "library/PlaylistStore.h"
#include "ui/TransportBar.h"
#include "ui/QueueView.h"
#include "ui/SongInfoEditor.h"
#include "ui/SidebarComponent.h"
#include "ui/AnalysisLogWindow.h"
#include "ui/AutoHideViewport.h"
#include "ui/PreferencesWindow.h"
#include <JuceHeader.h>

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

private:
    void showFolderChooser();
    void loadMusicFolder(const juce::File& folder);
    void saveMusicFolder(const juce::File& folder);
    juce::File loadSavedMusicFolder();

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

    // Library view management
    void updateTrackInLibrary(const TrackInfo& updated);
    void refreshCurrentView();
    void showSidebarItem(int sidebarId);
    void refreshSidebarPlaylists();
    void incrementPlayCount(const juce::File& file);

    // Drag-and-drop: add tracks to a playlist, with duplicate warning for single tracks.
    void handleTracksDroppedOnPlaylist(int sidebarId, const juce::StringArray& paths);

    // Returns a human-readable name for the given sidebar item (used for "Playing from:").
    juce::String sourceNameForSidebar(int sidebarId) const;

    // Wires AudioEngine callbacks to UI.
    void setupAudioEngineCallbacks();

    // juce::Timer: periodic orphan check
    void timerCallback() override;

    AudioEngine            engine_;
    PlayQueue              queue_;
    LibraryScanner         scanner_;
    AnalysisEngine         analysisEngine_;
    SidebarComponent       sidebar_;
    AutoHideViewport       sidebarViewport_;
    LibraryTableComponent  libraryTable_;
    TransportBar           transportBar_;
    QueueView              queueView_;

    juce::Label            emptyPromptLabel_;
    juce::TextButton       chooseFolderButton_ { "Choose Music Folder" };
    juce::DrawableButton   queueButton_ { "queueToggle", juce::DrawableButton::ImageFitted };

    bool                   queueVisible_    { false };
    bool                   shuffleOn_       { false };
    int                    repeatMode_      { 0 };   // 0=off, 1=repeat-all, 2=repeat-one
    int                    activeSidebarId_ { 1 };
    std::vector<TrackInfo> fullLibrary_;
    juce::File             currentMusicFolder_;
    juce::ApplicationProperties   appProperties_;
    std::unique_ptr<PlaylistStore>      playlistStore_;
    std::unique_ptr<AnalysisLogWindow>  analysisLogWindow_;
    std::unique_ptr<PreferencesWindow>  preferencesWindow_;
    juce::ApplicationCommandManager     commandManager_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace FoxPlayer
