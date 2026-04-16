#include "MainComponent.h"
#include "Constants.h"
#include "audio/FoxpFile.h"
#include "ui/SongInfoEditor.h"

namespace FoxPlayer
{

using namespace Constants;

enum CommandIDs
{
    cmdChooseFolder    = 0x1001,
    cmdShowHidden      = 0x1002,
    cmdShowAnalysisLog = 0x1003,
    cmdPreferences     = 0x1004,
    cmdQuit            = juce::StandardApplicationCommandIDs::quit,
};

static constexpr int orphanCheckIntervalMs = 30'000;

MainComponent::MainComponent()
    : transportBar_(engine_)
{
    // ApplicationProperties for persisting settings.
    juce::PropertiesFile::Options opts;
    opts.applicationName = "FoxPlayer";
    opts.filenameSuffix  = ".settings";
    opts.osxLibrarySubFolder = "Application Support";
    appProperties_.setStorageParameters(opts);

    playlistStore_ = std::make_unique<PlaylistStore>(appProperties_);
    playlistStore_->onPlaylistsChanged = [this] {
        refreshSidebarPlaylists();
        // A rename to the currently-open playlist should update the search placeholder.
        if (activeSidebarId_ >= 1000)
            libraryTable_.setSearchPlaceholder(sourceNameForSidebar(activeSidebarId_));
    };

    // Commands
    commandManager_.registerAllCommandsForTarget(this);
    addKeyListener(commandManager_.getKeyMappings());

    setSize(defaultWindowWidth, defaultWindowHeight);

    // Sub-components
    sidebarViewport_.setViewedComponent(&sidebar_, false);
    addAndMakeVisible(sidebarViewport_);
    addAndMakeVisible(libraryTable_);
    addAndMakeVisible(transportBar_);
    addAndMakeVisible(queueView_);
    addChildComponent(queueButton_);  // hidden until the queue has tracks

    // Empty-state prompt
    emptyPromptLabel_.setText("No music folder selected", juce::dontSendNotification);
    emptyPromptLabel_.setFont(juce::Font(18.0f));
    emptyPromptLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    emptyPromptLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyPromptLabel_);

    chooseFolderButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    chooseFolderButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    chooseFolderButton_.onClick = [this] { showFolderChooser(); };
    addChildComponent(chooseFolderButton_);

    // Library table callbacks
    libraryTable_.onRowActivated = [this](int rowIndex) {
        activateRow(rowIndex, libraryTable_.visibleTracks());
    };
    libraryTable_.onAnalyzeRequested = [this](std::vector<TrackInfo> tracks) {
        analysisEngine_.enqueueAll(tracks);
    };

    libraryTable_.onEditRequested = [this](TrackInfo track) {
        showSongInfoEditor(track);
    };

