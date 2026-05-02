#include "MainComponent.h"
#include "Constants.h"
#include "audio/StylFile.h"
#include "ui/SongInfoEditor.h"
#include <algorithm>
#include <set>

namespace Stylus
{

using namespace Constants;

static constexpr int orphanCheckIntervalMs = 30'000;

// Global LookAndFeel: pointer cursor on TextButton, light-red hover on "Quit".
class StylusLnF : public juce::LookAndFeel_V4
{
public:
    juce::MouseCursor getMouseCursorFor(juce::Component& c) override
    {
        if (dynamic_cast<juce::TextButton*>(&c) != nullptr)
            return juce::MouseCursor::PointingHandCursor;
        return LookAndFeel_V4::getMouseCursorFor(c);
    }

    void drawButtonBackground(juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        if (button.getButtonText() == "Quit" && (highlighted || down))
        {
            auto bounds = button.getLocalBounds().toFloat().reduced(0.5f, 0.5f);
            g.setColour(down ? juce::Colour(0xff5a1818) : juce::Colour(0xff8a2222));
            g.fillRoundedRectangle(bounds, 3.0f);
            return;
        }
        LookAndFeel_V4::drawButtonBackground(g, button, backgroundColour, highlighted, down);
    }
};

MainComponent::MainComponent()
    : transportBar_(engine_)
{
    appLnF_ = std::make_unique<StylusLnF>();
    juce::LookAndFeel::setDefaultLookAndFeel(appLnF_.get());

    // ApplicationProperties for persisting settings.
    juce::PropertiesFile::Options opts;
    opts.applicationName = "Stylus";
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

    // Commands. MainComponent is the permanent fallback target so File/Window
    // menu items stay enabled even when focus is on a child window (Analysis
    // Log, Preferences, Edit Info dialog) whose parent chain doesn't lead back
    // here.
    commandManager_.registerAllCommandsForTarget(this);
    commandManager_.setFirstCommandTarget(this);
    addKeyListener(commandManager_.getKeyMappings());

    setSize(defaultWindowWidth, defaultWindowHeight);

    // Sub-components
    libraryTable_.setAppProperties(&appProperties_);
    sidebarViewport_.setViewedComponent(&sidebar_, false);
    sidebarViewport_.getVerticalScrollBar().setColour(juce::ScrollBar::thumbColourId,      Color::scrollbarThumb);
    sidebarViewport_.getVerticalScrollBar().setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    sidebarViewport_.getVerticalScrollBar().setColour(juce::ScrollBar::trackColourId,      juce::Colours::transparentBlack);
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

    // Queue resize handle: thin component at the LEFT edge of the queue panel.
    // Dragging right shrinks the queue, dragging left grows it (dragSign = -1).
    queueDivider_.dragSign     = -1;
    queueDivider_.currentWidth = [this] { return queueWidth_; };
    queueDivider_.onDragged    = [this](int proposed) {
        // Min matches the sidebar's minimum; max caps at 40% of window width.
        constexpr int minQueueWidth = 44;
        const int maxQueueWidth = juce::jmax(minQueueWidth, (getWidth() * 2) / 5);
        queueWidth_ = juce::jlimit(minQueueWidth, maxQueueWidth, proposed);
        resized();
        saveSessionState();
    };
    addChildComponent(queueDivider_);  // hidden until queue is shown


    // Empty-state prompt for music (shown when no music folders are configured).
    emptyPromptLabel_.setText("No music folder selected", juce::dontSendNotification);
    emptyPromptLabel_.setFont(juce::Font(18.0f));
    emptyPromptLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    emptyPromptLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(emptyPromptLabel_);

    chooseFolderButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    chooseFolderButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    chooseFolderButton_.onClick = [this] { showPreferencesLibrary(); };
    addChildComponent(chooseFolderButton_);

    // Empty-state prompt for podcasts (shown when no podcast folders are configured).
    podcastPromptLabel_.setText("No podcasts folder selected", juce::dontSendNotification);
    podcastPromptLabel_.setFont(juce::Font(18.0f));
    podcastPromptLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    podcastPromptLabel_.setJustificationType(juce::Justification::centred);
    addChildComponent(podcastPromptLabel_);

    podcastFolderButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    podcastFolderButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    podcastFolderButton_.onClick = [this] { showPreferencesLibrary(); };
    addChildComponent(podcastFolderButton_);

    // Shown while the library scanner is running and the library view is still
    // empty. Hidden as soon as the first batch of tracks arrives.
    addChildComponent(loadingIndicator_);

    // Library table callbacks
    libraryTable_.onRowActivated = [this](int rowIndex) {
        activateRow(rowIndex, libraryTable_.visibleTracks());
    };
    libraryTable_.onInlineEditCommitted = [this](const TrackInfo& updated) {
        updateTrackInLibrary(updated);
    };
    libraryTable_.onAnalyzeRequested = [this](std::vector<TrackInfo> tracks) {
        analysisEngine_.enqueueAll(tracks);
    };

    libraryTable_.onEditRequested = [this](TrackInfo track, std::vector<TrackInfo> peerList, int peerIndex) {
        showSongInfoEditor(track, std::move(peerList), peerIndex);
    };

    libraryTable_.onMultiEditRequested = [this](std::vector<TrackInfo> tracks) {
        showMultiInfoEditor(tracks);
    };

    libraryTable_.onPodcastLookupRequested = [](std::vector<TrackInfo> tracks) {
        if (tracks.empty()) return;
        const juce::String query = tracks.front().podcast.isNotEmpty()
            ? tracks.front().podcast
            : tracks.front().displayTitle();
        juce::URL("https://podcastindex.org/search?q="
                  + juce::URL::addEscapeChars(query, true))
            .launchInDefaultBrowser();
    };

    libraryTable_.onClearInfoRequested = [this](std::vector<TrackInfo> tracks) {
        // Reset every editable/analysis field back to blank. The file itself
        // stays put; only the sidecar .styl + in-memory metadata change, and
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
            StylFile::save(t);
            AppleMusicLookup::artworkSidecarFor(t.file).deleteFile();
            updateTrackInLibrary(t);
        }
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_);
    };

    libraryTable_.onGoToArtistRequested = [this](TrackInfo t) {
        for (const auto& [id, name] : artistIdToName_)
            if (name == t.artist)
            {
                sidebar_.setSelectedId(id); showSidebarItem(id);
                juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
                return;
            }
    };
    libraryTable_.onGoToAlbumRequested = [this](TrackInfo t) {
        for (const auto& [id, info] : albumIdToInfo_)
            if (info.artist == t.artist && info.album == t.album)
            {
                sidebar_.setSelectedId(id); showSidebarItem(id);
                juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
                return;
            }
    };
    libraryTable_.onGoToPodcastRequested = [this](TrackInfo t) {
        for (const auto& [id, name] : podcastIdToName_)
            if (name == t.podcast)
            {
                sidebar_.setSelectedId(id); showSidebarItem(id);
                juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
                return;
            }
    };

    libraryTable_.onAddToQueueRequested = [this](std::vector<TrackInfo> tracks) {
        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);
        queue_.appendTracks(std::move(tracks), source);
    };

    libraryTable_.onPlayNextRequested = [this](std::vector<TrackInfo> tracks) {
        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);
        queue_.insertAfterCurrent(std::move(tracks), source);
    };

    libraryTable_.onRemoveFromPlaylistRequested = [this](std::vector<TrackInfo> tracks) {
        if (activeSidebarId_ < 1000 || activeSidebarId_ >= 2000) return;
        const int storeId = activeSidebarId_ - 1000;
        const auto* pl = playlistStore_->findById(storeId);
        if (pl == nullptr) return;

        std::set<juce::String> toRemove;
        for (const auto& t : tracks)
            toRemove.insert(t.file.getFullPathName());

        std::vector<juce::String> remaining = pl->trackPaths;
        remaining.erase(std::remove_if(remaining.begin(), remaining.end(),
                                       [&](const juce::String& p) { return toRemove.count(p) > 0; }),
                        remaining.end());
        playlistStore_->setPlaylistTracks(storeId, std::move(remaining));
        refreshCurrentView();
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
                if (result == 1 || result == 2) {
                    for (const auto& t : tracks)
                        lookupUndoSnapshots_[t.file] = t;
                    appleMusicLookup_.enqueueAll(tracks, result == 1);
                }
            });
    };

    libraryTable_.isLookupUndoable = [this](const juce::File& file) {
        return lookupUndoSnapshots_.count(file) > 0;
    };

    libraryTable_.onAppleMusicUndoRequested = [this](const TrackInfo& track) {
        auto it = lookupUndoSnapshots_.find(track.file);
        if (it == lookupUndoSnapshots_.end()) return;
        TrackInfo snapshot = it->second;
        lookupUndoSnapshots_.erase(it);
        StylFile::save(snapshot);
        updateTrackInLibrary(snapshot);
    };

    libraryTable_.onAlbumArtLookupRequested = [this](std::vector<TrackInfo> tracks) {
        for (const auto& t : tracks)
        {
            const juce::File artFile = AppleMusicLookup::artworkSidecarFor(t.file);
            ArtUndoData snap;
            snap.hadArt = artFile.existsAsFile();
            if (snap.hadArt) artFile.loadFileAsData(snap.data);
            artUndoSnapshots_[t.file] = std::move(snap);
            artOnlyLookupFiles_.insert(t.file);
        }
        appleMusicLookup_.enqueueAllArtOnly(tracks);
    };

    libraryTable_.isArtLookupUndoable = [this](const juce::File& file) {
        return artUndoSnapshots_.count(file) > 0;
    };

    libraryTable_.onAlbumArtUndoRequested = [this](const TrackInfo& track) {
        auto it = artUndoSnapshots_.find(track.file);
        if (it == artUndoSnapshots_.end()) return;
        const juce::File artFile = AppleMusicLookup::artworkSidecarFor(track.file);
        if (it->second.hadArt && it->second.data.getSize() > 0)
            artFile.replaceWithData(it->second.data.getData(), it->second.data.getSize());
        else
            artFile.deleteFile();
        artUndoSnapshots_.erase(it);
        transportBar_.refreshAlbumArt();
    };

    libraryTable_.onSelectionChanged = [this] {
        // User-driven selection in the active view: any selection saved for
        // other views becomes stale and should not reappear when navigating
        // back. Saved selection for the current view is rebuilt on the next
        // view switch from libraryTable_.selectedFiles().
        for (auto it = savedSelectionByView_.begin(); it != savedSelectionByView_.end();)
            it = (it->first == activeSidebarId_) ? std::next(it)
                                                 : savedSelectionByView_.erase(it);
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
    transportBar_.onChangeFolderClicked = [this] { showAddFolderChooser(); };
    transportBar_.onPlayingFromClicked  = [this](int sidebarId) {
        sidebar_.setSelectedId(sidebarId);
        showSidebarItem(sidebarId);
        juce::MessageManager::callAsync([this] {
            scrollSelectedSidebarItemIntoView();
            libraryTable_.selectAndScrollToPlayingRow();
        });
    };
    transportBar_.onTitleClicked = [this](int sidebarId) {
        sidebar_.setSelectedId(sidebarId);
        showSidebarItem(sidebarId);
        juce::MessageManager::callAsync([this] {
            scrollSelectedSidebarItemIntoView();
            libraryTable_.selectAndScrollToPlayingRow();
        });
    };
    transportBar_.onArtistClicked = [this](TrackInfo t) {
        if (t.isPodcast)
        {
            for (const auto& [id, name] : podcastIdToName_)
                if (name == t.podcast)
                {
                    sidebar_.setSelectedId(id);
                    showSidebarItem(id);
                    juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
                    return;
                }
        }
        else if (t.artist.isNotEmpty())
        {
            for (const auto& [id, name] : artistIdToName_)
                if (name == t.artist)
                {
                    sidebar_.setSelectedId(id);
                    showSidebarItem(id);
                    juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
                    return;
                }
        }
    };
    transportBar_.onShuffleToggled = [this](bool on) {
        shuffleOn_ = on;
        if (on)
            queue_.shuffleAll();
        else
            queue_.unshuffleRemaining();
        // After a shuffle/unshuffle the playing track has likely moved within
        // the queue; centre it in the panel rather than letting the default
        // scrollToEnsureRowIsOnscreen leave it pinned to the bottom edge.
        queueView_.centerPlayingRow();
        saveSessionState();
    };
    transportBar_.onRepeatToggled = [this](int mode) {
        repeatMode_ = mode;
        saveSessionState();
    };
    transportBar_.onVolumeChanged = [this](double v) {
        if (auto* props = appProperties_.getUserSettings())
        {
            props->setValue("volume", v);
            props->saveIfNeeded();
            DBG("Saved volume " + juce::String(v) + " to "
                + props->getFile().getFullPathName());
        }
        else
        {
            DBG("onVolumeChanged: getUserSettings() returned null");
        }
    };
    transportBar_.onMuteChanged = [this](bool muted, double premuteVol) {
        if (auto* props = appProperties_.getUserSettings())
        {
            props->setValue("muted", muted);
            props->setValue("premuteVolume", premuteVol);
            props->saveIfNeeded();
        }
    };

    // Restore persisted volume and mute state (defaults to 0.5 unmuted on first run).
    if (auto* props = appProperties_.getUserSettings())
    {
        transportBar_.setInitialVolume(props->getDoubleValue("volume", 0.5));
        if (props->getBoolValue("muted", false))
            transportBar_.setInitialMute(true, props->getDoubleValue("premuteVolume", 0.5));
    }

    // Scanner callbacks
    scanner_.onBatchReady = [this](std::vector<TrackInfo> batch) {
        if (scanReplacingCachedLibrary_)
        {
            // Cached library is being shown; collect into a buffer instead so
            // we don't disturb the visible state until the scan finishes.
            scanBuffer_.insert(scanBuffer_.end(), batch.begin(), batch.end());
            return;
        }

        fullLibrary_.insert(fullLibrary_.end(), batch.begin(), batch.end());

        // Only append tracks that belong in the current view. Podcast tracks
        // must not appear in All Music and vice versa.
        if (activeSidebarId_ == 1)
        {
            std::vector<TrackInfo> musicOnly;
            for (const auto& t : batch)
                if (!t.isPodcast) musicOnly.push_back(t);
            if (!musicOnly.empty())
                libraryTable_.appendTracks(musicOnly);
        }
        else if (activeSidebarId_ == 2)
        {
            std::vector<TrackInfo> podcastOnly;
            for (const auto& t : batch)
                if (t.isPodcast) podcastOnly.push_back(t);
            if (!podcastOnly.empty())
                libraryTable_.appendTracks(podcastOnly);
        }

        showEmptyLibraryPrompt(false);
        libraryTable_.setSuppressEmptyLabel(true);
        loadingIndicator_.setVisible(false);
        // Refresh Artists/Albums/Podcasts on every batch so they fill in
        // incrementally during the scan instead of staying empty until the end.
        refreshSidebarArtists();
        refreshSidebarAlbums();
        refreshSidebarGenres();
        refreshSidebarPodcasts();
    };
    scanner_.onScanComplete = [this](int total) {
        DBG("onScanComplete total=" + juce::String(total)
            + " sessionRestored=" + juce::String((int) sessionRestored_)
            + " fullLibrary.size=" + juce::String((int) fullLibrary_.size())
            + " replacingCache=" + juce::String((int) scanReplacingCachedLibrary_));

        // If this scan was confirming a cached library, swap the fresh
        // results in now and refresh dependent views.
        // Re-read every .styl sidecar after the swap: the scan batch was built
        // before any Apple Music lookups or analyses completed during the scan
        // run, so their .styl writes would otherwise be silently overwritten.
        if (scanReplacingCachedLibrary_)
        {
            fullLibrary_ = std::move(scanBuffer_);
            scanBuffer_.clear();
            scanReplacingCachedLibrary_ = false;
            for (auto& t : fullLibrary_)
                StylFile::load(t);
        }

        loadingIndicator_.setVisible(false);
        sidebar_.setLibraryLoading(false);
        libraryTable_.setSuppressEmptyLabel(emptyPromptLabel_.isVisible() || podcastPromptLabel_.isVisible());
        refreshSidebarArtists();
        refreshSidebarAlbums();
        refreshSidebarGenres();
        refreshSidebarPodcasts();
        // Always refresh the visible table after a complete scan so the
        // isPodcast split is applied regardless of which scan path was used.
        refreshCurrentView();

        // Restore the persisted session (queue, sidebar view, shuffle/repeat,
        // currently-loaded track) now that fullLibrary_ is populated.
        if (!sessionRestored_)
            restoreSessionState();

        // Persist the freshly-scanned library so the next launch can populate
        // fullLibrary_ instantly from disk before doing its own background scan.
        // Wrapped in try/catch so a write error never breaks the scan flow.
        try
        {
            const bool ok = LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_);
            DBG("LibraryCache::save -> " + juce::String((int) ok));
        }
        catch (...)
        {
            DBG("LibraryCache::save threw");
        }

        // Background housekeeping: rewrite sidecars for any track whose
        // isPodcast classification changed (clears stale music metadata from
        // podcast sidecars, and stale podcast fields from music sidecars), then
        // delete orphaned sidecars for audio files that no longer exist.
        std::vector<juce::File> allFolders = musicFolders_;
        allFolders.insert(allFolders.end(), podcastFolders_.begin(), podcastFolders_.end());
        std::vector<TrackInfo> librarySnapshot = fullLibrary_;
        juce::Thread::launch([folders    = std::move(allFolders),
                              snapshot   = std::move(librarySnapshot)]() {
            // Rewrite the sidecar for each track that has an existing one, so
            // stale fields from a prior classification are removed.
            for (const auto& track : snapshot)
                if (StylFile::exists(track))
                    StylFile::save(track);

            // Delete orphaned sidecars whose audio file no longer exists.
            for (const auto& folder : folders)
            {
                if (!folder.isDirectory()) continue;

                juce::Array<juce::File> sidecars;
                folder.findChildFiles(sidecars, juce::File::findFiles, true, "*.styl");

                for (const auto& sc : sidecars)
                {
                    const juce::String name = sc.getFileName();
                    if (! name.startsWith(".") || ! name.endsWithIgnoreCase(".styl"))
                        continue;

                    // Strip leading "." and trailing ".styl" to recover the audio file name.
                    const juce::String audioName = name.substring(1, name.length() - 5);
                    const juce::File audioFile   = sc.getParentDirectory().getChildFile(audioName);
                    if (! audioFile.existsAsFile())
                        sc.deleteFile();
                }
            }
        });
    };

    // Analysis log window (created hidden; shown via Window menu).
    analysisLogWindow_ = std::make_unique<AnalysisLogWindow>();
    preferencesWindow_ = std::make_unique<PreferencesWindow>(engine_.deviceManager(), appProperties_);
    preferencesWindow_->onClosed = [this] {
        // Unlock the main window.
        prefsLockOverlay_.setVisible(false);
        if (preferencesWindow_) preferencesWindow_->setAlwaysOnTop(false);
        // Re-enable the Preferences menu item now the window is dismissed.
        menuItemsChanged();
    };

    // Overlay buttons shown while Preferences is open.
    prefsLockOverlay_.onRecenterPrefs = [this] {
        if (! preferencesWindow_) return;
        // Recentre the Preferences window on whatever part of the screen the
        // main window currently occupies, so it's impossible to miss.
        const auto target = getScreenBounds().getCentre();
        preferencesWindow_->setCentrePosition(target);
        preferencesWindow_->toFront(true);
    };
    prefsLockOverlay_.onClosePrefs = [this] {
        if (preferencesWindow_) preferencesWindow_->closeButtonPressed();
    };
    addChildComponent(prefsLockOverlay_);

    editInfoLockOverlay_.onOpenEditInfo = [this] {
        if (auto* w = activeEditInfoWindow_.getComponent())
        {
            w->setCentrePosition(getScreenBounds().getCentre());
            w->toFront(true);
        }
    };
    editInfoLockOverlay_.onCloseEditInfo = [this] {
        if (auto* w = activeEditInfoWindow_.getComponent())
        {
            libraryTable_.scrollToFile(lastEditedInfoFile_);
            activeEditInfoWindow_ = nullptr;
            editInfoLockOverlay_.setVisible(false);
            resized();
            juce::MessageManager::callAsync([w] { delete w; });
        }
    };
    addChildComponent(editInfoLockOverlay_);

    quitLockOverlay_.onCancelQuit = [this] {
        if (auto* dlg = activeQuitDialog_.getComponent())
        {
            activeQuitDialog_ = nullptr;
            quitLockOverlay_.setVisible(false);
            resized();
            delete dlg;
        }
    };
    addChildComponent(quitLockOverlay_);

    // Wire the Library panel in Preferences to the live folder list.
    if (auto* libPanel = preferencesWindow_->libraryPanel())
    {
        libPanel->onFoldersChanged = [this](std::vector<juce::File> folders) {
            setMusicFolders(std::move(folders));
            if (auto* p = preferencesWindow_->libraryPanel())
                p->setFolders(musicFolders_);
        };
    }

    // Wire the podcast folder section inside the Library panel.
    if (auto* libPanel = preferencesWindow_->libraryPanel())
    {
        libPanel->onPodcastFoldersChanged = [this](std::vector<juce::File> folders) {
            setPodcastFolders(std::move(folders));
            if (auto* p = preferencesWindow_->libraryPanel())
                p->setPodcastFolders(podcastFolders_);
        };
    }

    // Rescan buttons. Both trigger the same keepLibrary rescan, the scanner
    // processes music and podcast folders in a single pass.
    if (auto* libPanel = preferencesWindow_->libraryPanel())
    {
        libPanel->onRescanMusicFolders   = [this] { setMusicFolders(musicFolders_, /*keepLibrary*/ true); };
        libPanel->onRescanPodcastFolders = [this] { setMusicFolders(musicFolders_, /*keepLibrary*/ true); };
    }

    // Analysis callbacks - feed both the library and the log window.
    analysisEngine_.onTrackQueued = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().trackQueued(t);
    };
    analysisEngine_.onTrackStarted = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().trackStarted(t);
    };
    analysisEngine_.onTrackAnalysed = [this](TrackInfo analysed) {
        // The TrackInfo emitted by AnalysisEngine is a snapshot from when
        // analysis was queued. Title/artist/album/etc. on it may be stale if
        // the user edited the track meanwhile. Merge ONLY the analysis fields
        // into our live state to avoid silent reverts.
        for (auto& t : fullLibrary_)
        {
            if (t.file == analysed.file)
            {
                t.bpm        = analysed.bpm;
                t.musicalKey = analysed.musicalKey;
                t.lufs       = analysed.lufs;
                libraryTable_.updateTrack(t);
                break;
            }
        }
        if (analysisLogWindow_) analysisLogWindow_->log().trackAnalysed(analysed);
    };

    appleMusicLookup_.onLookupQueued = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().lookupQueued(t);
    };
    appleMusicLookup_.onLookupStarted = [this](TrackInfo t) {
        if (analysisLogWindow_) analysisLogWindow_->log().lookupStarted(t);
    };
    appleMusicLookup_.onLookupCompleted = [this](TrackInfo t, juce::String summary, bool isBatch) {
        // Single lookups follow the track; batch lookups update silently so
        // the user's selection isn't yanked from row to row as each completes.
        updateTrackInLibrary(t, /*followTrack*/ ! isBatch);
        transportBar_.refreshAlbumArt();
        if (analysisLogWindow_) analysisLogWindow_->log().lookupCompleted(t, summary);

        const bool isEditorLookup = (editorLookupCallback_ && t.file == editorLookupFile_);
        if (isEditorLookup)
        {
            auto cb = std::move(editorLookupCallback_);
            editorLookupFile_ = juce::File();
            cb(summary.startsWith("Found"), t);

            // After a successful editor-triggered lookup, follow the track to
            // its new sorted position. Without this, the TableListBox would
            // keep its row index and a different track would appear selected.
            if (summary.startsWith("Found"))
            {
                juce::MessageManager::callAsync([this, file = t.file] {
                    libraryTable_.scrollToFile(file);
                });
            }
        }

        const bool isArtOnly = artOnlyLookupFiles_.count(t.file) > 0;
        if (isArtOnly) artOnlyLookupFiles_.erase(t.file);

        if (!summary.startsWith("Found") && !isEditorLookup)
            lookupUndoSnapshots_.erase(t.file);
        if (!summary.startsWith("Found") && isArtOnly)
            artUndoSnapshots_.erase(t.file);

        if (summary == "Network error" && ! appleMusicLookup_.isSuspended())
        {
            // Collect the failed track for a retry after a short delay.
            // The overwrite flag isn't available here, so default to false
            // (don't clobber existing data on retry).
            pendingRetryLookups_.push_back({ t, false });

            if (! retryScheduled_)
            {
                retryScheduled_ = true;
                juce::Timer::callAfterDelay(30000, [this] {
                    retryScheduled_ = false;
                    if (pendingRetryLookups_.empty() || appleMusicLookup_.isSuspended())
                    {
                        pendingRetryLookups_.clear();
                        return;
                    }
                    std::vector<TrackInfo> tracks;
                    for (const auto& j : pendingRetryLookups_)
                        tracks.push_back(j.track);
                    pendingRetryLookups_.clear();
                    appleMusicLookup_.enqueueAll(tracks, false);
                });
            }

            if (! isBatch)
            {
                const juce::String trackName = t.title.isNotEmpty() ? t.title
                                             : t.file.getFileNameWithoutExtension();
                juce::AlertWindow::showAsync(
                    juce::MessageBoxOptions()
                        .withIconType(juce::MessageBoxIconType::WarningIcon)
                        .withTitle("Apple Music Lookup Failed")
                        .withMessage("Could not reach the Apple Music server for \""
                                     + trackName + "\".\n\n"
                                     "The lookup has been rescheduled and will retry automatically.")
                        .withButton("OK")
                        .withAssociatedComponent(this),
                    nullptr);
            }
        }
    };

    appleMusicLookup_.onLookupSuspended = [this] {
        pendingRetryLookups_.clear();
        retryScheduled_ = false;
        juce::AlertWindow::showAsync(
            juce::MessageBoxOptions()
                .withIconType(juce::MessageBoxIconType::WarningIcon)
                .withTitle("Apple Music Lookup Paused")
                .withMessage(juce::String("Apple Music lookups have failed ")
                             + juce::String(AppleMusicLookup::maxConsecutiveFailures)
                             + " times in a row, likely due to rate limiting.\n\n"
                             "Stylus has stopped retrying to avoid further errors. "
                             "Try again later by right-clicking a track and choosing "
                             "\"Look up on Apple Music\".")
                .withButton("OK")
                .withAssociatedComponent(this),
            nullptr);
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
            queueDivider_.setVisible(false);
            updateQueueButtonIcon();
            resized();
        }
        updateNavButtons();
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

    queueView_.onRemoveTracks = [this](std::vector<int> queueIndices) {
        queue_.removeAt(queueIndices);
    };

    queueView_.onTracksDropped = [this](juce::StringArray paths, int insertIndex) {
        std::vector<TrackInfo> tracks;
        tracks.reserve(static_cast<size_t>(paths.size()));
        for (const auto& p : paths)
        {
            const juce::File f(p);
            for (const auto& t : fullLibrary_)
                if (t.file == f) { tracks.push_back(t); break; }
        }
        if (tracks.empty()) return;

        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);

        if (insertIndex < 0)
            queue_.appendTracks(std::move(tracks), source);
        else
            queue_.insertAt(insertIndex, std::move(tracks), source);
    };

    // Sidebar callbacks
    sidebar_.onItemSelected = [this](int id) { showSidebarItem(id); };
    sidebar_.onCreatePlaylistRequested = [this] {
        playlistStore_->createPlaylist("New Playlist");
    };
    sidebar_.onNewPlaylistWithTracksRequested = [this](juce::StringArray paths) {
        const int newId = playlistStore_->createPlaylist("New Playlist");
        std::vector<juce::String> pathVec(paths.begin(), paths.end());
        playlistStore_->addTracksToPlaylist(newId, pathVec);
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
    sidebar_.onDuplicatePlaylist = [this](int sidebarId) {
        const auto* pl = playlistStore_->findById(sidebarId - 1000);
        if (!pl) return;
        const int newId = playlistStore_->createPlaylist(pl->name + " copy");
        playlistStore_->addTracksToPlaylist(newId, pl->trackPaths);
    };
    sidebar_.onPlaylistsReordered = [this](std::vector<int> newOrder) {
        std::vector<int> storeIds;
        storeIds.reserve(newOrder.size());
        for (int sidebarId : newOrder)
            storeIds.push_back(sidebarId - 1000);
        playlistStore_->reorderPlaylists(storeIds);
    };
    sidebar_.onCreatePlaylistFromItem = [this](int sidebarId, juce::String name) {
        auto tracks = getTracksForSidebar(sidebarId);
        if (tracks.empty()) return;

        // Album view: sort by track number (unnumbered tracks last), then title.
        if (sidebarId >= 3000 && sidebarId < 4000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNum = a.trackNumber > 0;
                const bool bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
            });
        }
        // Artist view: unnumbered-album tracks first (alpha by title), then
        // albums in alphabetical order, each album sorted by track number then title.
        else if (sidebarId >= 2000 && sidebarId < 3000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNoAlbum = a.album.isEmpty();
                const bool bNoAlbum = b.album.isEmpty();
                if (aNoAlbum != bNoAlbum) return aNoAlbum > bNoAlbum;
                if (aNoAlbum)
                    return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
                const int albumCmp = a.album.compareIgnoreCase(b.album);
                if (albumCmp != 0) return albumCmp < 0;
                const bool aNum = a.trackNumber > 0;
                const bool bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
            });
        }

        const int newId = playlistStore_->createPlaylist(name);
        std::vector<juce::String> paths;
        paths.reserve(tracks.size());
        for (const auto& t : tracks) paths.push_back(t.file.getFullPathName());
        playlistStore_->addTracksToPlaylist(newId, paths);
    };

    auto sortTracksForSidebar = [](std::vector<TrackInfo>& tracks, int sidebarId) {
        if (sidebarId >= 3000 && sidebarId < 4000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNum = a.trackNumber > 0, bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
            });
        }
        else if (sidebarId >= 2000 && sidebarId < 3000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNoAlbum = a.album.isEmpty(), bNoAlbum = b.album.isEmpty();
                if (aNoAlbum != bNoAlbum) return aNoAlbum > bNoAlbum;
                if (aNoAlbum) return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
                const int albumCmp = a.album.compareIgnoreCase(b.album);
                if (albumCmp != 0) return albumCmp < 0;
                const bool aNum = a.trackNumber > 0, bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareIgnoreCase(b.displayTitle()) < 0;
            });
        }
    };

    sidebar_.onPlayNextFromItem = [this, sortTracksForSidebar](int sidebarId) {
        auto tracks = getTracksForSidebar(sidebarId);
        if (tracks.empty()) return;
        sortTracksForSidebar(tracks, sidebarId);
        queue_.insertAfterCurrent(std::move(tracks), { sourceNameForSidebar(sidebarId), sidebarId });
    };

    sidebar_.onAddToQueueFromItem = [this, sortTracksForSidebar](int sidebarId) {
        auto tracks = getTracksForSidebar(sidebarId);
        if (tracks.empty()) return;
        sortTracksForSidebar(tracks, sidebarId);
        queue_.appendTracks(std::move(tracks), { sourceNameForSidebar(sidebarId), sidebarId });
    };

    refreshSidebarPlaylists();

    setupAudioEngineCallbacks();

    // Restore saved podcast folders.
    podcastFolders_ = loadSavedPodcastFolders();
    if (auto* libPanel = preferencesWindow_->libraryPanel())
        libPanel->setPodcastFolders(podcastFolders_);

    // Restore saved music folders (or show the empty prompt if none).
    auto savedFolders = loadSavedMusicFolders();

    if (! savedFolders.empty())
    {
        // Try the on-disk library cache first so the UI populates instantly
        // (sidebar / library table / queue restore) without waiting for the
        // background scan to finish. The scan still runs to pick up any
        // changes since the cache was written.
        std::vector<TrackInfo>   cachedTracks;
        std::vector<juce::File>  cachedFolders;
        std::vector<juce::File>  cachedPodcastFolders;
        bool cacheUsed = false;
        try
        {
            if (LibraryCache::tryLoad(cachedTracks, cachedFolders, cachedPodcastFolders)
                && cachedFolders == savedFolders)
            {
                DBG("LibraryCache loaded "
                    + juce::String((int) cachedTracks.size()) + " tracks");
                fullLibrary_ = std::move(cachedTracks);
                libraryTable_.setTracks(fullLibrary_);
                refreshSidebarArtists();
                refreshSidebarAlbums();
                refreshSidebarPodcasts();
                if (! sessionRestored_)
                    restoreSessionState();
                cacheUsed = true;
            }
        }
        catch (...)
        {
            DBG("LibraryCache::tryLoad threw, ignoring cache");
        }
        // If the cache was used, keep the cached library visible while a
        // confirmation scan runs in the background; otherwise do a normal
        // clear-and-scan.
        setMusicFolders(std::move(savedFolders), /*keepLibrary*/ cacheUsed);
    }
    else
    {
        showEmptyLibraryPrompt(true);
    }

    setWantsKeyboardFocus(true);
    addKeyListener(this);

    startTimer(orphanCheckIntervalMs);
}

