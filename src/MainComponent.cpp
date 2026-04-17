#include "MainComponent.h"
#include "Constants.h"
#include "audio/FoxpFile.h"
#include "ui/SongInfoEditor.h"
#include <algorithm>
#include <set>

namespace FoxPlayer
{

using namespace Constants;

static constexpr juce::CommandID cmdQuit = juce::StandardApplicationCommandIDs::quit;

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

    // Pin button (always-on-top toggle). Lives in the title-bar strip on the
    // right-hand side, styled to sit naturally next to the traffic lights on
    // the opposite side of the bar.
    pinButton_.icon        = TransportButton::Icon::Pin;
    pinButton_.toggleStyle = true;
    pinButton_.onClick     = [this] { toggleAlwaysOnTop(); };
    addAndMakeVisible(pinButton_);

    // Sidebar resize handle: thin component at the right edge of the sidebar.
    sidebarDivider_.currentWidth = [this] { return sidebarWidth_; };
    sidebarDivider_.onDragged    = [this](int proposed) {
        // Minimum keeps just the icons visible; maximum caps at half window.
        constexpr int minSidebarWidth = 44;
        const int maxSidebarWidth = juce::jmax(minSidebarWidth, getWidth() / 2);
        sidebarWidth_ = juce::jlimit(minSidebarWidth, maxSidebarWidth, proposed);
        resized();
        saveSessionState();
    };
    addAndMakeVisible(sidebarDivider_);