    libraryTable_.onAddToQueueRequested = [this](std::vector<TrackInfo> tracks) {
        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);
        queue_.appendTracks(std::move(tracks), source);
    };

    libraryTable_.onLibraryChanged = [this] {
        // Sync fullLibrary_ from the table (hidden state may have changed).
        fullLibrary_ = libraryTable_.allTracks();
        menuItemsChanged();
    };

    // Queue button (floats above transport bar)
    queueButton_.onClick = [this] { toggleQueue(); };
    updateQueueButtonIcon();

    // Transport bar callbacks
    transportBar_.onPrevClicked         = [this] { playPrev(); };
    transportBar_.onNextClicked         = [this] { playNext(); };
    transportBar_.onChangeFolderClicked = [this] { showFolderChooser(); };
    transportBar_.onPlayingFromClicked  = [this](int sidebarId) {
        sidebar_.setSelectedId(sidebarId);
        showSidebarItem(sidebarId);
    };
    transportBar_.onShuffleToggled = [this](bool on) {
        shuffleOn_ = on;
        if (on) queue_.shuffleRemaining();
        else    queue_.unshuffleRemaining();
    };
    transportBar_.onRepeatToggled = [this](int mode) {
        repeatMode_ = mode;
    };

    // Scanner callbacks
    scanner_.onBatchReady = [this](std::vector<TrackInfo> batch) {
        fullLibrary_.insert(fullLibrary_.end(), batch.begin(), batch.end());
        if (activeSidebarId_ == 1)
            libraryTable_.appendTracks(batch);
        showEmptyLibraryPrompt(false);
    };
    scanner_.onScanComplete = [this](int /*total*/) {
        // Orphan-cleanup walks the entire music folder deleting any dot-prefixed
        // .foxp sidecar whose audio file no longer exists. It's pure background
        // housekeeping, so run it fire-and-forget on its own thread rather than
        // blocking shutdown or the message thread.
        const juce::File folder = currentMusicFolder_;
        juce::Thread::launch([folder]() {
            if (!folder.isDirectory()) return;

            juce::Array<juce::File> foxpFiles;
            folder.findChildFiles(foxpFiles, juce::File::findFiles, true, "*.foxp");

            for (const auto& foxp : foxpFiles)
            {
                const juce::String audioPath = foxp.getFullPathName().dropLastCharacters(5);
                if (! juce::File(audioPath).existsAsFile())
                    foxp.deleteFile();
            }
        });
    };

    // Analysis log window (created hidden; shown via Window menu).
    analysisLogWindow_ = std::make_unique<AnalysisLogWindow>();
    preferencesWindow_ = std::make_unique<PreferencesWindow>(engine_.deviceManager());

    // Analysis callbacks - feed both the library and the log window.
    analysisEngine_.onTrackQueued = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().trackQueued(t);
    };
    analysisEngine_.onTrackStarted = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().trackStarted(t);
    };
    analysisEngine_.onTrackAnalysed = [this](TrackInfo analysed) {
        updateTrackInLibrary(analysed);
        if (analysisLogWindow_) analysisLogWindow_->log().trackAnalysed(analysed);
    };

    // Queue callbacks
    queue_.onQueueChanged = [this] {
        queueView_.refresh(queue_);

        // Hide the queue button (and the panel itself) when the queue is empty.
        const bool hasQueue = queue_.size() > 0;
        queueButton_.setVisible(hasQueue);
        if (!hasQueue && queueVisible_)
        {
            queueVisible_ = false;
            queueView_.setVisible(false);
            updateQueueButtonIcon();
            resized();
        }
    };
    queue_.onIndexChanged = [this](int index) {
        queueView_.refresh(queue_);
        juce::ignoreUnused(index);
    };
    // Reset shuffle button when a new queue replaces the old one.
    queue_.onShuffleStateChanged = [this](bool on) {
        shuffleOn_ = on;
        transportBar_.setShuffleOn(on);
    };

    // Queue view double-click
    queueView_.onRowActivated = [this](int queueIndex) {
        if (queue_.jumpTo(queueIndex))
            playCurrentQueueItem();
    };

    // Sidebar callbacks
    sidebar_.onItemSelected = [this](int id) { showSidebarItem(id); };
    sidebar_.onCreatePlaylistRequested = [this] {
        playlistStore_->createPlaylist("New Playlist");
    };
    sidebar_.onTracksDropped = [this](int sidebarId, juce::StringArray paths) {
        handleTracksDroppedOnPlaylist(sidebarId, paths);
    };
    sidebar_.onRenamePlaylist = [this](int sidebarId, juce::String newName) {
        playlistStore_->renamePlaylist(sidebarId - 1000, newName);
    };
    sidebar_.onDeletePlaylist = [this](int sidebarId) {
        playlistStore_->deletePlaylist(sidebarId - 1000);
        if (activeSidebarId_ == sidebarId)
        {
            activeSidebarId_ = 1;
            sidebar_.setSelectedId(1);
            refreshCurrentView();
        }
    };
    refreshSidebarPlaylists();

    setupAudioEngineCallbacks();

    // Restore saved folder or show prompt.
    const auto savedFolder = loadSavedMusicFolder();
    if (savedFolder.isDirectory())
        loadMusicFolder(savedFolder);
    else
        showEmptyLibraryPrompt(true);

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    startTimer(orphanCheckIntervalMs);
}

MainComponent::~MainComponent()
{
    stopTimer();
    removeKeyListener(this);
}

void MainComponent::timerCallback()
{
    const size_t before = fullLibrary_.size();
    fullLibrary_.erase(
        std::remove_if(fullLibrary_.begin(), fullLibrary_.end(),
                       [](const TrackInfo& t) { return !t.file.existsAsFile(); }),
        fullLibrary_.end());

    if (fullLibrary_.size() != before)
        refreshCurrentView();
}

void MainComponent::updateTrackInLibrary(const TrackInfo& updated)
{
    for (auto& t : fullLibrary_)
    {
        if (t.file == updated.file)
        {
            t = updated;
            break;
        }
    }
    libraryTable_.updateTrack(updated);
}