MainComponent::~MainComponent()
{
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    DBG("MainComponent destructor begin (sessionRestored=" + juce::String((int) sessionRestored_) + ")");
    stopTimer();
    removeKeyListener(this);

    // Final session flush so the exact elapsed-seconds survives a clean quit.
    if (sessionRestored_)
    {
        saveSessionElapsed();
        saveSessionState();
    }
    // Always flush whatever state we have so changes never get stranded in
    // memory at quit time.
    if (auto* props = appProperties_.getUserSettings())
    {
        if (props->getBoolValue("debug.deleteSidecarsOnShutdown", false))
            deleteStylFilesInLibrary();

        const bool ok = props->save();
        DBG("Destructor save -> " + props->getFile().getFullPathName()
            + (ok ? " (save ok)" : " (save FAILED)"));
    }
    else
    {
        DBG("Destructor: getUserSettings() returned null");
    }
}

void MainComponent::checkFolderAccessibility()
{
    juce::StringArray errors;
    for (const auto& f : musicFolders_)
    {
        if (!f.isDirectory())
            errors.add("Not found: " + f.getFullPathName());
        else if (!f.hasReadAccess())
            errors.add("No read access: " + f.getFullPathName());
    }

    if (errors == lastFolderErrors_) return;

    const bool wasError = !lastFolderErrors_.isEmpty();
    const bool isError  = !errors.isEmpty();
    lastFolderErrors_ = errors;

    sidebar_.setLibraryErrors(errors);

    // A folder that was missing and is now accessible, trigger a rescan.
    if (wasError && !isError)
        setMusicFolders(musicFolders_, /*keepLibrary=*/true);
}