    // Empty-state prompt
    emptyPromptLabel_.setText("No music folder selected", juce::dontSendNotification);
    emptyPromptLabel_.setFont(juce::Font(18.0f));
    emptyPromptLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    emptyPromptLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyPromptLabel_);

    // Shown while the library scanner is running and the library view is still
    // empty. Hidden as soon as the first batch of tracks arrives.
    addChildComponent(loadingIndicator_);

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

    libraryTable_.onClearInfoRequested = [this](std::vector<TrackInfo> tracks) {
        // Reset every editable/analysis field back to blank. The file itself
        // stays put; only the sidecar .foxp + in-memory metadata change, and
        // the downloaded album-art sidecar (if any) is deleted too.
        for (auto& t : tracks)
        {
            t.title       = {};
            t.artist      = {};
            t.album       = {};
            t.genre       = {};
            t.year        = {};
            t.trackNumber = 0;
            t.bpm         = 0.0;
            t.musicalKey  = {};
            t.playCount   = 0;
            t.lufs        = 0.0f;
            FoxpFile::save(t);
            AppleMusicLookup::artworkSidecarFor(t.file).deleteFile();
            updateTrackInLibrary(t);
        }
    };

    libraryTable_.onAddToQueueRequested = [this](std::vector<TrackInfo> tracks) {
        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);
        queue_.appendTracks(std::move(tracks), source);
    };

    libraryTable_.onReorderRequested = [this](juce::StringArray dragged, int targetIndex) {
        // Only meaningful when viewing a playlist; the library view's own
        // checks should prevent this from firing otherwise.
        if (activeSidebarId_ < 1000 || activeSidebarId_ >= 2000) return;
        const int storeId = activeSidebarId_ - 1000;
        const auto* pl = playlistStore_->findById(storeId);
        if (pl == nullptr) return;

        // Build the new ordering: take the existing paths, lift out the
        // dragged ones (adjusting targetIndex for any that appeared before
        // it), and insert them back at the target position.
        std::vector<juce::String> order = pl->trackPaths;
        const std::set<juce::String> draggedSet(dragged.begin(), dragged.end());

        int adjusted = targetIndex;
        for (int i = 0; i < targetIndex && i < static_cast<int>(order.size()); ++i)
            if (draggedSet.count(order[static_cast<size_t>(i)]))
                --adjusted;

        order.erase(std::remove_if(order.begin(), order.end(),
                                    [&](const juce::String& p) { return draggedSet.count(p) > 0; }),
                    order.end());

        adjusted = juce::jlimit(0, static_cast<int>(order.size()), adjusted);
        order.insert(order.begin() + adjusted, dragged.begin(), dragged.end());

        playlistStore_->setPlaylistTracks(storeId, std::move(order));
        refreshCurrentView();
    };

    libraryTable_.onAppleMusicLookupRequested = [this](std::vector<TrackInfo> tracks) {
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::QuestionIcon)
                .withTitle("Apple Music Lookup")
                .withMessage("Overwrite existing data with Apple Music lookup?")
                .withButton("Yes")
                .withButton("No")
                .withButton("Cancel")
                .withAssociatedComponent(this),
            [this, tracks](int result) {
                // Button indices: 1 = Yes, 2 = No, 3 = Cancel (also 0 = dismissed).
                if (result == 1)
                    appleMusicLookup_.enqueueAll(tracks, true);
                else if (result == 2)
                    appleMusicLookup_.enqueueAll(tracks, false);
            });
    };

    libraryTable_.onLibraryChanged = [this] {
        // Sync fullLibrary_ from the table (hidden state may have changed).
        fullLibrary_ = libraryTable_.allTracks();
        menuItemsChanged();
    };

    // Queue button (floats above transport bar)
    queueButton_.onClick = [this] { toggleQueue(); };
    queueButton_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
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
        saveSessionState();
    };
    transportBar_.onRepeatToggled = [this](int mode) {
        repeatMode_ = mode;
        saveSessionState();
    };
    transportBar_.onVolumeChanged = [this](double v) {
        if (auto* props = appProperties_.getUserSettings())
            props->setValue("volume", v);
    };

    // Restore persisted volume (defaults to 0.5 on first run).
    if (auto* props = appProperties_.getUserSettings())
        transportBar_.setInitialVolume(props->getDoubleValue("volume", 0.5));

    // Scanner callbacks
    scanner_.onBatchReady = [this](std::vector<TrackInfo> batch) {
        fullLibrary_.insert(fullLibrary_.end(), batch.begin(), batch.end());
        if (activeSidebarId_ == 1)
            libraryTable_.appendTracks(batch);
        showEmptyLibraryPrompt(false);
        loadingIndicator_.setVisible(false);
    };
    scanner_.onScanComplete = [this](int /*total*/) {
        loadingIndicator_.setVisible(false);
        refreshSidebarArtists();
        refreshSidebarAlbums();

        // Restore the persisted session (queue, sidebar view, shuffle/repeat,
        // currently-loaded track) now that fullLibrary_ is populated.
        if (!sessionRestored_)
            restoreSessionState();

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
                // Sidecar naming: ".<audiofile>.<ext>.foxp" (dot-prefixed,
                // .foxp-suffixed). To recover the audio file path, strip both
                // the leading dot from the filename and the ".foxp" suffix.
                const juce::String name = foxp.getFileName();
                if (! name.startsWith(".") || ! name.endsWithIgnoreCase(".foxp"))
                    continue;

                const juce::String audioName = name.substring(1, name.length() - 5);
                const juce::File audioFile   = foxp.getParentDirectory().getChildFile(audioName);
                if (! audioFile.existsAsFile())
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

    appleMusicLookup_.onLookupQueued = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().lookupQueued(t);
    };
    appleMusicLookup_.onLookupStarted = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().lookupStarted(t);
    };
    appleMusicLookup_.onLookupCompleted = [this](TrackInfo t, juce::String summary) {
        updateTrackInLibrary(t);
        if (analysisLogWindow_) analysisLogWindow_->log().lookupCompleted(t, summary);
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
        saveSessionState();
    };
    queue_.onIndexChanged = [this](int index) {
        queueView_.refresh(queue_);
        juce::ignoreUnused(index);
        saveSessionState();
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

    // Final session flush so the exact elapsed-seconds survives a clean quit.
    if (sessionRestored_)
    {
        saveSessionElapsed();
        saveSessionState();
        if (auto* props = appProperties_.getUserSettings())
            props->saveIfNeeded();
    }
}

void MainComponent::timerCallback()
{
    // Persist playback position periodically so a crash doesn't lose the spot.
    saveSessionElapsed();

    const size_t before = fullLibrary_.size();
    fullLibrary_.erase(
        std::remove_if(fullLibrary_.begin(), fullLibrary_.end(),
                       [](const TrackInfo& t) { return !t.file.existsAsFile(); }),
        fullLibrary_.end());

    if (fullLibrary_.size() != before)
    {
        refreshSidebarArtists();
        refreshSidebarAlbums();
        refreshCurrentView();
    }
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
    refreshSidebarArtists();
        refreshSidebarAlbums();

    // On an artist view the edit may have removed the selected artist (e.g.
    // the only track by that artist was renamed). Re-resolve the sidebar
    // selection to an artist that still exists and refresh the library view
    // so what's shown matches what's highlighted.
    if (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)
    {
        if (artistIdToName_.find(activeSidebarId_) == artistIdToName_.end())
        {
            if (!artistIdToName_.empty())
            {
                // Pick the highest id that doesn't exceed the old one (so the
                // sidebar selection naturally drops to the artist that slid
                // into the now-removed slot); fall back to the last artist.
                int replacement = artistIdToName_.rbegin()->first;
                for (const auto& [id, name] : artistIdToName_)
                    if (id <= activeSidebarId_) replacement = id;
                activeSidebarId_ = replacement;
            }
            else
            {
                activeSidebarId_ = 1;
            }
            sidebar_.setSelectedId(activeSidebarId_);
        }
        refreshCurrentView();
        updatePlayingHighlight();
    }
}