void MainComponent::refreshCurrentView()
{
    libraryTable_.setSearchPlaceholder(sourceNameForSidebar(activeSidebarId_));

    if (activeSidebarId_ == 1)
    {
        libraryTable_.setTracks(fullLibrary_);
    }
    else
    {
        const int storeId = activeSidebarId_ - 1000;
        const auto* pl = playlistStore_->findById(storeId);
        if (!pl) { libraryTable_.clearTracks(); return; }

        std::vector<TrackInfo> tracks;
        for (const auto& path : pl->trackPaths)
            for (const auto& t : fullLibrary_)
                if (t.file.getFullPathName() == path)
                    { tracks.push_back(t); break; }

        libraryTable_.setTracks(tracks);
    }
}

void MainComponent::showSidebarItem(int sidebarId)
{
    activeSidebarId_ = sidebarId;
    refreshCurrentView();
}

void MainComponent::refreshSidebarPlaylists()
{
    std::vector<std::pair<int, juce::String>> items;
    for (const auto& p : playlistStore_->all())
        items.push_back({ 1000 + p.id, p.name });
    sidebar_.setPlaylists(items);
}

void MainComponent::handleTracksDroppedOnPlaylist(int sidebarId,
                                                   const juce::StringArray& paths)
{
    const int storeId = sidebarId - 1000;
    const auto* pl = playlistStore_->findById(storeId);
    if (!pl) return;

    // Single-track duplicate warning.
    if (paths.size() == 1)
    {
        const juce::String path = paths[0];
        if (playlistStore_->containsTrack(storeId, path))
        {
            juce::String trackName = juce::File(path).getFileNameWithoutExtension();
            for (const auto& t : fullLibrary_)
                if (t.file.getFullPathName() == path)
                    { trackName = t.displayTitle(); break; }

            const juce::String msg = "\"" + trackName + "\" is already a part of \""
                                     + pl->name + "\".";

            const int capturedId = storeId;
            juce::AlertWindow::showAsync(
                juce::MessageBoxOptions()
                    .withIconType(juce::MessageBoxIconType::QuestionIcon)
                    .withTitle("Duplicate Track")
                    .withMessage(msg)
                    .withButton("Add anyway")
                    .withButton("No")
                    .withAssociatedComponent(this),
                [this, capturedId, path](int result) {
                    if (result == 1) // "Add anyway"
                        playlistStore_->addTracksToPlaylist(capturedId, { path });
                });
            return;
        }
    }

    // No duplicate (or multi-track drop): add directly.
    std::vector<juce::String> pathVec(paths.begin(), paths.end());
    playlistStore_->addTracksToPlaylist(storeId, pathVec);
}

void MainComponent::incrementPlayCount(const juce::File& file)
{
    for (auto& t : fullLibrary_)
    {
        if (t.file == file)
        {
            ++t.playCount;
            FoxpFile::save(t);
            libraryTable_.updateTrack(t);
            return;
        }
    }
}

void MainComponent::showSongInfoEditor(const TrackInfo& track)
{
    auto* editor = new SongInfoEditor(track);

    editor->onSave = [this](TrackInfo updated) {
        FoxpFile::save(updated);
        updateTrackInLibrary(updated);
    };

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned(editor);
    opts.dialogTitle           = "Edit Song Info";
    opts.dialogBackgroundColour = juce::Colour(0xff1e1e1e);
    opts.useNativeTitleBar     = true;
    opts.resizable             = false;
    opts.launchAsync();
}