void MainComponent::timerCallback()
{
    checkFolderAccessibility();

    // Persist playback position periodically so a crash doesn't lose the spot.
    saveSessionElapsed();

    // Keep Now Playing metadata in sync.
    const double pos = engine_.elapsedSeconds();
    nowPlaying_.setPlaybackState(engine_.isPlaying(), pos);

    const size_t before = fullLibrary_.size();
    fullLibrary_.erase(
        std::remove_if(fullLibrary_.begin(), fullLibrary_.end(),
                       [](const TrackInfo& t) { return !t.file.existsAsFile(); }),
        fullLibrary_.end());

    if (fullLibrary_.size() != before)
    {
        refreshSidebarArtists();
        refreshSidebarAlbums();
        refreshSidebarGenres();
        refreshSidebarPodcasts();
        refreshCurrentView();
    }
}

void MainComponent::updateTrackInLibrary(const TrackInfo& updated, bool followTrack)
{
    for (auto& t : fullLibrary_)
        if (t.file == updated.file) { t = updated; break; }

    libraryTable_.updateTrack(updated);
    transportBar_.updateCurrentTrackInfo(updated);
    refreshSidebarArtists();
    refreshSidebarAlbums();
    refreshSidebarGenres();
    refreshSidebarPodcasts();

    // If the edited track is no longer in the current categorical view (because
    // its artist/album/genre changed), navigate to the view that now contains it.
    const bool inDynamicView = (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)
                             || (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000)
                             || (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000);
    if (followTrack && inDynamicView)
    {
        bool trackStillHere = false;
        for (const auto& t : getTracksForSidebar(activeSidebarId_))
            if (t.file == updated.file) { trackStillHere = true; break; }

        if (!trackStillHere)
        {
            int newId = 1;
            if (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)
            {
                for (const auto& [id, name] : artistIdToName_)
                    if (name == updated.artist) { newId = id; break; }
            }
            else if (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000)
            {
                for (const auto& [id, info] : albumIdToInfo_)
                    if (info.artist == updated.artist && info.album == updated.album)
                        { newId = id; break; }
            }
            else if (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000)
            {
                if (updated.genre.isEmpty())
                    newId = Constants::noGenreId;
                else
                    for (const auto& [id, name] : genreIdToName_)
                        if (name == updated.genre) { newId = id; break; }
            }
            activeSidebarId_ = newId;
            sidebar_.setSelectedId(newId);
        }
    }

    // Fix the transport bar's "Playing from" link if the source name changed
    // (e.g. the only track for an artist was renamed, making the old artist disappear).
    if (queue_.hasCurrent() && queue_.current().file == updated.file)
    {
        const auto qsrc = queue_.currentSource();
        if (qsrc.sidebarId >= 2000 && qsrc.sidebarId < 3000)
        {
            bool nameFound = false;
            for (const auto& [id, name] : artistIdToName_)
                if (name == qsrc.name) { nameFound = true; break; }
            if (!nameFound)
                for (const auto& [id, name] : artistIdToName_)
                    if (name == updated.artist) { transportBar_.setPlayingFrom(name, id); break; }
        }
        else if (qsrc.sidebarId >= 3000 && qsrc.sidebarId < 4000)
        {
            bool nameFound = false;
            for (const auto& [id, info] : albumIdToInfo_)
                if (info.artist == updated.artist && info.album == updated.album)
                    { nameFound = true; break; }
            if (!nameFound)
                for (const auto& [id, info] : albumIdToInfo_)
                    if (info.artist == updated.artist && info.album == updated.album)
                    {
                        const juce::String label = (updated.artist.isNotEmpty() ? updated.artist : "Unknown Artist")
                                                 + " - " + updated.album;
                        transportBar_.setPlayingFrom(label, id);
                        break;
                    }
        }
    }

    refreshCurrentView();
    updatePlayingHighlight();

    if (! followTrack) return;

    // Scroll the library to show the edited track in its new position.
    // Skip when an edit-info dialog is open: the navigation flow calls scrollToFile
    // with the correct (new) track, and this deferred call would overwrite it.
    juce::MessageManager::callAsync([this, file = updated.file] {
        if (!activeEditInfoWindow_.getComponent())
            libraryTable_.scrollToFile(file);
    });
}