void MainComponent::updatePlayingHighlight()
{
    if (!queue_.hasCurrent())
    {
        libraryTable_.setPlayingFile({});
        return;
    }

    // Only paint the "now playing" highlight when the user is looking at the
    // view the track was started from. Switching to a different view (e.g., a
    // playlist the track happens to also appear in) should not light it up.
    const int sourceId = queue_.currentSource().sidebarId;
    if (activeSidebarId_ == sourceId)
        libraryTable_.setPlayingFile(queue_.current().file);
    else
        libraryTable_.setPlayingFile({});
}

void MainComponent::refreshCurrentView()
{
    libraryTable_.setSearchPlaceholder(sourceNameForSidebar(activeSidebarId_));
    // Pick the view mode for the current sidebar selection. The mode controls
    // whether the "#" column exists, what it shows, and whether drag-reorder
    // is allowed.
    using VM = LibraryTableComponent::ViewMode;
    VM mode = VM::Library;
    if      (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000) mode = VM::Artist;
    else if (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000) mode = VM::Album;
    else if (activeSidebarId_ >= 1000 && activeSidebarId_ < 2000) mode = VM::Playlist;
    libraryTable_.setViewMode(mode);

    if (activeSidebarId_ == 1)
    {
        libraryTable_.setTracks(fullLibrary_);
    }
    else if (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)
    {
        // Artist view: only tracks whose artist matches the selected one.
        const auto it = artistIdToName_.find(activeSidebarId_);
        if (it == artistIdToName_.end())
        {
            libraryTable_.clearTracks();
            return;
        }
        const juce::String& artist = it->second;

        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (t.artist == artist)
                tracks.push_back(t);

        libraryTable_.setTracks(tracks);
    }
    else if (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000)
    {
        // Album view: tracks matching both the artist and the album.
        const auto it = albumIdToInfo_.find(activeSidebarId_);
        if (it == albumIdToInfo_.end())
        {
            libraryTable_.clearTracks();
            return;
        }
        const auto& [artist, album] = it->second;

        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (t.album == album && t.artist == artist)
                tracks.push_back(t);

        libraryTable_.setTracks(tracks);
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
    // Entering a playlist view: default-sort by "#" ascending so rows show
    // in their natural playlist order.
    if (sidebarId >= 1000 && sidebarId < 2000)
        libraryTable_.applyPlaylistDefaultSort();
    updatePlayingHighlight();
    saveSessionState();
}

void MainComponent::refreshSidebarPlaylists()
{
    std::vector<std::pair<int, juce::String>> items;
    for (const auto& p : playlistStore_->all())
        items.push_back({ 1000 + p.id, p.name });
    sidebar_.setPlaylists(items);
}

void MainComponent::refreshSidebarAlbums()
{
    // Collect unique (artist, album) pairs using the "ARTIST - ALBUM" label
    // as the sort key so the sidebar order matches the label order.
    struct Item { juce::String artist, album, label; };
    std::vector<Item> items;
    std::set<juce::String> seen;
    for (const auto& t : fullLibrary_)
    {
        if (t.album.isEmpty()) continue;
        const juce::String label = (t.artist.isNotEmpty() ? t.artist : juce::String("Unknown Artist"))
                                 + " - " + t.album;
        if (seen.insert(label).second)
            items.push_back({ t.artist, t.album, label });
    }

    std::sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.label.compareNatural(b.label, false) < 0;
    });

    albumIdToInfo_.clear();
    std::vector<std::pair<int, juce::String>> sidebarItems;
    int id = 3000;
    for (const auto& it : items)
    {
        if (id >= 4000) break;   // keep within reserved range
        albumIdToInfo_[id] = { it.artist, it.album };
        sidebarItems.push_back({ id, it.label });
        ++id;
    }
    sidebar_.setAlbums(sidebarItems);
}