void MainComponent::setupAudioEngineCallbacks()
{
    engine_.onTrackStarted = [this](const TrackInfo& track) {
        transportBar_.setCurrentTrack(track);
        queueView_.refresh(queue_);
        incrementPlayCount(track.file);
    };

    engine_.onTrackFinished = [this] {
        if (repeatMode_ == 2)
        {
            // Repeat-one: replay the current track without advancing.
            playCurrentQueueItem();
        }
        else if (queue_.hasNext())
        {
            queue_.advanceToNext();
            playCurrentQueueItem();
        }
        else if (repeatMode_ == 1)
        {
            // Repeat-all: wrap back to the start of the queue.
            queue_.jumpTo(0);
            playCurrentQueueItem();
        }
        else
        {
            transportBar_.clearTrack();
            libraryTable_.setPlayingFile({});
        }
    };

    engine_.onStateChanged = [this] {
        transportBar_.repaint();
    };
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    // Transport bar at the bottom
    transportBar_.setBounds(bounds.removeFromBottom(transportBarHeight));

    // Queue panel on the right (only when visible)
    if (queueVisible_)
        queueView_.setBounds(bounds.removeFromRight(queuePanelWidth));

    // Queue toggle button: bottom-right of content area, just above transport bar
    constexpr int qbSize = 32;
    queueButton_.setBounds(bounds.getRight() - qbSize - 6,
                           bounds.getBottom() - qbSize - 4,
                           qbSize, qbSize);

    // Sidebar on the left
    sidebarViewport_.setBounds(bounds.removeFromLeft(sidebarWidth));

    // Viewport doesn't auto-size its viewed component's width, so sync it to the
    // visible area (shrunk when a vertical scrollbar is shown). layoutItems()
    // handles the height.
    const int sbVisibleW = sidebarViewport_.getMaximumVisibleWidth();
    if (sbVisibleW > 0 && sidebar_.getWidth() != sbVisibleW)
        sidebar_.setSize(sbVisibleW, juce::jmax(sidebar_.getHeight(), 1));

    // Library fills remaining space
    libraryTable_.setBounds(bounds);

    // Empty state centered in library area
    emptyPromptLabel_.setBounds(bounds.withSizeKeepingCentre(400, 40));
    chooseFolderButton_.setBounds(bounds.withSizeKeepingCentre(200, 36)
                                        .translated(0, 50));
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        if (engine_.isPlaying())       engine_.pause();
        else if (engine_.isPaused())   engine_.resume();
        return true;
    }
    return false;
}

void MainComponent::showFolderChooser()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select your music folder",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;
            const auto folder = results[0];
            saveMusicFolder(folder);
            loadMusicFolder(folder);
        });
}

void MainComponent::loadMusicFolder(const juce::File& folder)
{
    currentMusicFolder_ = folder;
    fullLibrary_.clear();
    activeSidebarId_ = 1;
    sidebar_.setSelectedId(1);
    deleteOldStyleFoxpFiles(folder);
    libraryTable_.clearTracks();
    showEmptyLibraryPrompt(false);
    scanner_.scanFolder(folder);
}

void MainComponent::deleteOldStyleFoxpFiles(const juce::File& folder)
{
    juce::Array<juce::File> found;
    folder.findChildFiles(found, juce::File::findFiles, true, "*.foxp");

    for (const auto& f : found)
    {
        // Old-style files do not start with '.'; new-style ones do.
        if (!f.getFileName().startsWith("."))
            f.deleteFile();
    }
}

void MainComponent::saveMusicFolder(const juce::File& folder)
{
    if (auto* props = appProperties_.getUserSettings())
        props->setValue("musicFolder", folder.getFullPathName());
}

juce::File MainComponent::loadSavedMusicFolder()
{
    if (auto* props = appProperties_.getUserSettings())
    {
        const auto path = props->getValue("musicFolder");
        if (path.isNotEmpty())
            return juce::File(path);
    }
    return {};
}

juce::String MainComponent::sourceNameForSidebar(int sidebarId) const
{
    if (sidebarId == 1)
        return "All Music";
    const auto* pl = playlistStore_->findById(sidebarId - 1000);
    return pl ? pl->name : "Playlist";
}

void MainComponent::activateRow(int rowIndex, const std::vector<TrackInfo>& libraryTracks)
{
    // Capture shuffle state before setTracks resets it via onShuffleStateChanged.
    const bool wasShuffled = shuffleOn_;

    std::vector<TrackInfo> queueTracks(libraryTracks.begin() + rowIndex, libraryTracks.end());

    PlayQueue::QueueSource source;
    source.sidebarId = activeSidebarId_;
    source.name      = sourceNameForSidebar(activeSidebarId_);

    queue_.setTracks(std::move(queueTracks), 0, source);

    // Re-apply shuffle immediately so the new queue is shuffled from the start.
    if (wasShuffled)
    {
        shuffleOn_ = true;
        transportBar_.setShuffleOn(true);
        queue_.shuffleRemaining();
    }

    playCurrentQueueItem();
}

void MainComponent::playCurrentQueueItem()
{
    if (!queue_.hasCurrent()) return;
    const auto& track = queue_.current();
    engine_.play(track);
    transportBar_.setCurrentTrack(track);
    libraryTable_.setPlayingFile(track.file);
    const auto src = queue_.currentSource();
    transportBar_.setPlayingFrom(src.name, src.sidebarId);
    queueView_.refresh(queue_);
}