void MainComponent::scrollSelectedSidebarItemIntoView()
{
    const auto itemBounds = sidebar_.boundsForSelectedItem();
    if (itemBounds.isEmpty()) return;

    const int visibleH = sidebarViewport_.getMaximumVisibleHeight();
    const int totalH   = sidebar_.getHeight();
    if (visibleH <= 0 || totalH <= visibleH) return;   // nothing to scroll

    const int centreY = itemBounds.getY() + itemBounds.getHeight() / 2;
    int targetY = centreY - visibleH / 2;
    targetY = juce::jlimit(0, totalH - visibleH, targetY);
    sidebarViewport_.setViewPosition(0, targetY);
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
    // Show the podcast-no-folder prompt when viewing any podcast view with no
    // podcast folders configured. Hide both prompts for all other views.
    const bool isPodcastView = (activeSidebarId_ == 2)
                            || (activeSidebarId_ >= 4000 && activeSidebarId_ < 5000);
    if (isPodcastView && podcastFolders_.empty())
    {
        showPodcastPrompt(true);
    }
    else
    {
        showPodcastPrompt(false);
        // Only hide the music prompt here if there are music folders. If there
        // are none, setMusicFolders() already controls its visibility.
        if (!musicFolders_.empty())
            showEmptyLibraryPrompt(false);
    }

    libraryTable_.setSearchPlaceholder(sourceNameForSidebar(activeSidebarId_));
    // Pick the view mode for the current sidebar selection. The mode controls
    // whether the "#" column exists, what it shows, and whether drag-reorder
    // is allowed.
    using VM = LibraryTableComponent::ViewMode;
    VM mode = VM::Library;
    if      (activeSidebarId_ == 2)                                 mode = VM::Podcast;
    else if (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000)  mode = VM::Artist;
    else if (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000)  mode = VM::Album;
    else if (activeSidebarId_ >= 4000 && activeSidebarId_ < 5000)  mode = VM::Podcast;
    else if (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000)  mode = VM::Library;
    else if (activeSidebarId_ >= 1000 && activeSidebarId_ < 2000)  mode = VM::Playlist;
    libraryTable_.setViewMode(mode);

    if (activeSidebarId_ == 1)
    {
        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (!t.isPodcast) tracks.push_back(t);
        libraryTable_.setTracks(tracks);
    }
    else if (activeSidebarId_ == 2)
    {
        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (t.isPodcast) tracks.push_back(t);
        libraryTable_.setTracks(tracks);
    }
    else if (activeSidebarId_ >= 4000 && activeSidebarId_ < 5000)
    {
        const auto it = podcastIdToName_.find(activeSidebarId_);
        if (it == podcastIdToName_.end())
        {
            libraryTable_.clearTracks();
            return;
        }
        const juce::String& showName = it->second;
        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (t.isPodcast && t.podcast == showName) tracks.push_back(t);
        libraryTable_.setTracks(tracks);
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
    else if (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000)
    {
        const auto it = genreIdToName_.find(activeSidebarId_);
        if (it == genreIdToName_.end())
        {
            libraryTable_.clearTracks();
            return;
        }
        const juce::String& genre = it->second;
        std::vector<TrackInfo> tracks;
        for (const auto& t : fullLibrary_)
            if (!t.isPodcast && t.genre == genre)
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
    // Save the outgoing view's selection by file path so it can be restored
    // exactly when the user returns to that view; never carry indexes across
    // views (those would point at unrelated rows in the new view).
    if (sidebarId != activeSidebarId_)
    {
        auto outgoing = libraryTable_.selectedFiles();
        if (! outgoing.empty())
            savedSelectionByView_[activeSidebarId_] = std::move(outgoing);
        else
            savedSelectionByView_.erase(activeSidebarId_);

        // Clear the search box so a query typed for the previous view doesn't
        // narrow this one. dontSendNotification keeps applyFilter from running
        // on stale tracks; refreshCurrentView below repopulates correctly.
        libraryTable_.clearSearch();
    }

    activeSidebarId_ = sidebarId;
    refreshCurrentView();
    // Entering a playlist view: default-sort by "#" ascending so rows show
    // in their natural playlist order.
    if (sidebarId >= 1000 && sidebarId < 2000)
        libraryTable_.applyPlaylistDefaultSort();

    auto it = savedSelectionByView_.find(sidebarId);
    libraryTable_.setSelectedFiles(it != savedSelectionByView_.end()
                                       ? it->second
                                       : std::vector<juce::File>{});

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

std::vector<TrackInfo> MainComponent::getTracksForSidebar(int sidebarId) const
{
    std::vector<TrackInfo> result;

    if (sidebarId == 1)
    {
        for (const auto& t : fullLibrary_)
            if (!t.isPodcast) result.push_back(t);
    }
    else if (sidebarId == 2)
    {
        for (const auto& t : fullLibrary_)
            if (t.isPodcast) result.push_back(t);
    }
    else if (sidebarId >= 4000 && sidebarId < 5000)
    {
        const auto it = podcastIdToName_.find(sidebarId);
        if (it != podcastIdToName_.end())
            for (const auto& t : fullLibrary_)
                if (t.isPodcast && t.podcast == it->second) result.push_back(t);
    }
    else if (sidebarId >= 2000 && sidebarId < 3000)
    {
        const auto it = artistIdToName_.find(sidebarId);
        if (it != artistIdToName_.end())
            for (const auto& t : fullLibrary_)
                if (t.artist == it->second) result.push_back(t);
    }
    else if (sidebarId >= 3000 && sidebarId < 4000)
    {
        const auto it = albumIdToInfo_.find(sidebarId);
        if (it != albumIdToInfo_.end())
        {
            const auto& [artist, album] = it->second;
            for (const auto& t : fullLibrary_)
                if (t.album == album && t.artist == artist) result.push_back(t);
        }
    }
    else if (sidebarId >= 5000 && sidebarId < 6000)
    {
        if (sidebarId == Constants::noGenreId)
        {
            for (const auto& t : fullLibrary_)
                if (!t.isPodcast && t.genre.isEmpty()) result.push_back(t);
        }
        else
        {
            const auto it = genreIdToName_.find(sidebarId);
            if (it != genreIdToName_.end())
                for (const auto& t : fullLibrary_)
                    if (!t.isPodcast && t.genre == it->second) result.push_back(t);
        }
    }
    else if (sidebarId >= 1000 && sidebarId < 2000)
    {
        const auto* pl = playlistStore_->findById(sidebarId - 1000);
        if (pl)
            for (const auto& path : pl->trackPaths)
                for (const auto& t : fullLibrary_)
                    if (t.file.getFullPathName() == path) { result.push_back(t); break; }
    }

    return result;
}

void MainComponent::refreshSidebarAlbums()
{
    const bool activeInRange = (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000);
    juce::String prevActiveName;
    if (activeInRange)
        prevActiveName = sourceNameForSidebar(activeSidebarId_);

    const auto qsrc = queue_.hasCurrent() ? queue_.currentSource() : PlayQueue::QueueSource{};
    const bool srcInRange = (qsrc.sidebarId >= 3000 && qsrc.sidebarId < 4000);

    // Collect unique (artist, album) pairs using the "ARTIST - ALBUM" label
    // as the sort key so the sidebar order matches the label order.
    struct Item { juce::String artist, album, label; };
    std::vector<Item> items;
    std::set<juce::String> seen;
    for (const auto& t : fullLibrary_)
    {
        if (t.isPodcast || t.album.isEmpty()) continue;
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
        if (id >= 4000) break;
        albumIdToInfo_[id] = { it.artist, it.album };
        sidebarItems.push_back({ id, it.label });
        ++id;
    }
    sidebar_.setAlbums(sidebarItems);

    // Remap activeSidebarId_ if the label at this ID has shifted.
    if (activeInRange && prevActiveName.isNotEmpty())
        for (const auto& [newId, label] : sidebarItems)
            if (label == prevActiveName && newId != activeSidebarId_)
            {
                activeSidebarId_ = newId;
                sidebar_.setSelectedId(newId);
                break;
            }

    if (srcInRange && qsrc.name.isNotEmpty())
        for (const auto& [newId, label] : sidebarItems)
            if (label == qsrc.name && newId != qsrc.sidebarId)
            {
                transportBar_.setPlayingFrom(qsrc.name, newId);
                break;
            }
}

void MainComponent::refreshSidebarPodcasts()
{
    const bool activeInRange = (activeSidebarId_ >= 4000 && activeSidebarId_ < 5000);
    juce::String prevActiveName;
    if (activeInRange)
        if (auto it = podcastIdToName_.find(activeSidebarId_); it != podcastIdToName_.end())
            prevActiveName = it->second;

    const auto qsrc = queue_.hasCurrent() ? queue_.currentSource() : PlayQueue::QueueSource{};
    const bool srcInRange = (qsrc.sidebarId >= 4000 && qsrc.sidebarId < 5000);

    std::set<juce::String> unique;
    for (const auto& t : fullLibrary_)
        if (t.isPodcast && t.podcast.isNotEmpty())
            unique.insert(t.podcast);

    std::vector<juce::String> sorted(unique.begin(), unique.end());
    std::sort(sorted.begin(), sorted.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareNatural(b, false) < 0;
    });

    podcastIdToName_.clear();
    std::vector<std::pair<int, juce::String>> items;
    int id = 4000;
    for (const auto& name : sorted)
    {
        if (id >= 5000) break;
        podcastIdToName_[id] = name;
        items.push_back({ id, name });
        ++id;
    }
    sidebar_.setPodcasts(items);

    if (activeInRange && prevActiveName.isNotEmpty())
        for (const auto& [newId, name] : podcastIdToName_)
            if (name == prevActiveName && newId != activeSidebarId_)
            {
                activeSidebarId_ = newId;
                sidebar_.setSelectedId(newId);
                break;
            }

    if (srcInRange && qsrc.name.isNotEmpty())
        for (const auto& [newId, name] : podcastIdToName_)
            if (name == qsrc.name && newId != qsrc.sidebarId)
            {
                transportBar_.setPlayingFrom(qsrc.name, newId);
                break;
            }
}

void MainComponent::savePodcastFolders()
{
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return;

    juce::StringArray paths;
    for (const auto& f : podcastFolders_)
        paths.add(f.getFullPathName());
    props->setValue("podcastFolders", paths.joinIntoString("\n"));
}

std::vector<juce::File> MainComponent::loadSavedPodcastFolders()
{
    std::vector<juce::File> result;
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return result;

    const juce::String joined = props->getValue("podcastFolders");
    if (joined.isNotEmpty())
    {
        juce::StringArray paths;
        paths.addTokens(joined, "\n", "");
        for (const auto& p : paths)
            if (p.isNotEmpty())
                result.emplace_back(p);
    }
    return result;
}

void MainComponent::setPodcastFolders(std::vector<juce::File> folders)
{
    podcastFolders_ = std::move(folders);
    savePodcastFolders();
    // Rescan everything through the normal music-folder path (keepLibrary=true
    // so the current library stays visible while the background scan runs, and
    // scanReplacingCachedLibrary_ is set so batches go to scanBuffer_ instead
    // of appending duplicates directly to fullLibrary_).
    setMusicFolders(musicFolders_, /*keepLibrary*/ true);
}

void MainComponent::refreshSidebarArtists()
{
    // Capture names for any IDs we need to remap after the rebuild.
    const bool activeInRange = (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000);
    juce::String prevActiveName;
    if (activeInRange)
        if (auto it = artistIdToName_.find(activeSidebarId_); it != artistIdToName_.end())
            prevActiveName = it->second;

    const auto qsrc = queue_.hasCurrent() ? queue_.currentSource() : PlayQueue::QueueSource{};
    const bool srcInRange = (qsrc.sidebarId >= 2000 && qsrc.sidebarId < 3000);

    // Collect unique non-empty artist names (non-podcast tracks only), sorted case-insensitively.
    std::set<juce::String> unique;
    for (const auto& t : fullLibrary_)
        if (!t.isPodcast && t.artist.isNotEmpty())
            unique.insert(t.artist);

    std::vector<juce::String> sorted(unique.begin(), unique.end());
    std::sort(sorted.begin(), sorted.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareNatural(b, false) < 0;
    });

    artistIdToName_.clear();
    std::vector<std::pair<int, juce::String>> items;
    int id = 2000;
    for (const auto& name : sorted)
    {
        if (id >= 3000) break;
        artistIdToName_[id] = name;
        items.push_back({ id, name });
        ++id;
    }
    sidebar_.setArtists(items);

    // Remap activeSidebarId_ if it drifted due to insertion/removal shifting positions.
    if (activeInRange && prevActiveName.isNotEmpty())
        for (const auto& [newId, name] : artistIdToName_)
            if (name == prevActiveName && newId != activeSidebarId_)
            {
                activeSidebarId_ = newId;
                sidebar_.setSelectedId(newId);
                break;
            }

    // Remap the transport bar's playing-from ID to match the new assignment.
    if (srcInRange && qsrc.name.isNotEmpty())
        for (const auto& [newId, name] : artistIdToName_)
            if (name == qsrc.name && newId != qsrc.sidebarId)
            {
                transportBar_.setPlayingFrom(qsrc.name, newId);
                break;
            }
}

void MainComponent::refreshSidebarGenres()
{
    bool hasNoGenre = false;
    std::set<juce::String> unique;
    for (const auto& t : fullLibrary_)
    {
        if (t.isPodcast) continue;
        if (t.genre.isEmpty()) hasNoGenre = true;
        else                   unique.insert(t.genre);
    }

    std::vector<juce::String> sorted(unique.begin(), unique.end());
    std::sort(sorted.begin(), sorted.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareNatural(b, false) < 0;
    });

    genreIdToName_.clear();
    std::vector<std::pair<int, juce::String>> items;

    if (hasNoGenre)
    {
        genreIdToName_[Constants::noGenreId] = {};
        items.push_back({ Constants::noGenreId, "(no genre)" });
    }

    int id = 5000;
    for (const auto& name : sorted)
    {
        if (id >= Constants::noGenreId) break;   // leave 5999 reserved
        genreIdToName_[id] = name;
        items.push_back({ id, name });
        ++id;
    }
    sidebar_.setGenres(items);
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

void MainComponent::deleteStylFilesInLibrary()
{
    std::vector<juce::File> allFolders = musicFolders_;
    allFolders.insert(allFolders.end(), podcastFolders_.begin(), podcastFolders_.end());

    for (const auto& folder : allFolders)
    {
        if (!folder.isDirectory()) continue;
        for (const auto& f : folder.findChildFiles(juce::File::findFiles, true, ".*.styl"))
            f.deleteFile();
    }
}

void MainComponent::incrementPlayCount(const juce::File& file)
{
    for (auto& t : fullLibrary_)
    {
        if (t.file == file)
        {
            ++t.playCount;
            StylFile::save(t);
            libraryTable_.updateTrack(t);
            return;
        }
    }
}

void MainComponent::showSongInfoEditor(const TrackInfo& track,
                                       std::vector<TrackInfo> peerList,
                                       int peerIndex)
{
    lastEditedInfoFile_ = track.file;
    libraryTable_.scrollToFile(track.file);

    auto* editor = new SongInfoEditor(track);
    auto* dw     = new EditInfoDialogWindow("Edit Info", juce::Colour(0xff1e1e1e));
    dw->setUsingNativeTitleBar(true);
    dw->setResizable(false, false);
    dw->setContentOwned(editor, true);
    dw->centreWithSize(editor->getWidth(), editor->getHeight());
    activeEditInfoWindow_ = dw;

    editInfoLockOverlay_.setVisible(true);
    editInfoLockOverlay_.toFront(false);
    resized();

    // Closes the dialog window and, optionally, hides the overlay.
    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto closeDialog = [safeThis](bool hideOverlay) {
        if (auto* self = safeThis.getComponent())
        {
            if (auto* w = self->activeEditInfoWindow_.getComponent())
            {
                self->activeEditInfoWindow_ = nullptr;
                if (hideOverlay)
                {
                    self->editInfoLockOverlay_.setVisible(false);
                    self->resized();
                }
                juce::MessageManager::callAsync([w] { delete w; });
            }
        }
    };

    editor->onSave = [this](std::vector<TrackInfo> updated) {
        for (auto& t : updated) {
            StylFile::save(t);
            updateTrackInLibrary(t);
        }
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_);
    };

    editor->onLookupRequested = [this](const TrackInfo& t, std::function<void(bool, TrackInfo)> cb) {
        editorLookupFile_     = t.file;
        editorLookupCallback_ = std::move(cb);
        appleMusicLookup_.enqueue(t, true);
    };

    editor->onDismiss = [closeDialog] { closeDialog(true); };
    dw->onDismiss     = [closeDialog] { closeDialog(true); };

    if (peerIndex >= 0 && (int)peerList.size() > 1)
    {
        editor->setPeerNavigation(peerIndex, (int)peerList.size());
        editor->onSaveAndNavigate = [safeThis, closeDialog](int delta) {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;

            // Recompute peers from the current view at navigate time. An Apple
            // Music lookup may have changed the album/artist/genre and shifted
            // the track to a new position in the sort order, using the
            // snapshot captured when the dialog opened would walk the wrong
            // neighbours.
            auto currentPeers = self->libraryTable_.visibleTracks();
            if (currentPeers.empty()) return;

            int currentIdx = -1;
            for (int i = 0; i < (int) currentPeers.size(); ++i)
                if (currentPeers[(size_t) i].file == self->lastEditedInfoFile_)
                    { currentIdx = i; break; }

            if (currentIdx < 0) return;  // track is no longer in this view

            const int newIndex = currentIdx + delta;
            if (newIndex < 0 || newIndex >= (int) currentPeers.size()) return;

            closeDialog(false); // close old dialog without hiding the overlay

            TrackInfo trackToEdit = currentPeers[(size_t) newIndex];
            for (const auto& t : self->fullLibrary_)
                if (t.file == trackToEdit.file) { trackToEdit = t; break; }
            self->showSongInfoEditor(trackToEdit, currentPeers, newIndex);
        };
    }

    dw->setVisible(true);
}