void MainComponent::refreshSidebarArtists()
{
    // Collect unique non-empty artist names, sorted case-insensitively.
    std::set<juce::String> unique;
    for (const auto& t : fullLibrary_)
        if (t.artist.isNotEmpty())
            unique.insert(t.artist);

    std::vector<juce::String> sorted(unique.begin(), unique.end());
    std::sort(sorted.begin(), sorted.end(), [](const juce::String& a, const juce::String& b) {
        // Second arg is isCaseSensitive; false mixes upper/lower case artists.
        return a.compareNatural(b, false) < 0;
    });

    artistIdToName_.clear();
    std::vector<std::pair<int, juce::String>> items;
    int id = 2000;
    for (const auto& name : sorted)
    {
        if (id >= 3000) break;   // keep within the reserved 2000..2999 range
        artistIdToName_[id] = name;
        items.push_back({ id, name });
        ++id;
    }
    sidebar_.setArtists(items);
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
    };

    engine_.onTrackFinished = [this] {
        // Count this as a play only now that the track has run to its end.
        if (queue_.hasCurrent())
            incrementPlayCount(queue_.current().file);

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

    // Queue toggle button: fixed to the bottom-right corner of the content
    // area (just above the transport bar), regardless of whether the queue
    // panel is open. When the panel opens, the button stays in the same
    // on-screen spot and its label flips from "Show queue" to "Collapse queue".
    constexpr int qbSize = 32;
    queueButton_.setBounds(getWidth() - qbSize - 6,
                           bounds.getBottom() - qbSize - 4,
                           qbSize, qbSize);

    // Sidebar on the left, with a thin draggable divider just after it.
    constexpr int dividerW = 6;
    // Make sure the stored width is clamped to the current window's allowed
    // range in case the window got smaller since the user set it.
    constexpr int minSidebarWidth = 44;
    const int maxSidebarWidth = juce::jmax(minSidebarWidth, getWidth() / 2);
    sidebarWidth_ = juce::jlimit(minSidebarWidth, maxSidebarWidth, sidebarWidth_);

    sidebarViewport_.setBounds(bounds.removeFromLeft(sidebarWidth_));
    sidebarDivider_.setBounds(bounds.removeFromLeft(dividerW));
    sidebarDivider_.toFront(false);

    // Viewport doesn't auto-size its viewed component's width, so sync it to the
    // visible area (shrunk when a vertical scrollbar is shown). layoutItems()
    // handles the height.
    const int sbVisibleW = sidebarViewport_.getMaximumVisibleWidth();
    if (sbVisibleW > 0 && sidebar_.getWidth() != sbVisibleW)
        sidebar_.setSize(sbVisibleW, juce::jmax(sidebar_.getHeight(), 1));

    // Library fills remaining space
    libraryTable_.setBounds(bounds);

    // Pin button: normally sits in the void reserved at the right end of the
    // library search bar. As the window approaches minimum height (mini player
    // territory), it slides leftward so it lands directly above the speaker
    // icon in the transport bar.
    constexpr int pinSize         = 17;
    constexpr int pinRightPad     = 7;
    constexpr int librarySearchH  = 30;   // matches LibraryTableComponent::searchBarHeight
    // X-distance from the window's right edge to the speaker icon's centre.
    // Must stay in sync with TransportBar::resized()'s pad + volAreaW geometry
    // (10pt edge pad + 22pt speaker + 6pt gap + 20pt slider, centred at
    // windowRight - 10 - 48 + 11 = windowRight - 47).
    constexpr int speakerCentreFromRight = 47;

    const int normalPinX  = libraryTable_.getRight() - pinSize - pinRightPad;
    const int compactPinX = getWidth() - speakerCentreFromRight - pinSize / 2 - 1;
    const int normalPinY  = libraryTable_.getY() + (librarySearchH - pinSize) / 2;
    const int compactPinY = normalPinY + 6;   // small downward nudge to sit closer to the speaker

    const float pinShiftT = juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(Constants::compactHeight) - static_cast<float>(getHeight()))
            / static_cast<float>(Constants::compactHeight - Constants::minWindowHeight));
    const int pinX = normalPinX
                   + static_cast<int>(pinShiftT * (compactPinX - normalPinX));
    const int pinY = normalPinY
                   + static_cast<int>(pinShiftT * (compactPinY - normalPinY));

    pinButton_.setBounds(pinX, pinY, pinSize, pinSize);
    pinButton_.toFront(false);

    // Empty state centered in library area
    emptyPromptLabel_.setBounds(bounds.withSizeKeepingCentre(400, 40));
    loadingIndicator_.setBounds(bounds.withSizeKeepingCentre(280, 40));
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
    loadingIndicator_.setVisible(true);
    resized();
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
    if (sidebarId >= 2000 && sidebarId < 3000)
    {
        const auto it = artistIdToName_.find(sidebarId);
        return it != artistIdToName_.end() ? it->second : juce::String("Artist");
    }
    if (sidebarId >= 3000 && sidebarId < 4000)
    {
        const auto it = albumIdToInfo_.find(sidebarId);
        if (it == albumIdToInfo_.end()) return "Album";
        const auto& [artist, album] = it->second;
        return (artist.isNotEmpty() ? artist : juce::String("Unknown Artist"))
             + " - " + album;
    }
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
    updatePlayingHighlight();
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