void MainComponent::playNext()
{
    if (queue_.hasNext())
    {
        queue_.advanceToNext();
        playCurrentQueueItem();
    }
}

void MainComponent::playPrev()
{
    if (engine_.elapsedSeconds() > 3.0)
    {
        engine_.seekToNormalized(0.0);
    }
    else if (queue_.hasPrev())
    {
        queue_.retreatToPrev();
        playCurrentQueueItem();
    }
}

void MainComponent::toggleQueue()
{
    queueVisible_ = !queueVisible_;
    queueView_.setVisible(queueVisible_);
    updateQueueButtonIcon();
    resized();
}

void MainComponent::updateQueueButtonIcon()
{
    const char* data = queueVisible_ ? BinaryData::caretdoublerightfill_svg
                                     : BinaryData::queuefill_svg;
    const int   size = queueVisible_ ? BinaryData::caretdoublerightfill_svgSize
                                     : BinaryData::queuefill_svgSize;

    const auto xmlStr = juce::String::createStringFromData(data, size);
    if (auto xml = juce::XmlDocument::parse(xmlStr))
        if (auto drawable = juce::Drawable::createFromSVG(*xml))
        {
            drawable->replaceColour(juce::Colours::black, Color::textSecondary);
            queueButton_.setImages(drawable.get());
            queueButton_.setTooltip(queueVisible_ ? "Hide Queue" : "Show Queue");
        }
}

void MainComponent::toggleAnalysisLog()
{
    if (!analysisLogWindow_) return;

    const bool shouldShow = !analysisLogWindow_->isVisible();
    analysisLogWindow_->setVisible(shouldShow);
    if (shouldShow)
        analysisLogWindow_->toFront(true);

    menuItemsChanged();
}

void MainComponent::showPreferences()
{
    if (!preferencesWindow_) return;
    preferencesWindow_->setVisible(true);
    preferencesWindow_->toFront(true);
}

void MainComponent::showEmptyLibraryPrompt(bool show)
{
    emptyPromptLabel_.setVisible(show);
    chooseFolderButton_.setVisible(show);
}

// juce::MenuBarModel
juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Window" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int index, const juce::String& /*menuName*/)
{
    juce::PopupMenu menu;
    if (index == 0)
    {
        menu.addCommandItem(&commandManager_, cmdChooseFolder);
        menu.addCommandItem(&commandManager_, cmdShowHidden);
        menu.addSeparator();
        menu.addCommandItem(&commandManager_, cmdPreferences);
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commandManager_, cmdShowAnalysisLog);
    }
    return menu;
}

void MainComponent::menuItemSelected(int /*menuItemID*/, int /*topLevelMenuIndex*/) {}

// juce::ApplicationCommandTarget
void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.add(cmdChooseFolder);
    commands.add(cmdShowHidden);
    commands.add(cmdShowAnalysisLog);
    commands.add(cmdPreferences);
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    switch (id)
    {
        case cmdChooseFolder:
            info.setInfo("Choose Music Folder...", "Select the folder containing your music",
                         "File", 0);
            info.addDefaultKeypress('o', juce::ModifierKeys::commandModifier);
            break;

        case cmdShowHidden:
            info.setInfo("Show Hidden Songs in Library",
                         "Show hidden songs dimmed in the library",
                         "File", 0);
            info.setTicked(libraryTable_.showHidden());
            info.addDefaultKeypress('.', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier);
            break;

        case cmdShowAnalysisLog:
            info.setInfo("Show Analysis Log",
                         "Open the analysis log window",
                         "Window", 0);
            info.setTicked(analysisLogWindow_ && analysisLogWindow_->isVisible());
            info.addDefaultKeypress('l', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier);
            break;

        case cmdPreferences:
            info.setInfo("Preferences...",
                         "Open the FoxPlayer preferences window",
                         "File", 0);
            info.addDefaultKeypress(',', juce::ModifierKeys::commandModifier);
            break;

        default: break;
    }
}

bool MainComponent::perform(const ApplicationCommandTarget::InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdChooseFolder:
            showFolderChooser();
            return true;

        case cmdShowHidden:
            libraryTable_.setShowHidden(!libraryTable_.showHidden());
            menuItemsChanged();
            return true;

        case cmdShowAnalysisLog:
            toggleAnalysisLog();
            return true;

        case cmdPreferences:
            showPreferences();
            return true;

        default:
            return false;
    }
}

} // namespace FoxPlayer