void MainComponent::showMultiInfoEditor(const std::vector<TrackInfo>& tracks)
{
    auto* editor = new SongInfoEditor(tracks);
    auto* dw     = new EditInfoDialogWindow("Edit Info", juce::Colour(0xff1e1e1e));
    dw->setUsingNativeTitleBar(true);
    dw->setResizable(false, false);
    dw->setContentOwned(editor, true);
    dw->centreWithSize(editor->getWidth(), editor->getHeight());
    activeEditInfoWindow_ = dw;

    editInfoLockOverlay_.setVisible(true);
    editInfoLockOverlay_.toFront(false);
    resized();

    juce::Component::SafePointer<MainComponent> safeThis(this);
    auto closeDialog = [safeThis] {
        if (auto* self = safeThis.getComponent())
        {
            if (auto* w = self->activeEditInfoWindow_.getComponent())
            {
                self->activeEditInfoWindow_ = nullptr;
                self->editInfoLockOverlay_.setVisible(false);
                self->resized();
                juce::MessageManager::callAsync([w] { delete w; });
            }
        }
    };

    editor->onSave = [this](std::vector<TrackInfo> updated) {
        for (auto& t : updated) {
            StylFile::save(t);
            updateTrackInLibrary(t);
        }
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_);
    };

    editor->onDismiss = closeDialog;
    dw->onDismiss     = closeDialog;
    dw->setVisible(true);
}