void MainComponent::toggleAlwaysOnTop()
{
    alwaysOnTop_ = !alwaysOnTop_;
    applyAlwaysOnTop();
    pinButton_.toggleState = alwaysOnTop_ ? 1 : 0;
    pinButton_.repaint();
    menuItemsChanged();
    saveSessionState();
}

void MainComponent::applyAlwaysOnTop()
{
    if (auto* win = getTopLevelComponent())
        win->setAlwaysOnTop(alwaysOnTop_);
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
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commandManager_, cmdAlwaysOnTop);
        menu.addSeparator();
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
    commands.add(cmdAlwaysOnTop);
    commands.add(cmdFocusSearch);
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

        case cmdAlwaysOnTop:
            info.setInfo("Always on Top",
                         "Keep the FoxPlayer window above all other windows",
                         "Window", 0);
            info.setTicked(alwaysOnTop_);
            info.addDefaultKeypress('p', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier);
            break;

        case cmdFocusSearch:
            info.setInfo("Find",
                         "Focus the library search box",
                         "Edit", 0);
            info.addDefaultKeypress('f', juce::ModifierKeys::commandModifier);
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

        case cmdAlwaysOnTop:
            toggleAlwaysOnTop();
            return true;

        case cmdFocusSearch:
            libraryTable_.focusSearchBox();
            return true;

        default:
            return false;
    }
}

// ----------------------------------------------------------------------------
// Session persistence
// ----------------------------------------------------------------------------

void MainComponent::saveSessionState()
{
    if (sessionRestoring_ || !sessionRestored_) return;

    auto* props = appProperties_.getUserSettings();
    if (!props) return;

    props->setValue("sessionSidebarWidth",    sidebarWidth_);
    props->setValue("sessionActiveSidebarId", activeSidebarId_);
    // For artist views the numeric id is index-based and can shift between
    // runs; persist the artist name so we can re-resolve the id on restore.
    if (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)
    {
        const auto it = artistIdToName_.find(activeSidebarId_);
        props->setValue("sessionActiveArtistName",
                        it != artistIdToName_.end() ? it->second : juce::String());
    }
    else
    {
        props->removeValue("sessionActiveArtistName");
    }
    props->setValue("sessionShuffleOn",       shuffleOn_);
    props->setValue("sessionRepeatMode",      repeatMode_);
    props->setValue("sessionAlwaysOnTop",     alwaysOnTop_);

    // Persist the queue in its original (un-shuffled) order so un-shuffle still
    // works on the restored session, plus the current track's path so we can
    // jump back to it regardless of shuffle re-randomisation.
    const auto& tracksToSave = queue_.originalTracks();
    if (!tracksToSave.empty() && queue_.hasCurrent())
    {
        juce::StringArray paths;
        for (const auto& t : tracksToSave)
            paths.add(t.file.getFullPathName());

        const auto src = queue_.currentSource();
        props->setValue("sessionQueuePaths",      paths.joinIntoString("\n"));
        props->setValue("sessionQueueSourceId",   src.sidebarId);
        props->setValue("sessionQueueSourceName", src.name);
        props->setValue("sessionQueueCurrentPath",
                        queue_.current().file.getFullPathName());
    }
    else
    {
        props->removeValue("sessionQueuePaths");
        props->removeValue("sessionQueueSourceId");
        props->removeValue("sessionQueueSourceName");
        props->removeValue("sessionQueueCurrentPath");
    }
}

void MainComponent::saveSessionElapsed()
{
    if (sessionRestoring_ || !sessionRestored_) return;
    if (auto* props = appProperties_.getUserSettings())
        props->setValue("sessionElapsedSeconds", engine_.elapsedSeconds());
}

