#include "MainComponent.h"
#include "Constants.h"
#include "UIConstants.h"
#include "audio/StylFile.h"
#include "ui/SongInfoEditor.h"
#include "ui/PlatformChrome.h"
#include <algorithm>
#include <filesystem>
#include <set>

namespace Stylus
{

using namespace UIConstants;

static constexpr int orphanCheckIntervalMs = 30'000;

// Resolves symlinks (and any "/foo/../bar" relative segments) so dedup
// comparisons treat two paths pointing at the same physical file as equal.
// Returns the original juce::File when canonicalization fails (file doesn't
// exist, permission error, broken symlink, etc.) so dedup is best-effort
// rather than silently allowing duplicates through.
static juce::File canonicalizeFile(const juce::File& f)
{
    try
    {
        auto canonical = std::filesystem::weakly_canonical(
            std::filesystem::path(f.getFullPathName().toStdString()));
        return juce::File(juce::String(canonical.string()));
    }
    catch (...)
    {
        return f;
    }
}

// Global LookAndFeel: pointer cursor on TextButton, light-red hover on "Quit",
// and (on Windows) flat dark menu styling that matches the immersive title
// bar. macOS uses the system menu bar via setMacMainMenu so the menu-side
// overrides only matter for popup / context menus there.
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

    // Flat fill for the menu bar strip: V4's default draws a 1-px contrast
    // border at the top and bottom that breaks visual continuity with the
    // title bar above.
    void drawMenuBarBackground(juce::Graphics& g, int width, int height,
                               bool, juce::MenuBarComponent& bar) override
    {
        juce::ignoreUnused(width, height);
        g.fillAll(bar.findColour(juce::PopupMenu::backgroundColourId));
    }

    // Flat fill for popup menu panels. V4's default adds a subtle gradient
    // that doesn't match the Win11 dark menu look.
    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override
    {
        juce::ignoreUnused(width, height);
        g.fillAll(findColour(juce::PopupMenu::backgroundColourId));
    }

   #if JUCE_WINDOWS
    // Win11 system menus use Segoe UI at ~9pt; matching the system font is
    // the biggest single win for "looks native" perception. Mac inherits
    // SF Pro from JUCE's platform default, so this override is Windows-only.
    juce::Font getMenuBarFont(juce::MenuBarComponent&,
                              int /*itemIndex*/,
                              const juce::String& /*itemText*/) override
    {
        return juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(17.0f));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font(juce::FontOptions().withName("Segoe UI").withHeight(19.0f));
    }

    // Tighter horizontal padding per menu-bar item than the V4 default's
    // +16 px so "File" and "Window" sit closer together, matching the
    // density of native Win11 menu bars.
    int getMenuBarItemWidth(juce::MenuBarComponent& bar, int itemIndex,
                            const juce::String& itemText) override
    {
        return juce::GlyphArrangement::getStringWidthInt(
                   getMenuBarFont(bar, itemIndex, itemText), itemText) + 14;
    }

    // Brighter highlight than highlightedBackgroundColourId because the
    // menu bar's full-cell fill looks visually dimmer than the popup's
    // inset rounded fill at the same colour value. We don't want to nudge
    // the global highlight colour up because that brightens the popup
    // hover too far.
    void drawMenuBarItem(juce::Graphics& g, int width, int height,
                         int itemIndex, const juce::String& itemText,
                         bool isMouseOverItem, bool isMenuOpen,
                         bool /*isMouseOverBar*/,
                         juce::MenuBarComponent& bar) override
    {
        if (isMenuOpen || isMouseOverItem)
        {
            g.setColour(juce::Colour(0xff404040));
            g.fillRect(0, 0, width, height);
            g.setColour(bar.findColour(juce::PopupMenu::highlightedTextColourId));
        }
        else
        {
            g.setColour(bar.findColour(juce::PopupMenu::textColourId));
        }
        g.setFont(getMenuBarFont(bar, itemIndex, itemText));
        g.drawFittedText(itemText, 0, 0, width, height,
                         juce::Justification::centred, 1);
    }

    // Tighter popup-item layout than V4's default. The default reserves a
    // font-height-wide column on the left for icons/checkmarks even when
    // none are shown, which leaves a visible gap before the item text.
    // Reserve a smaller column and only draw a checkmark when an item is
    // actually ticked.
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon,
                           const juce::Colour* textColour) override
    {
        if (isSeparator)
        {
            auto r = area.reduced(5, 0);
            r.removeFromTop(juce::roundToInt((float) r.getHeight() * 0.5f - 0.5f));
            g.setColour(findColour(juce::PopupMenu::textColourId)
                            .withMultipliedAlpha(0.3f));
            g.fillRect(r.removeFromTop(1));
            return;
        }

        juce::Colour col = (textColour != nullptr ? *textColour
                                                  : findColour(juce::PopupMenu::textColourId));
        auto r = area.reduced(1);

        if (isHighlighted && isActive)
        {
            g.setColour(findColour(juce::PopupMenu::highlightedBackgroundColourId));
            g.fillRect(r);
            g.setColour(findColour(juce::PopupMenu::highlightedTextColourId));
        }
        else
        {
            g.setColour(col.withMultipliedAlpha(isActive ? 1.0f : 0.5f));
        }

        r.reduce(4, 0);

        auto font = getPopupMenuFont();
        const float maxFontHeight = (float) r.getHeight() / 1.3f;
        if (font.getHeight() > maxFontHeight)
            font.setHeight(maxFontHeight);
        g.setFont(font);

        // Tighter icon/tick column than V4's default. The column is always
        // reserved (so the leading text alignment stays consistent across
        // ticked and unticked items) but smaller than V4's full-font-height
        // gap, so plain text doesn't appear orphaned away from the menu's
        // left edge.
        const int iconAreaW = juce::roundToInt(font.getHeight() * 0.35f);
        auto iconArea = r.removeFromLeft(iconAreaW).toFloat();

        if (icon != nullptr)
        {
            icon->drawWithin(g, iconArea,
                             juce::RectanglePlacement::centred
                             | juce::RectanglePlacement::onlyReduceInSize, 1.0f);
        }
        else if (isTicked)
        {
            auto tick = getTickShape(1.0f);
            g.fillPath(tick, tick.getTransformToScaleToFit(
                                iconArea.reduced(iconArea.getWidth() / 5,
                                                 iconArea.getHeight() / 5),
                                true));
        }

        if (hasSubMenu)
        {
            const float arrowH = 0.6f * font.getHeight();
            const float x = (float) r.removeFromRight((int) arrowH).getX();
            const float halfH = (float) r.getCentreY();

            juce::Path p;
            p.startNewSubPath(x, halfH - arrowH * 0.5f);
            p.lineTo(x + arrowH * 0.6f, halfH);
            p.lineTo(x, halfH + arrowH * 0.5f);
            g.strokePath(p, juce::PathStrokeType(2.0f));
        }

        r.removeFromRight(3);
        g.drawFittedText(text, r, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            auto f2 = font;
            f2.setHeight(f2.getHeight() * 0.85f);
            g.setFont(f2);
            g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
        }
    }
   #endif
};