void MainComponent::setupAudioEngineCallbacks()
{
    engine_.onTrackStarted = [this](const TrackInfo& track) {
        transportBar_.setCurrentTrack(track);
        queueView_.refresh(queue_);
        const auto& t = track;
        const std::string artist = t.isPodcast ? t.podcast.toStdString() : t.artist.toStdString();
        const std::string title  = t.displayTitle().toStdString();
        nowPlaying_.setTrackInfo(title, artist, t.durationSecs);
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
            nowPlaying_.clearNowPlaying();
            updateNavButtons();
        }
    };

    engine_.onTrackFailed = [this] {
        if (queue_.hasNext())
        {
            queue_.advanceToNext();
            playCurrentQueueItem();
        }
    };

    engine_.onStateChanged = [this] {
        // Start/stop the transport bar's animation timer in lockstep with
        // playback so the bar does no per-frame work while paused or stopped.
        transportBar_.updateTimerState();
        transportBar_.repaint();
        StatusBarItem::State sbState;
        if (engine_.isPlaying())       sbState = StatusBarItem::State::Playing;
        else if (engine_.isPaused())   sbState = StatusBarItem::State::Paused;
        else                           sbState = StatusBarItem::State::Stopped;
        statusBarItem_.setState(sbState);
    };

    auto togglePlayPause = [this] {
        if (engine_.isPlaying())     engine_.pause();
        else if (engine_.isPaused()) engine_.resume();
    };
    nowPlaying_.onPlayPause  = [this, togglePlayPause] { transportBar_.flashPlayPause(); togglePlayPause(); };
    nowPlaying_.onPrevious   = [this] { transportBar_.flashPrev(); playPrev(); };
    nowPlaying_.onNext       = [this] { transportBar_.flashNext(); playNext(); };
    statusBarItem_.onShowApp = [this] { if (onShowWindowRequested) onShowWindowRequested(); };
    statusBarItem_.onQuit    = [] { juce::JUCEApplication::getInstance()->systemRequestedQuit(); };
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

MainComponent::PrefsLockOverlay::PrefsLockOverlay()
{
    // Capture every click so nothing underneath is reachable.
    setInterceptsMouseClicks(true, true);

    message_.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)).boldened());
    message_.setColour(juce::Label::textColourId, Constants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
        b.setColour(juce::TextButton::textColourOffId, Constants::Color::textPrimary);
    };
    styleButton(openBtn_);
    styleButton(closeBtn_);
    openBtn_.onClick  = [this] { if (onRecenterPrefs) onRecenterPrefs(); };
    closeBtn_.onClick = [this] { if (onClosePrefs)    onClosePrefs(); };
    addAndMakeVisible(openBtn_);
    addAndMakeVisible(closeBtn_);
}

void MainComponent::PrefsLockOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));

    // Solid panel behind the text + buttons so they read clearly against any
    // dimmed library content underneath.
    constexpr int msgH   = 28;
    constexpr int gap    = 12;
    constexpr int btnH   = 32;
    constexpr int btnW   = 180;
    constexpr int btnGap = 12;
    constexpr int padX   = 22;
    constexpr int padY   = 16;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    const auto panel = getLocalBounds()
                          .withSizeKeepingCentre(groupW + padX * 2,
                                                 groupH + padY * 2)
                          .toFloat();

    g.setColour(Constants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);

    g.setColour(Constants::Color::border);
    g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
}

void MainComponent::PrefsLockOverlay::resized()
{
    constexpr int msgH = 28;
    constexpr int gap  = 12;
    constexpr int btnH = 32;
    constexpr int btnW = 180;
    constexpr int btnGap = 12;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    auto area = getLocalBounds().withSizeKeepingCentre(groupW, groupH);

    message_.setBounds(area.removeFromTop(msgH));
    area.removeFromTop(gap);
    auto row = area.removeFromTop(btnH);
    openBtn_.setBounds(row.removeFromLeft(btnW));
    row.removeFromLeft(btnGap);
    closeBtn_.setBounds(row.removeFromLeft(btnW));
}

// ----------------------------------------------------------------------------
// EditInfoLockOverlay
// ----------------------------------------------------------------------------

MainComponent::EditInfoLockOverlay::EditInfoLockOverlay()
{
    setInterceptsMouseClicks(true, true);

    message_.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)).boldened());
    message_.setColour(juce::Label::textColourId, Constants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
        b.setColour(juce::TextButton::textColourOffId, Constants::Color::textPrimary);
    };
    styleButton(openBtn_);
    styleButton(closeBtn_);
    openBtn_.onClick  = [this] { if (onOpenEditInfo)  onOpenEditInfo(); };
    closeBtn_.onClick = [this] { if (onCloseEditInfo) onCloseEditInfo(); };
    addAndMakeVisible(openBtn_);
    addAndMakeVisible(closeBtn_);
}

void MainComponent::EditInfoLockOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));

    constexpr int msgH   = 28;
    constexpr int gap    = 12;
    constexpr int btnH   = 32;
    constexpr int btnW   = 180;
    constexpr int btnGap = 12;
    constexpr int padX   = 22;
    constexpr int padY   = 16;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    const auto panel = getLocalBounds()
                          .withSizeKeepingCentre(groupW + padX * 2,
                                                 groupH + padY * 2)
                          .toFloat();

    g.setColour(Constants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);

    g.setColour(Constants::Color::border);
    g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
}