void MainComponent::restoreSessionState()
{
    auto* props = appProperties_.getUserSettings();
    if (!props) { sessionRestored_ = true; return; }

    sessionRestoring_ = true;

    // Sidebar width (the layout clamps to the valid min/max on the next
    // resized() call, so out-of-range persisted values are harmless).
    sidebarWidth_ = props->getIntValue("sessionSidebarWidth", Constants::sidebarWidth);
    resized();

    // Sidebar view (must come before queue restore so refreshCurrentView shows
    // the right tracks, and so "Playing from: ..." lines up with the view).
    int sidebarId = props->getIntValue("sessionActiveSidebarId", 1);
    if (sidebarId >= 2000 && sidebarId < 3000)
    {
        // Re-resolve the artist id from the persisted name, since our 2000+
        // ids are index-based and shift between sessions.
        const juce::String wanted = props->getValue("sessionActiveArtistName");
        sidebarId = 1;   // fall back to All Music if the artist has vanished
        if (wanted.isNotEmpty())
        {
            for (const auto& [id, name] : artistIdToName_)
                if (name == wanted) { sidebarId = id; break; }
        }
    }
    activeSidebarId_ = sidebarId;
    sidebar_.setSelectedId(sidebarId);
    refreshCurrentView();

    // Repeat mode (shuffle is handled after the queue is loaded).
    repeatMode_ = juce::jlimit(0, 2, props->getIntValue("sessionRepeatMode", 0));
    transportBar_.setRepeatMode(repeatMode_);

    // Always-on-top pin state.
    alwaysOnTop_ = props->getBoolValue("sessionAlwaysOnTop", false);
    pinButton_.toggleState = alwaysOnTop_ ? 1 : 0;
    pinButton_.repaint();
    applyAlwaysOnTop();
    menuItemsChanged();

    // Queue tracks: resolve persisted paths against the freshly-scanned library.
    const juce::String pathsStr = props->getValue("sessionQueuePaths");
    if (pathsStr.isNotEmpty() && !fullLibrary_.empty())
    {
        juce::StringArray paths;
        paths.addTokens(pathsStr, "\n", "");

        std::vector<TrackInfo> queueTracks;
        queueTracks.reserve(static_cast<size_t>(paths.size()));
        for (const auto& p : paths)
        {
            for (const auto& t : fullLibrary_)
            {
                if (t.file.getFullPathName() == p)
                {
                    queueTracks.push_back(t);
                    break;
                }
            }
        }

        if (!queueTracks.empty())
        {
            PlayQueue::QueueSource source;
            source.sidebarId = props->getIntValue("sessionQueueSourceId", 1);
            source.name      = props->getValue("sessionQueueSourceName");
            if (source.name.isEmpty())
                source.name = sourceNameForSidebar(source.sidebarId);

            // Locate the persisted "current" track in the resolved list. Some
            // tracks may have gone missing between sessions; land on the
            // closest surviving entry (default 0).
            const juce::String currentPath = props->getValue("sessionQueueCurrentPath");
            int startIdx = 0;
            for (size_t i = 0; i < queueTracks.size(); ++i)
            {
                if (queueTracks[i].file.getFullPathName() == currentPath)
                {
                    startIdx = static_cast<int>(i);
                    break;
                }
            }

            queue_.setTracks(std::move(queueTracks), startIdx, source);

            // Re-apply shuffle on top of the restored queue. This produces a
            // fresh shuffle of the upcoming tracks while preserving the
            // original order for un-shuffle.
            shuffleOn_ = props->getBoolValue("sessionShuffleOn", false);
            transportBar_.setShuffleOn(shuffleOn_);
            if (shuffleOn_)
                queue_.shuffleRemaining();

            // Load the current track paused at the saved elapsed position.
            // User explicitly wants the app to never auto-resume playback.
            if (queue_.hasCurrent())
            {
                const auto& track = queue_.current();
                const double elapsed = props->getDoubleValue("sessionElapsedSeconds", 0.0);
                engine_.prepareTrackPaused(track, elapsed);
                transportBar_.setCurrentTrack(track);
                updatePlayingHighlight();
                const auto src = queue_.currentSource();
                transportBar_.setPlayingFrom(src.name, src.sidebarId);
                queueView_.refresh(queue_);
            }
        }
    }

    sessionRestoring_ = false;
    sessionRestored_  = true;
}

} // namespace FoxPlayer