MainComponent::MainComponent()
    : transportBar_(engine_)
{
    appLnF_ = std::make_unique<StylusLnF>();
    juce::LookAndFeel::setDefaultLookAndFeel(appLnF_.get());

   #if ! JUCE_MAC
    // Menu palette matches Win11's immersive dark title bar. Set on the
    // LnF (rather than just the menu bar component) so context menus and
    // dropdowns inherit the same colours.
    appLnF_->setColour(juce::PopupMenu::backgroundColourId,
                       juce::Colour(0xff202020));
    appLnF_->setColour(juce::PopupMenu::textColourId,
                       Color::textPrimary);
    appLnF_->setColour(juce::PopupMenu::highlightedBackgroundColourId,
                       juce::Colour(0xff2d2d2d));
    appLnF_->setColour(juce::PopupMenu::highlightedTextColourId,
                       Color::textPrimary);
   #endif

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

   #if ! JUCE_MAC
    // Windows host for the menu bar. Colours and font come from the app-wide
    // StylusLnF set above, so we don't need any per-component setColour or
    // setLookAndFeel calls here.
    addAndMakeVisible(menuBar_);
   #endif

    // Pin button (always-on-top toggle). Lives in the title-bar strip on the
    // right-hand side, styled to sit naturally next to the traffic lights on
    // the opposite side of the bar.
    pinButton_.icon        = TransportButton::Icon::Pin;
    pinButton_.toggleStyle = true;
    pinButton_.onClick     = [this] { toggleAlwaysOnTop(); };
    pinButton_.setTooltip("Always on top");
    addAndMakeVisible(pinButton_);

    // Normalize-volume toggle button. Mirrors the pin button on the bottom
    // side of the speaker icon: pin sits in the upper third of the
    // transport bar, the speaker in the middle, and this in the lower third.
    // Click toggles the same setting the Audio preferences exposes; both UIs
    // stay in sync via the wiring below.
    normalizeButton_.icon        = TransportButton::Icon::Normalize;
    normalizeButton_.toggleStyle = true;
    normalizeButton_.setTooltip("Normalize playback volume");
    normalizeButton_.onClick     = [this] {
        const bool on = ! engine_.isVolumeNormalizationEnabled();
        engine_.setVolumeNormalizationEnabled(on);
        normalizeButton_.toggleState = on ? 1 : 0;
        normalizeButton_.repaint();
        // Persist + sync the prefs panel toggle so opening Preferences shows
        // the matching state. The panel reads the setting on its next layout
        // and any open panel's toggle is updated directly.
        if (auto* s = appProperties_.getUserSettings())
            s->setValue(AudioPreferencesPanel::kNormalizeVolumeKey, on);
        if (auto* audPanel = preferencesWindow_->audioPanel())
            audPanel->setNormalizeVolumeChecked(on);
        // Mirror the panel-side behaviour: when toggling on with a track
        // already loaded that hasn't been analysed for loudness, kick off a
        // one-shot LUFS measurement so the offset takes effect on the
        // current playback rather than waiting for the next track.
        if (on)
        {
            const auto& t = engine_.currentTrack();
            if (t.lufs == 0.0f && t.file.existsAsFile())
                analysisEngine_.enqueueLufsOnly(t);
        }
        // Refresh the spinner / check-fade overlay immediately and (re)start
        // the animator so the visual tracks the audio ramp.
        updateNormalizeOverlay();
        normalizeAnimator_.startTimerHz(30);
    };
    addAndMakeVisible(normalizeButton_);

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
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_, individualTracks_);
    };

    // Right-click Go-to handlers. Skip the navigation when already in the
    // target view (avoids a redundant refreshCurrentView that can perturb
    // sort state); always re-focus on the right-clicked track in the
    // destination so the user can see what they navigated for.
    auto navigateAndFocusFile = [this](int targetId, juce::File trackFile) {
        if (activeSidebarId_ != targetId)
        {
            sidebar_.setSelectedId(targetId);
            showSidebarItem(targetId);
        }
        juce::MessageManager::callAsync([this, trackFile] {
            scrollSelectedSidebarItemIntoView();
            libraryTable_.scrollToFile(trackFile);
        });
    };

    libraryTable_.onGoToArtistRequested = [this, navigateAndFocusFile](TrackInfo t) {
        for (const auto& [id, name] : artistIdToName_)
            if (name == t.artist) { navigateAndFocusFile(id, t.file); return; }
    };
    libraryTable_.onGoToAlbumRequested = [this, navigateAndFocusFile](TrackInfo t) {
        for (const auto& [id, info] : albumIdToInfo_)
            if (info.artist == t.artist && info.album == t.album)
            {
                navigateAndFocusFile(id, t.file);
                return;
            }
    };
    libraryTable_.onGoToPodcastRequested = [this, navigateAndFocusFile](TrackInfo t) {
        for (const auto& [id, name] : podcastIdToName_)
            if (name == t.podcast) { navigateAndFocusFile(id, t.file); return; }
    };

    libraryTable_.onAddToQueueRequested = [this](std::vector<TrackInfo> tracks) {
        PlayQueue::QueueSource source;
        source.sidebarId = activeSidebarId_;
        source.name      = sourceNameForSidebar(activeSidebarId_);
        queue_.appendTracks(std::move(tracks), source);
    };

    libraryTable_.getPlaylistsForMenu = [this] {
        std::vector<std::pair<int, juce::String>> out;
        for (const auto& p : playlistStore_->all())
            out.emplace_back(p.id, p.name);
        return out;
    };
    libraryTable_.onAddToPlaylistRequested =
        [this](std::vector<TrackInfo> tracks, int playlistStoreId) {
            juce::StringArray paths;
            for (const auto& t : tracks)
                paths.add(t.file.getFullPathName());
            handleTracksDroppedOnPlaylist(1000 + playlistStoreId, paths);
        };

    libraryTable_.onCreateNewPlaylistRequested =
        [this](std::vector<TrackInfo> tracks) {
            if (tracks.empty()) return;

            // Intelligent name: same album across the selection wins,
            // otherwise same artist, otherwise fall back to "New Playlist".
            // Empty fields don't count - a selection that's all "no album"
            // shouldn't end up with an empty-string playlist name.
            juce::String suggested;
            const auto& first = tracks.front();
            const bool sameAlbum = ! first.album.isEmpty()
                && std::all_of(tracks.begin(), tracks.end(),
                    [&](const TrackInfo& t) { return t.album == first.album; });
            const bool sameArtist = ! first.artist.isEmpty()
                && std::all_of(tracks.begin(), tracks.end(),
                    [&](const TrackInfo& t) { return t.artist == first.artist; });
            if      (sameAlbum)  suggested = first.album;
            else if (sameArtist) suggested = first.artist;
            else                 suggested = "New Playlist";

            const int newId = playlistStore_->createPlaylist(suggested);
            std::vector<juce::String> paths;
            paths.reserve(tracks.size());
            for (const auto& t : tracks) paths.push_back(t.file.getFullPathName());
            playlistStore_->addTracksToPlaylist(newId, paths);

            const int newSidebarId = 1000 + newId;
            sidebar_.setSelectedId(newSidebarId);
            showSidebarItem(newSidebarId);
            juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
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
        // Library, queue, and sidebar focus are mutually exclusive: selecting
        // in one clears the others so the user only ever sees a highlight in
        // the pane that the delete key (and future keyboard nav) would act on.
        queueView_.deselectAll();
        sidebar_.clearFocus();
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
    // Title and "Playing from:" both navigate to the queue source view and
    // refocus the playing track. selectAndScrollToPlayingRow works there
    // because updatePlayingHighlight only sets playingFile_ on the source
    // view. Skipping the navigation when already at the target avoids a
    // redundant refreshCurrentView that can perturb sort state.
    auto navigateToSourceAndFocusPlaying = [this](int sidebarId) {
        if (activeSidebarId_ != sidebarId)
        {
            sidebar_.setSelectedId(sidebarId);
            showSidebarItem(sidebarId);
        }
        juce::MessageManager::callAsync([this] {
            scrollSelectedSidebarItemIntoView();
            libraryTable_.selectAndScrollToPlayingRow();
        });
    };
    transportBar_.onPlayingFromClicked = navigateToSourceAndFocusPlaying;
    transportBar_.onTitleClicked       = navigateToSourceAndFocusPlaying;
    // Click handler for the artist / album-art links. Resolves the target
    // sidebar id, navigates if we're not already there, and async-selects
    // the playing track's row by file path (works regardless of whether the
    // destination is the queue source view, unlike selectAndScrollToPlayingRow).
    // Skipping the navigation when activeSidebarId_ already matches avoids
    // a redundant refreshCurrentView round-trip that can perturb sort state.
    auto navigateAndHighlight = [this](int targetId, juce::File playingFile) {
        if (activeSidebarId_ != targetId)
        {
            sidebar_.setSelectedId(targetId);
            showSidebarItem(targetId);
        }
        juce::MessageManager::callAsync([this, playingFile] {
            scrollSelectedSidebarItemIntoView();
            libraryTable_.scrollToFile(playingFile);
        });
    };

    transportBar_.onArtistClicked = [this, navigateAndHighlight](TrackInfo t) {
        if (t.isPodcast)
        {
            for (const auto& [id, name] : podcastIdToName_)
                if (name == t.podcast) { navigateAndHighlight(id, t.file); return; }
        }
        else if (t.artist.isNotEmpty())
        {
            for (const auto& [id, name] : artistIdToName_)
                if (name == t.artist) { navigateAndHighlight(id, t.file); return; }
        }
    };
    transportBar_.onAlbumArtClicked = [this, navigateAndHighlight](TrackInfo t) {
        if (t.isPodcast)
        {
            for (const auto& [id, name] : podcastIdToName_)
                if (name == t.podcast) { navigateAndHighlight(id, t.file); return; }
        }
        else if (t.album.isNotEmpty())
        {
            for (const auto& [id, info] : albumIdToInfo_)
                if (info.artist == t.artist && info.album == t.album)
                {
                    navigateAndHighlight(id, t.file);
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
            const bool ok = LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_, individualTracks_);
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
    quitLockOverlay_.onShowQuit = [this] {
        if (auto* dlg = activeQuitDialog_.getComponent())
        {
            // The dialog can end up behind the main window or otherwise
            // hidden (some title-bar interactions, alt-tab, etc.). The
            // Show button drops it back on top — and re-centres it on
            // the main window — instead of forcing the user to cancel
            // and restart the quit flow.
            const auto mainScreen = getScreenBounds();
            dlg->setCentrePosition(mainScreen.getCentreX(),
                                   mainScreen.getCentreY());
            dlg->setVisible(true);
            dlg->toFront(true);
            applyDarkTitleBar(*dlg);
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

    // Wire the Display panel: persisted on toggle, applied immediately to the
    // transport bar. Initial state is read from settings and pushed below.
    if (auto* dispPanel = preferencesWindow_->displayPanel())
    {
        dispPanel->onUseStaticAlbumArtChanged = [this](bool useStatic) {
            transportBar_.setUseStaticAlbumArt(useStatic);
        };
    }
    if (auto* s = appProperties_.getUserSettings())
        transportBar_.setUseStaticAlbumArt(
            s->getBoolValue(DisplayPreferencesPanel::kUseStaticAlbumArtKey, false));

    // Wire the Audio panel: forwards "Normalize playback volume" toggles
    // straight to the engine, which mixes the per-track LUFS-based offset
    // with the user volume on the next loadTrack and live on toggle.
    if (auto* audPanel = preferencesWindow_->audioPanel())
    {
        audPanel->onNormalizeVolumeChanged = [this](bool on) {
            engine_.setVolumeNormalizationEnabled(on);
            // Keep the transport-bar mirror button in sync.
            normalizeButton_.toggleState = on ? 1 : 0;
            normalizeButton_.repaint();
            // When normalisation flips on with a track already loaded that
            // hasn't been analysed for loudness, kick off a one-shot LUFS
            // measurement so the toggle takes effect on the current
            // playback rather than waiting for the next track.
            if (on)
            {
                const auto& t = engine_.currentTrack();
                if (t.lufs == 0.0f && t.file.existsAsFile())
                    analysisEngine_.enqueueLufsOnly(t);
            }
            // Same overlay refresh as the transport-bar button click.
            updateNormalizeOverlay();
            normalizeAnimator_.startTimerHz(30);
        };
    }
    if (auto* s = appProperties_.getUserSettings())
    {
        const bool normOn = s->getBoolValue(AudioPreferencesPanel::kNormalizeVolumeKey, false);
        engine_.setVolumeNormalizationEnabled(normOn);
        normalizeButton_.toggleState = normOn ? 1 : 0;
        // Initial overlay sync: no track yet means spinner is off and check
        // is at 0; that's exactly what we want for the no-track resting
        // state. Once a track loads, onTrackStarted refreshes this.
        updateNormalizeOverlay();
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
        // Push the result to the engine when it targets the currently-
        // playing track. A non-zero value updates the per-track gain
        // offset; a zero value tells the engine analysis ran but couldn't
        // produce a measurement, so the -3 dB pre-roll falls back to
        // unity rather than holding for the rest of the track.
        if (engine_.currentTrack().file == analysed.file)
        {
            if (analysed.lufs != 0.0f)
                engine_.updateCurrentTrackLufs(analysed.lufs);
            else
                engine_.markLufsAnalysisFailed();
            // Either path settles the pending state: turn the spinner off,
            // start the check-fade (or stay off for failed analysis). The
            // animator will tick through the fade and self-stop.
            updateNormalizeOverlay();
            normalizeAnimator_.startTimerHz(30);
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

    queueView_.onSelectionChanged = [this] {
        // Mirror image of libraryTable_.onSelectionChanged: clear the library
        // selection and sidebar focus when the user picks a queue row, so
        // the delete key (and future keyboard nav) acts unambiguously on
        // the queue.
        libraryTable_.deselectAll();
        sidebar_.clearFocus();
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
    sidebar_.onItemSelected = [this](int id) {
        showSidebarItem(id);
        // Keyboard nav can move the focus to a row that's scrolled
        // off-screen; mouse clicks already land on visible rows so this
        // is a no-op there. Async to defer past any layout that
        // showSidebarItem may have triggered.
        juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
    };

    // Cross-pane keyboard nav. The sidebar / library / queue route arrow
    // and Tab keys through these callbacks; MainComponent decides which
    // pane actually receives focus and whether to show the queue first.
    auto focusSidebar = [this] {
        // grabKeyboardFocus() lands JUCE focus on SidebarComponent;
        // focusGained() then re-asserts the gray-overlay focus indicator
        // and clears library / queue visual selection via the wired
        // onFocusGained handler.
        sidebar_.grabKeyboardFocus();
    };
    auto focusLibrary = [this] {
        // Explicit visual mutex: clear queue / sidebar highlight so only
        // the library shows a selected row once it gains focus. The
        // onSelectionChanged-based mutex doesn't catch this path because
        // focusTable restores via setSelectedFiles (dontSendNotification)
        // and may not call selectRow at all if the saved selection is
        // already the visible one.
        queueView_.deselectAll();
        sidebar_.clearFocus();
        libraryTable_.focusTable();
    };
    auto focusQueueIfPossible = [this] {
        // No queue rows = nothing to focus. Match the user spec for the
        // Right / Tab path from the library: silent no-op when empty.
        if (queue_.size() == 0) return;
        if (! queueVisible_) toggleQueue();
        // Same explicit mutex as focusLibrary - the queue's focusList may
        // not call selectRow (when the saved row is already current) and
        // therefore wouldn't fire onSelectionChanged on its own.
        libraryTable_.deselectAll();
        sidebar_.clearFocus();
        queueView_.focusList();
    };

    sidebar_.onMoveFocusToLibrary = focusLibrary;
    sidebar_.onMoveFocusToQueue   = focusQueueIfPossible;
    libraryTable_.onMoveFocusToSidebar = focusSidebar;
    libraryTable_.onMoveFocusToQueue   = focusQueueIfPossible;
    queueView_.onMoveFocusToLibrary    = focusLibrary;
    queueView_.onMoveFocusToSidebar    = focusSidebar;

    // Sidebar focusGained fires whenever JUCE focus lands on the sidebar
    // (click, Left arrow back from library, Tab cycle). Mirror the existing
    // library / queue onSelectionChanged handlers: clear the other panes'
    // selections so only the focused pane shows a highlight.
    sidebar_.onFocusGained = [this] {
        libraryTable_.deselectAll();
        queueView_.deselectAll();
    };
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

    // Bulk-rename helper: walk fullLibrary_, mutate every track that
    // matches the predicate, persist each one's .styl, push the change to
    // the library table, then refresh every sidebar section that derives
    // from the changed metadata. One refresh after the loop instead of N.
    auto bulkRenameTracks = [this](auto matchFn, auto mutateFn) {
        bool anyChanged = false;
        for (auto& t : fullLibrary_)
        {
            if (!matchFn(t)) continue;
            mutateFn(t);
            StylFile::save(t);
            libraryTable_.updateTrack(t);
            anyChanged = true;
        }
        if (!anyChanged) return false;
        refreshSidebarArtists();
        refreshSidebarAlbums();
        refreshSidebarGenres();
        refreshSidebarPodcasts();
        return true;
    };

    // Find a sidebar id by scanning a (id -> name) map for a matching name;
    // returns -1 if not found. Used to remap activeSidebarId_ after a rename
    // shifts entries in the sorted sidebar list.
    auto findIdByName = [](const auto& map, const juce::String& name) -> int {
        for (const auto& [id, n] : map) if (n == name) return id;
        return -1;
    };

    sidebar_.onRenameArtist = [this, bulkRenameTracks, findIdByName]
        (int sidebarId, juce::String newName) {
            auto it = artistIdToName_.find(sidebarId);
            if (it == artistIdToName_.end()) return;
            const juce::String oldName = it->second;
            if (newName == oldName) return;

            const bool changed = bulkRenameTracks(
                [&](const TrackInfo& t) { return ! t.isPodcast && t.artist == oldName; },
                [&](TrackInfo& t)       { t.artist = newName; });
            if (! changed) return;

            // Stay on the renamed artist's row (its id may have shifted
            // because the alphabetic position changed; or the rename may
            // have merged into an existing artist with the same new name).
            if (activeSidebarId_ == sidebarId
                || (activeSidebarId_ >= 2000 && activeSidebarId_ < 3000))
            {
                const int newId = findIdByName(artistIdToName_, newName);
                if (newId > 0)
                {
                    activeSidebarId_ = newId;
                    sidebar_.setSelectedId(newId);
                }
            }
            refreshCurrentView();

            // Queue source name fixup: if the now-playing source was this
            // artist, point it at the renamed entry so "Playing from:" stays
            // accurate. Same idea for the queue's stored source.
            if (queue_.hasCurrent())
            {
                auto qsrc = queue_.currentSource();
                if (qsrc.sidebarId >= 2000 && qsrc.sidebarId < 3000 && qsrc.name == oldName)
                {
                    const int newId = findIdByName(artistIdToName_, newName);
                    if (newId > 0)
                    {
                        transportBar_.setPlayingFrom(newName, newId);
                    }
                }
            }
        };

    sidebar_.onRenameAlbum = [this, bulkRenameTracks]
        (int sidebarId, juce::String newArtist, juce::String newAlbum) {
            auto it = albumIdToInfo_.find(sidebarId);
            if (it == albumIdToInfo_.end()) return;
            const juce::String oldArtist = it->second.artist;
            const juce::String oldAlbum  = it->second.album;
            if (newArtist == oldArtist && newAlbum == oldAlbum) return;

            const bool changed = bulkRenameTracks(
                [&](const TrackInfo& t) {
                    return ! t.isPodcast && t.artist == oldArtist && t.album == oldAlbum;
                },
                [&](TrackInfo& t) {
                    t.artist = newArtist;
                    t.album  = newAlbum;
                });
            if (! changed) return;

            if (activeSidebarId_ == sidebarId
                || (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000))
            {
                int newId = -1;
                for (const auto& [id, info] : albumIdToInfo_)
                    if (info.artist == newArtist && info.album == newAlbum) { newId = id; break; }
                if (newId > 0)
                {
                    activeSidebarId_ = newId;
                    sidebar_.setSelectedId(newId);
                }
            }
            refreshCurrentView();

            // Queue source: if the now-playing source was this album, point
            // it at the renamed entry. The label format "Artist - Album"
            // matches what the sidebar uses.
            if (queue_.hasCurrent())
            {
                auto qsrc = queue_.currentSource();
                if (qsrc.sidebarId >= 3000 && qsrc.sidebarId < 4000)
                {
                    const juce::String oldLabel =
                        (oldArtist.isNotEmpty() ? oldArtist : juce::String("Unknown Artist"))
                        + " - " + oldAlbum;
                    if (qsrc.name == oldLabel)
                    {
                        for (const auto& [id, info] : albumIdToInfo_)
                            if (info.artist == newArtist && info.album == newAlbum)
                            {
                                const juce::String newLabel =
                                    (newArtist.isNotEmpty() ? newArtist : juce::String("Unknown Artist"))
                                    + " - " + newAlbum;
                                transportBar_.setPlayingFrom(newLabel, id);
                                break;
                            }
                    }
                }
            }
        };

    sidebar_.onRenameGenre = [this, bulkRenameTracks, findIdByName]
        (int sidebarId, juce::String newName) {
            auto it = genreIdToName_.find(sidebarId);
            if (it == genreIdToName_.end()) return;
            const juce::String oldName = it->second;
            if (newName == oldName) return;

            const bool changed = bulkRenameTracks(
                [&](const TrackInfo& t) { return ! t.isPodcast && t.genre == oldName; },
                [&](TrackInfo& t)       { t.genre = newName; });
            if (! changed) return;

            if (activeSidebarId_ == sidebarId
                || (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000))
            {
                const int newId = findIdByName(genreIdToName_, newName);
                if (newId > 0)
                {
                    activeSidebarId_ = newId;
                    sidebar_.setSelectedId(newId);
                }
            }
            refreshCurrentView();

            if (queue_.hasCurrent())
            {
                auto qsrc = queue_.currentSource();
                if (qsrc.sidebarId >= 5000 && qsrc.sidebarId < 6000 && qsrc.name == oldName)
                {
                    const int newId = findIdByName(genreIdToName_, newName);
                    if (newId > 0)
                    {
                        transportBar_.setPlayingFrom(newName, newId);
                    }
                }
            }
        };

    sidebar_.onRenamePodcast = [this, bulkRenameTracks, findIdByName]
        (int sidebarId, juce::String newName) {
            auto it = podcastIdToName_.find(sidebarId);
            if (it == podcastIdToName_.end()) return;
            const juce::String oldName = it->second;
            if (newName == oldName) return;

            const bool changed = bulkRenameTracks(
                [&](const TrackInfo& t) { return t.isPodcast && t.podcast == oldName; },
                [&](TrackInfo& t)       { t.podcast = newName; });
            if (! changed) return;

            if (activeSidebarId_ == sidebarId
                || (activeSidebarId_ >= 4000 && activeSidebarId_ < 5000))
            {
                const int newId = findIdByName(podcastIdToName_, newName);
                if (newId > 0)
                {
                    activeSidebarId_ = newId;
                    sidebar_.setSelectedId(newId);
                }
            }
            refreshCurrentView();

            if (queue_.hasCurrent())
            {
                auto qsrc = queue_.currentSource();
                if (qsrc.sidebarId >= 4000 && qsrc.sidebarId < 5000 && qsrc.name == oldName)
                {
                    const int newId = findIdByName(podcastIdToName_, newName);
                    if (newId > 0)
                    {
                        transportBar_.setPlayingFrom(newName, newId);
                    }
                }
            }
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
                return a.displayTitle().compareNatural(b.displayTitle()) < 0;
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
                    return a.displayTitle().compareNatural(b.displayTitle()) < 0;
                const int albumCmp = a.album.compareNatural(b.album);
                if (albumCmp != 0) return albumCmp < 0;
                const bool aNum = a.trackNumber > 0;
                const bool bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareNatural(b.displayTitle()) < 0;
            });
        }

        const int newId = playlistStore_->createPlaylist(name);
        std::vector<juce::String> paths;
        paths.reserve(tracks.size());
        for (const auto& t : tracks) paths.push_back(t.file.getFullPathName());
        playlistStore_->addTracksToPlaylist(newId, paths);

        // Jump to the freshly-created playlist so the user immediately sees
        // the tracks they just collected, instead of leaving them on the
        // source view wondering whether the action did anything. Scroll
        // happens async so the sidebar's reflow has run before we ask it
        // to bring the new row on-screen.
        const int newSidebarId = 1000 + newId;
        sidebar_.setSelectedId(newSidebarId);
        showSidebarItem(newSidebarId);
        juce::MessageManager::callAsync([this] { scrollSelectedSidebarItemIntoView(); });
    };

    auto sortTracksForSidebar = [](std::vector<TrackInfo>& tracks, int sidebarId) {
        if (sidebarId >= 3000 && sidebarId < 4000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNum = a.trackNumber > 0, bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareNatural(b.displayTitle()) < 0;
            });
        }
        else if (sidebarId >= 2000 && sidebarId < 3000)
        {
            std::sort(tracks.begin(), tracks.end(), [](const TrackInfo& a, const TrackInfo& b) {
                const bool aNoAlbum = a.album.isEmpty(), bNoAlbum = b.album.isEmpty();
                if (aNoAlbum != bNoAlbum) return aNoAlbum > bNoAlbum;
                if (aNoAlbum) return a.displayTitle().compareNatural(b.displayTitle()) < 0;
                const int albumCmp = a.album.compareNatural(b.album);
                if (albumCmp != 0) return albumCmp < 0;
                const bool aNum = a.trackNumber > 0, bNum = b.trackNumber > 0;
                if (aNum != bNum) return aNum > bNum;
                if (aNum && a.trackNumber != b.trackNumber) return a.trackNumber < b.trackNumber;
                return a.displayTitle().compareNatural(b.displayTitle()) < 0;
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

    sidebar_.getPlaylistsForMenu = [this] {
        std::vector<std::pair<int, juce::String>> out;
        for (const auto& p : playlistStore_->all())
            out.emplace_back(p.id, p.name);
        return out;
    };
    sidebar_.onAddToPlaylistFromItem =
        [this, sortTracksForSidebar](int sourceSidebarId, int destPlaylistStoreId) {
            auto tracks = getTracksForSidebar(sourceSidebarId);
            if (tracks.empty()) return;
            sortTracksForSidebar(tracks, sourceSidebarId);
            juce::StringArray paths;
            for (const auto& t : tracks)
                paths.add(t.file.getFullPathName());
            handleTracksDroppedOnPlaylist(1000 + destPlaylistStoreId, paths);
        };

    refreshSidebarPlaylists();

    setupAudioEngineCallbacks();

    // Restore saved podcast folders.
    podcastFolders_ = loadSavedPodcastFolders();
    if (auto* libPanel = preferencesWindow_->libraryPanel())
        libPanel->setPodcastFolders(podcastFolders_);

    // Restore saved loose tracks (drag-dropped or "Open With" added).
    individualTracks_ = loadSavedIndividualTracks();

    // Restore saved music folders (or show the empty prompt if none).
    auto savedFolders = loadSavedMusicFolders();

    if (! savedFolders.empty() || ! individualTracks_.empty())
    {
        // Try the on-disk library cache first so the UI populates instantly
        // (sidebar / library table / queue restore) without waiting for the
        // background scan to finish. The scan still runs to pick up any
        // changes since the cache was written.
        std::vector<TrackInfo>   cachedTracks;
        std::vector<juce::File>  cachedFolders;
        std::vector<juce::File>  cachedPodcastFolders;
        std::vector<juce::File>  cachedIndividualTracks;
        bool cacheUsed = false;
        try
        {
            // Cache is only reusable when ALL three input lists match the
            // current state. If any list has changed (folder added/removed,
            // loose file added since last save), fall through to a fresh
            // scan instead of showing a stale snapshot.
            if (LibraryCache::tryLoad(cachedTracks, cachedFolders,
                                       cachedPodcastFolders, cachedIndividualTracks)
                && cachedFolders          == savedFolders
                && cachedPodcastFolders   == podcastFolders_
                && cachedIndividualTracks == individualTracks_)
            {
                DBG("LibraryCache loaded "
                    + juce::String((int) cachedTracks.size()) + " tracks");
                fullLibrary_ = std::move(cachedTracks);
                libraryTable_.setTracks(fullLibrary_);
                refreshSidebarArtists();
                refreshSidebarAlbums();
                refreshSidebarGenres();
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
                if (updated.artist.isEmpty())
                    newId = UIConstants::noArtistId;
                else
                    for (const auto& [id, name] : artistIdToName_)
                        if (name == updated.artist) { newId = id; break; }
            }
            else if (activeSidebarId_ >= 3000 && activeSidebarId_ < 4000)
            {
                if (updated.album.isEmpty())
                    newId = UIConstants::noAlbumId;
                else
                    for (const auto& [id, info] : albumIdToInfo_)
                        if (info.artist == updated.artist && info.album == updated.album)
                            { newId = id; break; }
            }
            else if (activeSidebarId_ >= 5000 && activeSidebarId_ < 6000)
            {
                if (updated.genre.isEmpty())
                    newId = UIConstants::noGenreId;
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

    // Two sticky zones overlay the top of the viewport when scrolled:
    // the LIBRARY section pins at the top, and the active section's
    // header (e.g. "ARTISTS") pins right below it. A scroll-into-view
    // nudge has to subtract both occlusion heights so the focused row
    // ends up below them, not behind them.
    const int rowTop    = itemBounds.getY();
    const int rowBottom = rowTop + itemBounds.getHeight();
    const int libH      = sidebar_.libraryStickyHeight();
    const int sticky    = sidebar_.topStickyHeightFor(rowTop);
    const bool inLibrary = (rowBottom <= libH);

    // LIBRARY rows are the sticky overlay - always visible regardless of
    // scroll. No nudge needed.
    if (inLibrary) return;

    const int viewTop    = sidebarViewport_.getViewPositionY();
    const int viewBottom = viewTop + visibleH;
    const int effTop     = viewTop + sticky;

    // Minimum-movement scroll: leave the row alone if it's already fully
    // visible (clear of sticky occlusion above and not falling off the
    // bottom), otherwise nudge just enough to bring its top or bottom
    // edge onto the viewport.
    int targetY = viewTop;
    if (rowTop < effTop)
        targetY = rowTop - sticky;
    else if (rowBottom > viewBottom)
        targetY = rowBottom - visibleH;
    else
        return;   // already visible

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
        if (sidebarId == UIConstants::noArtistId)
        {
            for (const auto& t : fullLibrary_)
                if (! t.isPodcast && t.artist.isEmpty()) result.push_back(t);
        }
        else
        {
            const auto it = artistIdToName_.find(sidebarId);
            if (it != artistIdToName_.end())
                for (const auto& t : fullLibrary_)
                    if (t.artist == it->second) result.push_back(t);
        }
    }
    else if (sidebarId >= 3000 && sidebarId < 4000)
    {
        if (sidebarId == UIConstants::noAlbumId)
        {
            for (const auto& t : fullLibrary_)
                if (! t.isPodcast && t.album.isEmpty()) result.push_back(t);
        }
        else
        {
            const auto it = albumIdToInfo_.find(sidebarId);
            if (it != albumIdToInfo_.end())
            {
                const auto& [artist, album] = it->second;
                for (const auto& t : fullLibrary_)
                    if (t.album == album && t.artist == artist) result.push_back(t);
            }
        }
    }
    else if (sidebarId >= 5000 && sidebarId < 6000)
    {
        if (sidebarId == UIConstants::noGenreId)
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
    // as the sort key so the sidebar order matches the label order. Tracks
    // with an empty album field don't get a real row; they're collected
    // into the "(no album)" bucket at the top of the section instead.
    struct Item { juce::String artist, album, label; };
    std::vector<Item> items;
    std::set<juce::String> seen;
    bool hasNoAlbum = false;
    for (const auto& t : fullLibrary_)
    {
        if (t.isPodcast) continue;
        if (t.album.isEmpty())
        {
            hasNoAlbum = true;
            continue;
        }
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

    if (hasNoAlbum)
    {
        albumIdToInfo_[UIConstants::noAlbumId] = { {}, {} };
        sidebarItems.push_back({ UIConstants::noAlbumId, "(no album)" });
    }

    int id = 3000;
    for (const auto& it : items)
    {
        if (id >= UIConstants::noAlbumId) break;   // leave 3999 reserved
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

    // Drop any loose tracks now covered by a podcast root so the scanner
    // doesn't double-handle them. Mirrors the music-root cleanup in
    // addMusicFolderRoots.
    const auto before = individualTracks_.size();
    individualTracks_.erase(
        std::remove_if(individualTracks_.begin(), individualTracks_.end(),
            [this](const juce::File& f) {
                for (const auto& r : podcastFolders_)
                    if (f.isAChildOf(r)) return true;
                return false;
            }),
        individualTracks_.end());
    if (individualTracks_.size() != before)
        saveIndividualTracks();

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

    // Detect whether any non-podcast tracks lack an artist tag; those land
    // in the "(no artist)" bucket row at the top of the section. Real
    // artists fill the rest of the range.
    bool hasNoArtist = false;
    std::set<juce::String> unique;
    for (const auto& t : fullLibrary_)
    {
        if (t.isPodcast) continue;
        if (t.artist.isEmpty()) hasNoArtist = true;
        else                    unique.insert(t.artist);
    }

    std::vector<juce::String> sorted(unique.begin(), unique.end());
    std::sort(sorted.begin(), sorted.end(), [](const juce::String& a, const juce::String& b) {
        return a.compareNatural(b, false) < 0;
    });

    artistIdToName_.clear();
    std::vector<std::pair<int, juce::String>> items;

    if (hasNoArtist)
    {
        artistIdToName_[UIConstants::noArtistId] = {};
        items.push_back({ UIConstants::noArtistId, "(no artist)" });
    }

    int id = 2000;
    for (const auto& name : sorted)
    {
        if (id >= UIConstants::noArtistId) break;   // leave 2999 reserved
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
        genreIdToName_[UIConstants::noGenreId] = {};
        items.push_back({ UIConstants::noGenreId, "(no genre)" });
    }

    int id = 5000;
    for (const auto& name : sorted)
    {
        if (id >= UIConstants::noGenreId) break;   // leave 5999 reserved
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

    // Force the overlay's dim to render BEFORE the OS dialog window is
    // shown below: setVisible on the overlay only queues a repaint, and
    // dw->setVisible(true) blocks the message loop while creating the
    // HWND. Without this flush the dialog pops up over an undimmed
    // background and the dim catches up a frame later, which reads as
    // sluggish.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();

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

    // (no X) bucket views are a special workflow: the user is methodically
    // filling in a missing field across many tracks. After save, the edited
    // track no longer belongs in the bucket, so we deliberately keep the
    // active view pinned to the bucket (followTrack=false) and route the
    // selection / Next / Prev navigation to whichever track took the just-
    // vacated slot. The captured peerIndex is the pre-save position of the
    // edited row in the bucket's visible order.
    const bool inNoBucketView = (activeSidebarId_ == UIConstants::noGenreId
                              || activeSidebarId_ == UIConstants::noArtistId
                              || activeSidebarId_ == UIConstants::noAlbumId);
    const int  capturedPeerIndex = peerIndex;

    editor->onSave = [this, inNoBucketView, capturedPeerIndex](std::vector<TrackInfo> updated) {
        for (auto& t : updated) {
            StylFile::save(t);
            // In a (no X) bucket view we don't want to chase the edited
            // track out into its new home: the user wants to stay put and
            // keep working through the bucket. updateTrackInLibrary with
            // followTrack=false leaves activeSidebarId_ alone.
            updateTrackInLibrary(t, /*followTrack*/ ! inNoBucketView);
        }
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_, individualTracks_);

        // After the (no X) refresh, the edited track has dropped out of
        // visibleTracks(). Move the library selection to whichever track
        // now occupies the pre-edit slot, so the user has visual context
        // for "what's next". If the bucket is now empty (or the slot is
        // past the end), clear the selection instead.
        if (inNoBucketView)
        {
            const auto& peers = libraryTable_.visibleTracks();
            if (capturedPeerIndex >= 0
                && capturedPeerIndex < (int) peers.size())
            {
                libraryTable_.scrollToFile(peers[(size_t) capturedPeerIndex].file);
            }
            else
            {
                libraryTable_.setSelectedFiles({});
            }
        }
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
        editor->onSaveAndNavigate = [safeThis, closeDialog, inNoBucketView,
                                     capturedPeerIndex](int delta) {
            auto* self = safeThis.getComponent();
            if (self == nullptr) return;

            // Recompute peers from the current view at navigate time. An Apple
            // Music lookup may have changed the album/artist/genre and shifted
            // the track to a new position in the sort order, using the
            // snapshot captured when the dialog opened would walk the wrong
            // neighbours.
            auto currentPeers = self->libraryTable_.visibleTracks();
            if (currentPeers.empty())
            {
                closeDialog(true);   // bucket emptied: dismiss
                return;
            }

            int newIndex = -1;
            int currentIdx = -1;
            for (int i = 0; i < (int) currentPeers.size(); ++i)
                if (currentPeers[(size_t) i].file == self->lastEditedInfoFile_)
                    { currentIdx = i; break; }

            if (currentIdx >= 0)
            {
                // Track is still in this view (no field that affected the
                // view's filter changed). Step forward / back from its
                // live position.
                newIndex = currentIdx + delta;
            }
            else if (inNoBucketView)
            {
                // Track has dropped out of a (no X) bucket because the user
                // just supplied the missing field. The slot the edited row
                // held has been backfilled by what was originally at
                // index+1, so for Next we land on capturedPeerIndex (the
                // same numeric slot, now showing a different file). For
                // Prev we step one slot earlier, which is unchanged by the
                // removal.
                newIndex = (delta > 0)
                             ? capturedPeerIndex
                             : capturedPeerIndex - 1;
            }
            else
            {
                return;  // edited track left a non-bucket view; nothing to do
            }

            if (newIndex < 0 || newIndex >= (int) currentPeers.size())
            {
                // No track left in the user's direction of travel; dismiss
                // the dialog rather than wrapping silently.
                closeDialog(true);
                return;
            }

            closeDialog(false); // close old dialog without hiding the overlay

            TrackInfo trackToEdit = currentPeers[(size_t) newIndex];
            for (const auto& t : self->fullLibrary_)
                if (t.file == trackToEdit.file) { trackToEdit = t; break; }
            self->showSongInfoEditor(trackToEdit, currentPeers, newIndex);
        };
    }

    // Pre-create the peer with the HWND hidden, set the dark title bar,
    // then show. Without this dance the dialog appears with the default
    // light title bar for one frame before our DwmSetWindowAttribute
    // call darkens it.
    dw->setVisible(false);
    dw->addToDesktop(dw->getDesktopWindowStyleFlags());
    applyDarkTitleBar(*dw);
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

    // See comment in showSongInfoEditor: flush pending repaints so the
    // dim lands on-screen before the OS dialog window blocks the loop.
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();

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
        LibraryCache::save(fullLibrary_, musicFolders_, podcastFolders_, individualTracks_);
    };

    editor->onDismiss = closeDialog;
    dw->onDismiss     = closeDialog;

    // See showSongInfoEditor: pre-create peer hidden, set dark, then show.
    dw->setVisible(false);
    dw->addToDesktop(dw->getDesktopWindowStyleFlags());
    applyDarkTitleBar(*dw);
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

        // Lazy LUFS-on-first-play: when the user has volume normalisation
        // enabled, every track they play that hasn't been analysed for
        // loudness yet kicks off a background measurement so the engine
        // can apply a gain offset within a second or two. Once measured,
        // the result is saved to .styl and applied via the onTrackAnalysed
        // hook above. Applies to podcasts too: with normalisation on, the
        // user wants consistent levels regardless of content type.
        if (engine_.isVolumeNormalizationEnabled() && track.lufs == 0.0f)
            analysisEngine_.enqueueLufsOnly(track);

        // Refresh the spinner / check-fade overlay for the new track and
        // restart the animator. A track with lufs already known settles the
        // animator at full alpha within one tick; an unmeasured track turns
        // the spinner on.
        updateNormalizeOverlay();
        normalizeAnimator_.startTimerHz(30);
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

   #if ! JUCE_MAC
    // Paint the menu-bar background colour across the full top strip even
    // in the leading gap before the first menu item, so the dark chrome
    // (title bar, leading pad, menu items) all reads as one continuous band.
    g.setColour(juce::Colour(0xff202020));
    g.fillRect(0, 0, getWidth(), UIConstants::menuBarHeight);
   #endif
}

MainComponent::PrefsLockOverlay::PrefsLockOverlay()
{
    // Capture every click so nothing underneath is reachable.
    setInterceptsMouseClicks(true, true);

    message_.setFont(juce::Font(juce::FontOptions().withHeight(18.0f)).boldened());
    message_.setColour(juce::Label::textColourId, UIConstants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
        b.setColour(juce::TextButton::textColourOffId, UIConstants::Color::textPrimary);
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

    g.setColour(UIConstants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);

    g.setColour(UIConstants::Color::border);
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
    message_.setColour(juce::Label::textColourId, UIConstants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
        b.setColour(juce::TextButton::textColourOffId, UIConstants::Color::textPrimary);
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

    g.setColour(UIConstants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);

    g.setColour(UIConstants::Color::border);
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
    message_.setColour(juce::Label::textColourId, UIConstants::Color::textPrimary);
    message_.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(message_);

    auto styleButton = [](juce::TextButton& b) {
        b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
        b.setColour(juce::TextButton::textColourOffId, UIConstants::Color::textPrimary);
    };
    styleButton(showBtn_);
    styleButton(cancelBtn_);
    showBtn_.onClick   = [this] { if (onShowQuit)   onShowQuit(); };
    cancelBtn_.onClick = [this] { if (onCancelQuit) onCancelQuit(); };
    addAndMakeVisible(showBtn_);
    addAndMakeVisible(cancelBtn_);
}

void MainComponent::QuitLockOverlay::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colours::black.withAlpha(0.55f));

    constexpr int msgH   = 28;
    constexpr int gap    = 12;
    constexpr int btnH   = 32;
    constexpr int btnW   = 140;
    constexpr int btnGap = 12;
    constexpr int padX   = 22;
    constexpr int padY   = 16;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    const auto panel = getLocalBounds()
                          .withSizeKeepingCentre(groupW + padX * 2,
                                                 groupH + padY * 2)
                          .toFloat();

    g.setColour(UIConstants::Color::background);
    g.fillRoundedRectangle(panel, 8.0f);
    g.setColour(UIConstants::Color::border);
    g.drawRoundedRectangle(panel.reduced(0.5f), 8.0f, 1.0f);
}

void MainComponent::QuitLockOverlay::resized()
{
    constexpr int msgH   = 28;
    constexpr int gap    = 12;
    constexpr int btnH   = 32;
    constexpr int btnW   = 140;
    constexpr int btnGap = 12;

    const int groupW = btnW * 2 + btnGap;
    const int groupH = msgH + gap + btnH;
    auto area = getLocalBounds().withSizeKeepingCentre(groupW, groupH);

    message_.setBounds(area.removeFromTop(msgH));
    area.removeFromTop(gap);
    auto row = area.removeFromTop(btnH);
    showBtn_.setBounds(row.removeFromLeft(btnW));
    row.removeFromLeft(btnGap);
    cancelBtn_.setBounds(row.removeFromLeft(btnW));
}

void MainComponent::resized()
{
    auto bounds = getLocalBounds();

   #if ! JUCE_MAC
    // Windows menu bar strip at the very top, rendered by JUCE so it
    // matches the dark theme of the rest of the window. Leading pad means
    // "File" doesn't sit flush against the window's left edge; the gap
    // matches the inter-item gap so the bar reads as evenly spaced. The
    // strip-coloured fill behind the leading gap is drawn in paint().
    // Each menu-bar item carries half of getMenuBarItemWidth's +14 padding
    // on its leading side (i.e. ~7 px of internal pad before its text),
    // so the OUTER offset is half the inter-item gap to make the visual
    // distance "screen edge → File text" equal "File text → Window text".
    constexpr int menuLeadingPx = 7;
    auto topStrip = bounds.removeFromTop(UIConstants::menuBarHeight);
    menuBar_.setBounds(topStrip.withTrimmedLeft(menuLeadingPx));
   #endif

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

    // Normalize button: mirror image of the pin in the bottom third, so the
    // speaker icon sits between the two right-side mod toggles. Slightly
    // larger than the pin so the sliders + green checkmark composition has
    // enough room to read clearly without crowding.
    constexpr int normSize = 21;
    const int normX = getWidth() - speakerCentreFromRight - normSize / 2 - 1;
    const int normY = transportBar_.getY() + 2 * thirdH + (thirdH - normSize) / 2 - 5;
    normalizeButton_.setBounds(normX, normY, normSize, normSize);
    normalizeButton_.toFront(false);

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
        refreshSidebarGenres();
        refreshSidebarPodcasts();
    }

    if (musicFolders_.empty() && individualTracks_.empty())
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
    scanner_.scanFolders(musicFolders_, podcastFolders_, individualTracks_);
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

void MainComponent::saveIndividualTracks()
{
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return;

    juce::StringArray paths;
    for (const auto& f : individualTracks_)
        paths.add(f.getFullPathName());
    props->setValue("individualTracks", paths.joinIntoString("\n"));
}

std::vector<juce::File> MainComponent::loadSavedIndividualTracks()
{
    std::vector<juce::File> result;
    auto* props = appProperties_.getUserSettings();
    if (props == nullptr) return result;

    const juce::String joined = props->getValue("individualTracks");
    if (joined.isEmpty()) return result;

    juce::StringArray paths;
    paths.addTokens(joined, "\n", "");
    bool removedAny = false;
    for (const auto& p : paths)
    {
        if (p.isEmpty()) continue;
        const juce::File f(p);
        // Drop entries whose file is no longer on disk. Without this they'd
        // accumulate forever (the scanner silently skips missing files, but
        // the path would still ride in individualTracks_ across launches).
        if (! f.existsAsFile())
        {
            removedAny = true;
            continue;
        }
        result.push_back(f);
    }
    if (removedAny)
    {
        // Persist the pruned list immediately so a subsequent crash doesn't
        // re-resurrect the stale entries on the next launch.
        juce::StringArray remaining;
        for (const auto& f : result) remaining.add(f.getFullPathName());
        props->setValue("individualTracks", remaining.joinIntoString("\n"));
    }
    return result;
}

std::vector<juce::File> MainComponent::collectAudioFilesUnder(const juce::File& folder)
{
    std::vector<juce::File> out;
    if (! folder.isDirectory()) return out;

    juce::Array<juce::File> all;
    folder.findChildFiles(all, juce::File::findFiles, true);
    for (const auto& f : all)
    {
        // Skip hidden segments (any path component starting with '.') and
        // unsupported extensions, matching LibraryScanner's behaviour.
        bool hidden = false;
        juce::File cur = f;
        while (cur != folder)
        {
            if (cur.getFileName().startsWith(".")) { hidden = true; break; }
            cur = cur.getParentDirectory();
        }
        if (hidden) continue;
        if (! Constants::supportedExtensions.contains(
                f.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
            continue;
        out.push_back(f);
    }
    return out;
}

int MainComponent::addIndividualTracks(const std::vector<juce::File>& files)
{
    // Cache canonicalised paths up front so repeated comparisons against the
    // existing lists don't re-resolve symlinks for each candidate. The
    // canonical path is used purely for dedup; the original juce::File is
    // what we store and show to the user.
    auto canonOf = [](const juce::File& f) { return canonicalizeFile(f); };

    std::vector<juce::File> canonExisting;
    canonExisting.reserve(individualTracks_.size());
    for (const auto& e : individualTracks_) canonExisting.push_back(canonOf(e));

    std::vector<juce::File> canonMusicRoots;
    canonMusicRoots.reserve(musicFolders_.size());
    for (const auto& r : musicFolders_)    canonMusicRoots.push_back(canonOf(r));

    std::vector<juce::File> canonPodcastRoots;
    canonPodcastRoots.reserve(podcastFolders_.size());
    for (const auto& r : podcastFolders_)  canonPodcastRoots.push_back(canonOf(r));

    int added = 0;
    for (const auto& f : files)
    {
        if (! f.existsAsFile()) continue;
        if (! Constants::supportedExtensions.contains(
                f.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
            continue;

        // Skip if already in the loose list (handles duplicates within the
        // same drop too), or already covered by a music or podcast root —
        // the scanner would otherwise emit them via the folder pass, and
        // letting the file linger in individualTracks_ would persist a
        // ghost entry that never produces a TrackInfo. Canonicalising both
        // sides handles symlinks: a file dropped via its symlinked path is
        // recognised as the same physical track already in the library.
        const juce::File canonF = canonOf(f);

        bool dup = false;
        for (const auto& e : canonExisting)
            if (e == canonF) { dup = true; break; }
        if (dup) continue;
        for (const auto& r : canonMusicRoots)
            if (canonF.isAChildOf(r)) { dup = true; break; }
        if (dup) continue;
        for (const auto& r : canonPodcastRoots)
            if (canonF.isAChildOf(r)) { dup = true; break; }
        if (dup) continue;

        individualTracks_.push_back(f);
        canonExisting.push_back(canonF);     // keep the cache in sync for
                                              // the next iteration's dedup
        ++added;
    }
    if (added > 0)
        saveIndividualTracks();
    return added;
}

int MainComponent::addMusicFolderRoots(const std::vector<juce::File>& roots)
{
    int added = 0;
    for (const auto& r : roots)
    {
        if (! r.isDirectory()) continue;

        bool dup = false;
        for (const auto& existing : musicFolders_)
            if (existing == r) { dup = true; break; }
        if (dup) continue;

        musicFolders_.push_back(r);
        ++added;
    }
    if (added > 0)
    {
        saveMusicFolders();

        // Drop any individual tracks that are now covered by one of the new
        // roots so the scanner doesn't double-emit them.
        const auto before = individualTracks_.size();
        individualTracks_.erase(
            std::remove_if(individualTracks_.begin(), individualTracks_.end(),
                [this](const juce::File& f) {
                    for (const auto& r : musicFolders_)
                        if (f.isAChildOf(r)) return true;
                    return false;
                }),
            individualTracks_.end());
        if (individualTracks_.size() != before)
            saveIndividualTracks();
    }
    return added;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& p : files)
    {
        const juce::File f(p);
        if (f.isDirectory()) return true;
        if (Constants::supportedExtensions.contains(
                f.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
            return true;
    }
    return false;
}

void MainComponent::filesDropped(const juce::StringArray& files, int /*x*/, int /*y*/)
{
    handleExternalPaths(files, /*startPlayback*/ false);
}

void MainComponent::handleExternalPaths(const juce::StringArray& paths, bool startPlayback)
{
    std::vector<juce::File> folders;
    std::vector<juce::File> audioFiles;
    for (const auto& p : paths)
    {
        const juce::File f(p);
        if (f.isDirectory())
            folders.push_back(f);
        else if (f.existsAsFile()
                 && Constants::supportedExtensions.contains(
                        f.getFileExtension().trimCharactersAtStart(".").toLowerCase()))
            audioFiles.push_back(f);
    }

    juce::File playbackTarget;
    if (startPlayback && ! audioFiles.empty())
        playbackTarget = audioFiles.front();

    // Commit loose audio files immediately - they're not gated by the
    // folder prompt. A mixed drop (file + folder) where the user cancels
    // the folder prompt should still keep the file. Playback also fires
    // here so it doesn't wait on the prompt being answered.
    if (! audioFiles.empty() || playbackTarget != juce::File{})
        commitDroppedTracks(audioFiles, /*newRoots*/ {}, playbackTarget);

    // Folders go through the choice dialog independently. No files are
    // passed along (those were already handled above) and the playback
    // target is cleared so the prompt doesn't re-fire playback.
    if (! folders.empty())
        promptForFolderDrop(std::move(folders), /*files*/ {}, juce::File{});
}

void MainComponent::promptForFolderDrop(std::vector<juce::File> folders,
                                         std::vector<juce::File> filesAlreadyQueued,
                                         juce::File              startPlaybackPath)
{
    juce::String message;
    if (folders.size() == 1)
    {
        message = "\"" + folders.front().getFileName() + "\"\n\n"
                  "Add this folder to your library so its contents stay in sync, "
                  "or just import the audio files inside as loose tracks?";
    }
    else
    {
        message = juce::String((int) folders.size())
                  + " folders dropped.\n\n"
                  "Add them to your library so their contents stay in sync, "
                  "or just import the audio files inside as loose tracks?";
    }

    juce::AlertWindow::showAsync(
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle(folders.size() == 1 ? "Add Folder" : "Add Folders")
            .withMessage(message)
            .withButton("Add as Library Folder")
            .withButton("Add Files Only")
            .withButton("Cancel")
            .withAssociatedComponent(this),
        [this, droppedFolders = std::move(folders),
               loose          = std::move(filesAlreadyQueued),
               playbackPath   = startPlaybackPath](int result) mutable {
            if (result == 3 || result == 0) return;  // Cancel / dismissed

            if (result == 1)
            {
                commitDroppedTracks(loose, droppedFolders, playbackPath);
                return;
            }

            // Add Files Only: walk each folder, collect audio files, fold into
            // the loose-files list. Folder roots stay untouched.
            for (const auto& folder : droppedFolders)
            {
                auto contained = collectAudioFilesUnder(folder);
                loose.insert(loose.end(), contained.begin(), contained.end());
            }
            commitDroppedTracks(loose, /*newRoots*/ {}, playbackPath);
        });
}

void MainComponent::commitDroppedTracks(const std::vector<juce::File>& looseFiles,
                                         const std::vector<juce::File>& newRoots,
                                         const juce::File&              startPlaybackPath)
{
    bool changed = false;

    if (! newRoots.empty())
    {
        if (addMusicFolderRoots(newRoots) > 0)
            changed = true;
    }

    if (! looseFiles.empty())
    {
        if (addIndividualTracks(looseFiles) > 0)
            changed = true;
    }

    if (changed)
    {
        // Single rescan covers both the new roots and the new loose files.
        setMusicFolders(musicFolders_, /*keepLibrary*/ true);
    }

    if (startPlaybackPath != juce::File{} && startPlaybackPath.existsAsFile())
    {
        // Build a quick TrackInfo synchronously so playback starts immediately
        // instead of waiting for the background rescan to land. The scanner
        // will produce a proper TrackInfo for the same file shortly; the
        // library view will pick that one up naturally.
        TrackInfo t = LibraryScanner::buildTrackInfoWithTimeout(
            startPlaybackPath, /*isPodcast*/ false);

        PlayQueue::QueueSource source;
        source.sidebarId = 1;
        source.name      = "Dropped Track";

        std::vector<TrackInfo> single { t };
        queue_.setTracks(std::move(single), 0, source);
        playCurrentQueueItem();
    }
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
    if (sidebarId == UIConstants::noArtistId)
        return "(no artist)";
    if (sidebarId >= 2000 && sidebarId < 3000)
    {
        const auto it = artistIdToName_.find(sidebarId);
        return it != artistIdToName_.end() ? it->second : juce::String("Artist");
    }
    if (sidebarId == UIConstants::noAlbumId)
        return "(no album)";
    if (sidebarId >= 3000 && sidebarId < 4000)
    {
        const auto it = albumIdToInfo_.find(sidebarId);
        if (it == albumIdToInfo_.end()) return "Album";
        const auto& [artist, album] = it->second;
        return (artist.isNotEmpty() ? artist : juce::String("Unknown Artist"))
             + " - " + album;
    }
    if (sidebarId == UIConstants::noGenreId)
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
    // Show the lock overlay on the main window FIRST. It dims the content
    // and captures clicks so the user can't interact with anything
    // underneath, but exposes its own "Open Preferences" / "Close
    // Preferences" buttons. Showing it before the dialog (and flushing
    // pending repaints below) means the dim is already on screen when the
    // OS pops up the Preferences window — without that ordering, the
    // bright background flashes briefly.
    prefsLockOverlay_.setVisible(true);
    prefsLockOverlay_.toFront(false);
    resized();
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();

    preferencesWindow_->setVisible(true);
    preferencesWindow_->toFront(true);
    // Preferences floats above the main window so the user can't hide it.
    preferencesWindow_->setAlwaysOnTop(true);

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
       #if ! JUCE_MAC
        // On macOS Preferences lives in the application (Apple) menu via
        // appleMenuExtras in MainWindow. Windows has no equivalent of that
        // hidden menu, so route it through File where users will look for it.
        menu.addSeparator();
        menu.addCommandItem(&commandManager_, cmdPreferences);
       #endif
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

void MainComponent::menuItemSelected(int /*menuItemID*/, int /*topLevelMenuIndex*/)
{
   #if ! JUCE_MAC
    // Tell the menu bar an item was picked so its grace-window for
    // cross-menu hover navigation is cleared (otherwise an idle hover
    // afterwards would re-pop a menu).
    menuBar_.onItemSelected();
   #endif
}

void MainComponent::menuBarActivated(bool isActive)
{
   #if ! JUCE_MAC
    // JUCE fires this when the menu bar's currentPopupIndex transitions
    // between -1 and >=0 (popup just closed / just opened). The bar uses
    // this signal to keep cross-menu hover navigation working past
    // PopupMenu's auto-dismiss-on-mouseUp.
    menuBar_.onModelActivationChanged(isActive);
   #else
    juce::ignoreUnused(isActive);
   #endif
}

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
    commands.add(cmdToggleQueue);
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
        {
            // When the sidebar has keyboard focus on a renamable row, Cmd-R
            // means "rename this row" instead of "edit info on the library
            // selection". Either path keeps the command active so the menu
            // entry stays usable; the perform handler picks the right route.
            const bool sidebarRenameReady =
                sidebar_.hasKeyboardFocus(true)
                && SidebarComponent::isRenamableId(sidebar_.focusedId());

            info.setInfo(sidebarRenameReady ? "Rename" : "Edit Info",
                         sidebarRenameReady
                             ? "Rename the focused sidebar item"
                             : "Edit metadata for the selected track(s)",
                         "File", 0);
            info.setActive(
                sidebarRenameReady
                || (libraryTable_.hasSelection() && !activeEditInfoWindow_.getComponent()));
            info.addDefaultKeypress('r', juce::ModifierKeys::commandModifier);
            break;
        }

        case cmdFocusSearch:
            info.setInfo("Find",
                         "Focus the library search box",
                         "Edit", 0);
            info.addDefaultKeypress('f', juce::ModifierKeys::commandModifier);
            break;

        case cmdToggleQueue:
            info.setInfo("Show Queue",
                         "Show or hide the play queue panel",
                         "Window", 0);
            info.setTicked(queueVisible_);
            // Plain "Q" - no modifier - so it acts as a quick toggle. JUCE
            // suppresses this when a TextEditor (search box, inline rename,
            // etc.) has focus, so it doesn't eat literal q characters.
            // Also stand down while the sidebar is mid-type-ahead so a
            // user typing letters to jump to a Q-starting row gets the
            // letter appended to the search buffer instead of toggling
            // the queue. The command re-activates as soon as the
            // type-ahead window times out (1 s of no input).
            info.addDefaultKeypress('q', juce::ModifierKeys::noModifiers);
            info.setActive(! sidebar_.isTypeAheadActive());
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
            // Sidebar wins when it's focused on a renamable item; otherwise
            // fall through to the library Edit Info path. Matches the
            // dual-purpose label / shortcut declared in getCommandInfo.
            if (sidebar_.hasKeyboardFocus(true) && sidebar_.beginRenameFocused())
                return true;
            libraryTable_.triggerEditInfoForSelection();
            return true;

        case cmdToggleQueue:
            toggleQueue();
            menuItemsChanged();   // refresh the Window menu's tick mark
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
    sidebarWidth_ = props->getIntValue("sessionSidebarWidth", UIConstants::sidebarWidth);
    queueWidth_   = props->getIntValue("sessionQueueWidth",   UIConstants::queuePanelWidth);
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

        // Bottom padding is larger than the symmetric top padding so the
        // buttons don't crowd the OS-managed window border on platforms
        // where the title-bar style steals a few pixels from the bottom of
        // the client area (Windows native title bars in particular).
        enum { pad = 18, padBottom = 28, rowH = 22, btnH = 28 };

        QuitDialog()
        {
            msg.setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(msg);
            addAndMakeVisible(dontShow);
            addAndMakeVisible(cancelBtn);
            addAndMakeVisible(quitBtn);
            setSize(320, pad + rowH + 10 + rowH + 16 + btnH + padBottom);
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

    activeQuitDialog_ = dlg;

    // Overlay first so the dim is in flight before the OS dialog window
    // is created below; flushing pending repaints synchronously (rather
    // than waiting for the next vsync) keeps the dialog from popping up
    // over an undimmed background.
    quitLockOverlay_.setVisible(true);
    quitLockOverlay_.toFront(false);
    resized();
    if (auto* peer = getPeer())
        peer->performAnyPendingRepaintsNow();

    // juce::Component default-constructs with isVisible() == true, so
    // hide before adding to desktop or the HWND is created with WS_VISIBLE
    // and the default light title bar flashes for a frame before our
    // DwmSetWindowAttribute call lands.
    dlg->setVisible(false);
    dlg->addToDesktop(juce::ComponentPeer::windowHasTitleBar
                    | juce::ComponentPeer::windowHasDropShadow
                    | juce::ComponentPeer::windowIsTemporary);
    applyDarkTitleBar(*dlg);
    dlg->setCentrePosition(getScreenBounds().getCentreX(), getScreenBounds().getCentreY());
    dlg->setVisible(true);
}

void MainComponent::NormalizeAnimator::timerCallback()
{
    parent_.updateNormalizeOverlay();
}

void MainComponent::updateNormalizeOverlay()
{
    const bool  spinner = engine_.isLufsAnalysisPending();
    const float alpha   = engine_.normalizationCheckOpacity();

    normalizeButton_.setShowSpinner(spinner);
    normalizeButton_.setCheckAlpha(alpha);

    // Stop the animator once both overlays are at a stable rest. Spinner
    // off means analysis isn't pending (so no rotation to advance), and
    // alpha pinned at 0 or 1 means the check is fully shown / hidden and
    // not mid-fade. Anything else and we keep ticking.
    const bool atRest = ! spinner
                        && (alpha <= 0.001f || alpha >= 0.999f);
    if (atRest && normalizeAnimator_.isTimerRunning())
        normalizeAnimator_.stopTimer();
}

} // namespace Stylus