void MainComponent::EditInfoLockOverlay::resized()
{
    constexpr int msgH   = 28;
    constexpr int gap    = 12;
    constexpr int btnH   = 32;
    constexpr int btnW   = 180;
    constexpr int btnGap = 12;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    auto area = getLocalBounds().withSizeKeepingCentre(groupW, groupH);

    message_.setBounds(area.removeFromTop(msgH));
    area.removeFromTop(gap);
    auto row = area.removeFromTop(btnH);
    openBtn_.setBounds(row.removeFromLeft(btnW));
    row.removeFromLeft(btnGap);
    closeBtn_.setBounds(row.removeFromLeft(btnW));
}

// ----------------------------------------------------------------------------
// QuitLockOverlay
// ----------------------------------------------------------------------------

MainComponent::QuitLockOverlay::QuitLockOverlay()
{
    setInterceptsMouseClicks(true, true);

    message_.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)).boldened());
    message_.setColour(juce::Label::textColourId, Constants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    cancelBtn_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    cancelBtn_.setColour(juce::TextButton::textColourOffId, Constants::Color::textPrimary);
    cancelBtn_.onClick = [this] { if (onCancelQuit) onCancelQuit(); };
    addAndMakeVisible(cancelBtn_);
}

void MainComponent::QuitLockOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));

    constexpr int msgH = 28;
    constexpr int gap  = 12;
    constexpr int btnH = 32;
    constexpr int btnW = 180;
    constexpr int padX = 22;
    constexpr int padY = 16;

    const int groupW = btnW;
    const int groupH = msgH + gap + btnH;
    const auto panel = getLocalBounds()
                          .withSizeKeepingCentre(groupW + padX * 2,
                                                 groupH + padY * 2)
                          .toFloat();

    g.setColour(Constants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(Constants::Color::border);
    g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
}

void MainComponent::QuitLockOverlay::resized()
{
    constexpr int msgH = 28;
    constexpr int gap  = 12;
    constexpr int btnH = 32;
    constexpr int btnW = 180;

    const int groupH = msgH + gap + btnH;
    auto area = getLocalBounds().withSizeKeepingCentre(btnW, groupH);

    message_.setBounds(area.removeFromTop(msgH));
    area.removeFromTop(gap);
    cancelBtn_.setBounds(area.removeFromTop(btnH));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

    // Transport bar at the bottom
    transportBar_.setBounds(bounds.removeFromBottom(transportBarHeight));

    // Queue panel on the right (only when visible). A thin draggable divider
    // sits at its left edge; users drag it to resize the queue.
    if (queueVisible_)
    {
        constexpr int queueDividerW = 6;
        constexpr int minQueueWidth = 44;
        const int maxQueueWidth = juce::jmax(minQueueWidth, (getWidth() * 2) / 5);
        queueWidth_ = juce::jlimit(minQueueWidth, maxQueueWidth, queueWidth_);

        queueView_.setBounds(bounds.removeFromRight(queueWidth_));
        queueDivider_.setBounds(bounds.removeFromRight(queueDividerW));
        queueDivider_.toFront(false);
    }

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
    // handles the height, but it only runs when setSize triggers resized(). If
    // the viewport grows taller while the width is unchanged, JUCE skips the
    // call, leaving a void. Force a layout pass in that case too.
    const int sbVisibleW = sidebarViewport_.getMaximumVisibleWidth();
    const int sbVisibleH = sidebarViewport_.getMaximumVisibleHeight();
    if (sbVisibleW > 0 && sidebar_.getWidth() != sbVisibleW)
        sidebar_.setSize(sbVisibleW, juce::jmax(sidebar_.getHeight(), sbVisibleH));
    else if (sbVisibleH > sidebar_.getHeight())
        sidebar_.setSize(sidebar_.getWidth(), sbVisibleH);

    // Library fills remaining space
    libraryTable_.setBounds(bounds);

    // Pin button: always sits above the speaker icon in the transport bar.
    constexpr int pinSize = 17;
    // X-distance from the window's right edge to the speaker icon's centre.
    // Must stay in sync with TransportBar::resized()'s pad + volAreaW geometry.
    constexpr int speakerCentreFromRight = 47;
    const int pinX = getWidth() - speakerCentreFromRight - pinSize / 2 - 1;
    const int thirdH = transportBar_.getHeight() / 3;
    const int pinY   = transportBar_.getY() + (thirdH - pinSize) / 2 + 5;

    pinButton_.setBounds(pinX, pinY, pinSize, pinSize);
    pinButton_.toFront(false);

    // Empty-state prompts centered in the library area.
    emptyPromptLabel_.setBounds(bounds.withSizeKeepingCentre(400, 40));
    chooseFolderButton_.setBounds(bounds.withSizeKeepingCentre(200, 36).translated(0, 50));
    podcastPromptLabel_.setBounds(bounds.withSizeKeepingCentre(400, 40));
    podcastFolderButton_.setBounds(bounds.withSizeKeepingCentre(200, 36).translated(0, 50));
    loadingIndicator_.setBounds(bounds.withSizeKeepingCentre(280, 40));

    // Preferences-lock overlay covers the whole component so it dims both
    // the library area and transport bar uniformly.
    prefsLockOverlay_.setBounds(getLocalBounds());
    if (prefsLockOverlay_.isVisible())
        prefsLockOverlay_.toFront(false);

    editInfoLockOverlay_.setBounds(getLocalBounds());
    if (editInfoLockOverlay_.isVisible())
        editInfoLockOverlay_.toFront(false);

    quitLockOverlay_.setBounds(getLocalBounds());
    if (quitLockOverlay_.isVisible())
        quitLockOverlay_.toFront(false);
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (key == juce::KeyPress(juce::KeyPress::spaceKey))
    {
        transportBar_.flashPlayPause();
        if (engine_.isPlaying())       engine_.pause();
        else if (engine_.isPaused())   engine_.resume();
        return true;
    }
    return false;
}

void MainComponent::showAddFolderChooser()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Select a music folder",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;
            auto updated = musicFolders_;
            const auto folder = results[0];
            // Avoid duplicate entries.
            bool already = false;
            for (const auto& f : updated)
                if (f == folder) { already = true; break; }
            if (! already)
                updated.push_back(folder);
            setMusicFolders(std::move(updated));
        });
}

void MainComponent::setMusicFolders(std::vector<juce::File> folders)
{
    setMusicFolders(std::move(folders), /*keepLibrary*/ false);
}

void MainComponent::setMusicFolders(std::vector<juce::File> folders, bool keepLibrary)
{
    musicFolders_ = std::move(folders);
    DBG("setMusicFolders: " + juce::String((int) musicFolders_.size())
        + " folder(s), keepLibrary=" + juce::String((int) keepLibrary));
    for (const auto& f : musicFolders_)
        DBG("  folder: " + f.getFullPathName()
            + " (isDir=" + juce::String((int) f.isDirectory()) + ")");
    saveMusicFolders();

    if (! keepLibrary)
    {
        fullLibrary_.clear();
        activeSidebarId_ = 1;
        sidebar_.setSelectedId(1);
        libraryTable_.clearTracks();
        refreshSidebarArtists();
        refreshSidebarAlbums();
    }

    if (musicFolders_.empty())
    {
        loadingIndicator_.setVisible(false);
        sidebar_.setLibraryLoading(false);
        showEmptyLibraryPrompt(true);
        refreshCurrentView();
        resized();
        return;
    }

    checkFolderAccessibility();

    showEmptyLibraryPrompt(false);
    libraryTable_.setSuppressEmptyLabel(true);
    // Only show the centred loading overlay when we have nothing to display
    // already. With a cached library visible, the spinner next to the LIBRARY
    // heading is enough to signal background activity.
    loadingIndicator_.setVisible(! keepLibrary);
    sidebar_.setLibraryLoading(true);
    resized();

    scanReplacingCachedLibrary_ = keepLibrary;
    scanBuffer_.clear();
    scanner_.scanFolders(musicFolders_, podcastFolders_);
}


void MainComponent::saveMusicFolders()
{
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return;

    juce::StringArray paths;
    for (const auto& f : musicFolders_)
        paths.add(f.getFullPathName());
    props->setValue("musicFolders", paths.joinIntoString("\n"));
    props->removeValue("musicFolder");   // remove legacy single-folder key
}

std::vector<juce::File> MainComponent::loadSavedMusicFolders()
{
    std::vector<juce::File> result;
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return result;

    const juce::String joined = props->getValue("musicFolders");
    if (joined.isNotEmpty())
    {
        juce::StringArray paths;
        paths.addTokens(joined, "\n", "");
        for (const auto& p : paths)
            if (p.isNotEmpty())
                result.emplace_back(p);
        return result;
    }

    // Fall back to the old single-folder key so upgrading users keep their
    // library without having to re-add it.
    const juce::String legacy = props->getValue("musicFolder");
    if (legacy.isNotEmpty())
        result.emplace_back(legacy);

    return result;
}

juce::String MainComponent::sourceNameForSidebar(int sidebarId) const
{
    if (sidebarId == 1)
        return "All Music";
    if (sidebarId == 2)
        return "All Podcasts";
    if (sidebarId >= 4000 && sidebarId < 5000)
    {
        const auto it = podcastIdToName_.find(sidebarId);
        return it != podcastIdToName_.end() ? it->second : juce::String("Podcast");
    }
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
    if (sidebarId == Constants::noGenreId)
        return "(no genre)";
    if (sidebarId >= 5000 && sidebarId < 6000)
    {
        const auto it = genreIdToName_.find(sidebarId);
        return it != genreIdToName_.end() ? it->second : juce::String("Genre");
    }
    const auto* pl = playlistStore_->findById(sidebarId - 1000);
    return pl ? pl->name : "Playlist";
}

void MainComponent::activateRow(int rowIndex, const std::vector<TrackInfo>& libraryTracks)
{
    // Capture shuffle state before setTracks resets it via onShuffleStateChanged.
    const bool wasShuffled = shuffleOn_;

    // Always load in natural view order with the selected track as startIndex.
    // shuffleRemaining() will shuffle only what follows, and originalTracks_ will
    // hold the full natural order so unshuffling restores it correctly.
    std::vector<TrackInfo> queueTracks(libraryTracks.begin(), libraryTracks.end());

    PlayQueue::QueueSource source;
    source.sidebarId = activeSidebarId_;
    source.name      = sourceNameForSidebar(activeSidebarId_);

    queue_.setTracks(std::move(queueTracks), rowIndex, source);

    if (wasShuffled)
    {
        shuffleOn_ = true;
        transportBar_.setShuffleOn(true);
        queue_.shuffleAll();
    }

    playCurrentQueueItem();
}

void MainComponent::updateNavButtons()
{
    transportBar_.setCanGoPrev(queue_.hasCurrent());
    transportBar_.setCanGoNext(queue_.hasNext());
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
    updateNavButtons();
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
    if (engine_.elapsedSeconds() > 3.0 || !queue_.hasPrev())
    {
        engine_.seekToNormalized(0.0);
    }
    else
    {
        queue_.retreatToPrev();
        playCurrentQueueItem();
    }
}

void MainComponent::toggleQueue()
{
    queueVisible_ = !queueVisible_;
    queueView_.setVisible(queueVisible_);
    queueDivider_.setVisible(queueVisible_);
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
    // Push the live folder lists into the panels each time we open Preferences
    // so the UI reflects any changes made elsewhere.
    if (auto* libPanel = preferencesWindow_->libraryPanel())
    {
        libPanel->setFolders(musicFolders_);
        libPanel->setPodcastFolders(podcastFolders_);
    }
    preferencesWindow_->setVisible(true);
    preferencesWindow_->toFront(true);
    // Preferences floats above the main window so the user can't hide it.
    preferencesWindow_->setAlwaysOnTop(true);

    // Show the lock overlay on the main window. It dims the content and
    // captures clicks so the user can't interact with anything underneath,
    // but exposes its own "Open Preferences" / "Close Preferences" buttons.
    prefsLockOverlay_.setVisible(true);
    prefsLockOverlay_.toFront(false);
    resized();

    // Refresh the menu so "Preferences..." disables while the window is open.
    menuItemsChanged();
}

void MainComponent::showEmptyLibraryPrompt(bool show)
{
    emptyPromptLabel_.setVisible(show);
    chooseFolderButton_.setVisible(show);
    if (show) showPodcastPrompt(false);
    libraryTable_.setSuppressEmptyLabel(show || podcastPromptLabel_.isVisible());
}

void MainComponent::showPodcastPrompt(bool show)
{
    podcastPromptLabel_.setVisible(show);
    podcastFolderButton_.setVisible(show);
    if (show) showEmptyLibraryPrompt(false);
    libraryTable_.setSuppressEmptyLabel(show || emptyPromptLabel_.isVisible());
}

void MainComponent::showPreferencesLibrary()
{
    showPreferences();
    if (preferencesWindow_)
        preferencesWindow_->showLibraryCategory();
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
        menu.addCommandItem(&commandManager_, cmdShowHidden);
    }
    else if (index == 1)
    {
        menu.addCommandItem(&commandManager_, cmdShowPlayerWindow);
        menu.addSeparator();
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
    commands.add(cmdShowHidden);
    commands.add(cmdShowAnalysisLog);
    commands.add(cmdPreferences);
    commands.add(cmdAlwaysOnTop);
    commands.add(cmdFocusSearch);
    commands.add(cmdShowPlayerWindow);
    commands.add(cmdEditInfo);
}

void MainComponent::getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    switch (id)
    {
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
                         "Open the Stylus preferences window",
                         "File", 0);
            // Disable the menu item + shortcut while the preferences window
            // is already open, so clicking it again does nothing.
            info.setActive(! (preferencesWindow_ && preferencesWindow_->isVisible()));
            info.addDefaultKeypress(',', juce::ModifierKeys::commandModifier);
            break;

        case cmdAlwaysOnTop:
            info.setInfo("Always on Top",
                         "Keep the Stylus window above all other windows",
                         "Window", 0);
            info.setTicked(alwaysOnTop_);
            info.addDefaultKeypress('p', juce::ModifierKeys::commandModifier
                                       | juce::ModifierKeys::shiftModifier);
            break;

        case cmdShowPlayerWindow:
            info.setInfo("Show Player Window",
                         "Bring the Stylus window to the front",
                         "Window", 0);
            break;

        case cmdEditInfo:
            info.setInfo("Edit Info",
                         "Edit metadata for the selected track(s)",
                         "File", 0);
            info.setActive(libraryTable_.hasSelection() && !activeEditInfoWindow_.getComponent());
            info.addDefaultKeypress('r', juce::ModifierKeys::commandModifier);
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

        case cmdShowPlayerWindow:
            if (onShowWindowRequested) onShowWindowRequested();
            return true;

        case cmdEditInfo:
            libraryTable_.triggerEditInfoForSelection();
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
    if (sessionRestoring_)
    {
        DBG("saveSessionState skipped (sessionRestoring=true)");
        return;
    }
    sessionRestored_ = true;

    auto* props = appProperties_.getUserSettings();
    if (!props)
    {
        DBG("saveSessionState: getUserSettings() returned null");
        return;
    }
    DBG("saveSessionState - sidebar=" + juce::String(activeSidebarId_)
        + " queue.size=" + juce::String(queue_.size())
        + " queue.hasCurrent=" + juce::String((int) queue_.hasCurrent()));

    props->setValue("sessionSidebarWidth",    sidebarWidth_);
    props->setValue("sessionQueueWidth",      queueWidth_);
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
    // Always-on-top is deliberately not persisted; each launch starts "off".

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

    // Flush to disk immediately so the saved state survives crashes / SIGKILL
    // / debugger stops, not only clean exits.
    const bool ok = props->saveIfNeeded();
    DBG("saveSessionState wrote to " + props->getFile().getFullPathName()
        + (ok ? " (saveIfNeeded ok)" : " (saveIfNeeded FAILED or no-op)"));
}

void MainComponent::saveSessionElapsed()
{
    if (sessionRestoring_ || !sessionRestored_) return;
    // (Unlike saveSessionState we don't auto-flip sessionRestored_ here:
    //  the timer that calls this fires regardless of user action, so flipping
    //  on the first tick would always pre-empt the upcoming restore.)
    if (auto* props = appProperties_.getUserSettings())
    {
        props->setValue("sessionElapsedSeconds", engine_.elapsedSeconds());
        props->saveIfNeeded();
    }
}

void MainComponent::restoreSessionState()
{
    auto* props = appProperties_.getUserSettings();
    if (!props)
    {
        DBG("restoreSessionState: getUserSettings() returned null");
        sessionRestored_ = true;
        return;
    }

    DBG("restoreSessionState reading from " + props->getFile().getFullPathName()
        + " (file exists=" + juce::String((int) props->getFile().existsAsFile()) + ")");
    DBG("  sessionQueuePaths length: "
        + juce::String(props->getValue("sessionQueuePaths").length()));
    DBG("  sessionQueueCurrentPath: " + props->getValue("sessionQueueCurrentPath"));
    DBG("  fullLibrary_ size: " + juce::String((int) fullLibrary_.size()));

    sessionRestoring_ = true;

    // Sidebar width (the layout clamps to the valid min/max on the next
    // resized() call, so out-of-range persisted values are harmless).
    sidebarWidth_ = props->getIntValue("sessionSidebarWidth", Constants::sidebarWidth);
    queueWidth_   = props->getIntValue("sessionQueueWidth",   Constants::queuePanelWidth);
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

    // Always-on-top pin: intentionally NOT restored. Every launch starts with
    // the feature disabled; the user has to opt in again each session.
    alwaysOnTop_ = false;
    pinButton_.toggleState = 0;
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
                queue_.shuffleAll();

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
    DBG("restoreSessionState complete");

    // Deferred so it runs after any pending resize/layout messages have
    // processed, meaning the sidebar's item bounds + viewport height are
    // final by the time we compute the scroll position.
    juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
}

void MainComponent::requestQuit(std::function<void()> onConfirmed)
{
    // If the dialog is already open, just bring it to front.
    if (activeQuitDialog_.getComponent() != nullptr)
    {
        activeQuitDialog_.getComponent()->toFront(true);
        return;
    }

    // Dismiss any open modal dialogs before proceeding.
    if (activeEditInfoWindow_ != nullptr)
        activeEditInfoWindow_->exitModalState(0);
    if (preferencesWindow_ && preferencesWindow_->isVisible())
        preferencesWindow_->setVisible(false);

    bool ask = true;
    if (auto* s = appProperties_.getUserSettings())
        ask = s->getBoolValue(MiscPreferencesPanel::kAskBeforeQuittingKey, true);

    if (!ask || !engine_.isPlaying())
    {
        if (onConfirmed) onConfirmed();
        return;
    }

    // Custom modal dialog so we control all padding and can use a native title bar.
    struct QuitDialog : public juce::Component
    {
        juce::Label        msg      { {}, "Quitting will end playback immediately." };
        juce::ToggleButton dontShow { "Don't show again" };
        juce::TextButton   cancelBtn { "Cancel" };
        juce::TextButton   quitBtn   { "Quit" };

        enum { pad = 18, rowH = 22, btnH = 28 };

        QuitDialog()
        {
            msg.setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(msg);
            addAndMakeVisible(dontShow);
            addAndMakeVisible(cancelBtn);
            addAndMakeVisible(quitBtn);
            setSize(320, pad + rowH + 10 + rowH + 16 + btnH + pad);
        }

        void paint(juce::Graphics& g) override
        {
            g.fillAll(juce::Colour(0xff1e1e1e));
        }

        void resized() override
        {
            int y = pad;
            msg.setBounds(pad, y, getWidth() - 2 * pad, rowH);
            y += rowH + 10;
            dontShow.setBounds(pad, y, getWidth() - 2 * pad, rowH);
            y += rowH + 16;
            const int bw = 80;
            const int x2 = getWidth() - pad - bw;
            cancelBtn.setBounds(x2 - bw - 8, y, bw, btnH);
            quitBtn.setBounds(x2, y, bw, btnH);
        }
    };

    auto* dlg      = new QuitDialog();
    auto* dontShow = &dlg->dontShow;
    dlg->setName("Confirm Quit");

    auto dismiss = [this, dlg](bool confirmed, std::function<void()> cb) {
        activeQuitDialog_ = nullptr;
        quitLockOverlay_.setVisible(false);
        resized();
        delete dlg;
        if (confirmed && cb) cb();
    };

    dlg->cancelBtn.onClick = [dismiss] { dismiss(false, {}); };
    dlg->quitBtn.onClick   = [this, dismiss, dontShow, onConfirmed] {
        if (dontShow->getToggleState())
            if (auto* s = appProperties_.getUserSettings())
                s->setValue(MiscPreferencesPanel::kAskBeforeQuittingKey, false);
        dismiss(true, onConfirmed);
    };

    dlg->addToDesktop(juce::ComponentPeer::windowHasTitleBar
                    | juce::ComponentPeer::windowHasDropShadow
                    | juce::ComponentPeer::windowIsTemporary);
    dlg->setVisible(true);
    dlg->setCentrePosition(getScreenBounds().getCentreX(), getScreenBounds().getCentreY());

    activeQuitDialog_ = dlg;
    quitLockOverlay_.setVisible(true);
    quitLockOverlay_.toFront(false);
    resized();
}

} // namespace Stylus
