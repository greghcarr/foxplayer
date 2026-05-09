# Stylus: Architecture Reference

## Overview
C++17/JUCE 8.0.4 audio player. Primary target macOS; Windows 11 build also supported (see "Cross-platform structure" below). Current state: library browser with multi-folder support, on-disk library cache, playback with media keys + Now Playing + system tray, resizable play queue with right-click remove and shuffle-aware ordering, sidebar with Library / Artists / Albums / Genres / Playlists / Podcasts, drag-to-reorder playlists, album art display, BPM / key / LUFS analysis, Apple Music + album-art lookups (with undo + retries), per-track play count + date-added, mini-player resize mode, always-on-top, per-view selection memory, BS.1770 volume normalisation with lazy LUFS measurement and 2 s smoothstep gain ramps, full keyboard navigation across the three panes with type-ahead search.
Long-term: DJ mode, beatgrid detection, Rekordbox/Serato metadata export.

## Build

### macOS
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
open build/Stylus_artefacts/Debug/Stylus.app
```
Requires: CMake 3.22+, Xcode CLT. JUCE 8.0.4 is fetched automatically via FetchContent.

### Windows 11
```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
& "build\Stylus_artefacts\Debug\Stylus.exe"
```
Requires: CMake 3.22+, Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v14.4x + Windows 10/11 SDK). VS uses a multi-config generator, so pass `--config Debug|Release` at build time, not `-DCMAKE_BUILD_TYPE` at configure time.

## File Layout
```
src/
  Main.cpp                     : JUCEApplication entry point
  MainWindow.h                 : DocumentWindow shell
  MainComponent.h/.cpp         : Top-level layout, wires all subsystems together
  Constants.h                  : All magic numbers, colors, column IDs
  audio/
    TrackInfo.h                : Plain struct: file + metadata + duration + playCount
    PlayQueue.h/.cpp           : Ordered track list + current index; advance/prev/jump
    AudioEngine.h/.cpp         : Full audio pipeline; owns DeviceManager/TransportSource
    StylFile.h/.cpp            : Read/write .styl JSON sidecar files (dot-prefixed, hidden)
    AlbumArtExtractor.h        : JUCE-side: calls platform bridge, falls back to folder art
    AlbumArtExtractor.cpp      : JUCE-side implementation (no platform code here)
    AlbumArtExtractor.mm       : macOS: pure ObjC++ AVFoundation extraction (no JUCE types)
    AlbumArtExtractor_Win.cpp  : Windows: Shell IPropertyStore + PKEY_ThumbnailStream
    MediaFoundationReader.h/.cpp : Windows-only AudioFormatReader subclass wrapping IMFSourceReader
                                    (AAC / Apple Lossless / WMA decode via OS codecs)
  analysis/
    BpmDetector.h/.cpp         : BPM detection
    KeyDetector.h/.cpp         : Musical key detection
    LufsDetector.h/.cpp        : BS.1770 K-weighted integrated LUFS measurement
    AnalysisEngine.h/.cpp      : Queue-based analysis (Full or LufsOnly mode); fires onTrackAnalysed
    AppleMusicLookup.h/.cpp    : Background iTunes Search API queries; retry + suspend logic
  library/
    LibraryScanner.h/.cpp      : Background thread: recursive scan + metadata extraction
    LibraryCache.h/.cpp        : On-disk cache of fullLibrary_ keyed by folder list, so app
                                   starts with the previous library visible while a fresh scan runs
    LibraryTableComponent.h/.cpp: TableListBox showing the library; drag source for DnD
    PlaylistStore.h/.cpp       : Persists playlists to ApplicationProperties as JSON
  ui/
    TransportBar.h/.cpp        : Bottom bar: album art, play/pause, seek, volume, queue toggle
    QueueView.h/.cpp           : Right-side panel showing current play queue, draggable left edge
    SidebarComponent.h/.cpp    : Left sidebar: Music + Playlists; DragAndDropTarget
    SongInfoEditor.h/.cpp      : Modal dialog for editing track metadata
    AnalysisLogWindow.h/.cpp   : Standalone window logging queued/running/finished analyses
    AutoHideViewport.h         : Viewport that fades its vertical scrollbar after idle
    LoadingIndicator.h         : Centred spinner overlay shown during a fresh library scan
    Splash.h                   : Transparent borderless splash, shows the embedded app icon
    PreferencesWindow.h/.cpp   : Tabbed Preferences window (Audio, Library, Display, Misc); Cmd-,
    MacWindowHelper.h/.mm      : macOS: native NSWindow activation; Dock-reopen swizzle + appDidBecomeActive
    MacWindowHelper_Win.cpp    : Windows: no-op stubs (toFront() in MainWindow handles activation)
    NowPlayingBridge.h/.mm     : macOS: MPNowPlayingInfoCenter + MPRemoteCommandCenter (media keys, lock screen)
    NowPlayingBridge_Win.cpp   : Windows: SMTC via C++/WinRT (media flyout, lock screen, media keys)
    StatusBarItem.h/.mm        : macOS: menu-bar status item with quick controls
    StatusBarItem_Win.cpp      : Windows: Shell_NotifyIcon system-tray entry + context menu
    RecordSpinnerLayer.h       : Spinning-disc overlay; base class differs per platform (see header)
    RecordSpinnerLayer.mm      : macOS: NSViewComponent + CALayer GPU rotation
    RecordSpinnerLayer_Win.cpp : Windows: juce::Component + Timer-driven affine-rotation paint

resources/
  icons/                       : Phosphor fill-weight SVGs, curated to ~255 icons
                                   (transport, audio, playback, lists, folders, UI chrome,
                                   carets, arrows, dots). Add to binary-data in CMakeLists.txt
                                   to reference from code as BinaryData::<name>fill_svg.
  app-icon.png                 : Generated by make_app_icon.py; embedded for splash, fed
                                   into juceaide for the macOS .icns
  make_app_icon.py             : Regenerates app-icon.png (play triangle on silver squircle
                                   with the Apple drop-shadow/highlight template)
```

## Key Patterns

### Cross-platform structure
One codebase, two platforms. Don't fork; don't keep a long-lived "windows" branch. Platform divergence is handled in two places only:

1. **CMakeLists.txt** picks platform-specific source files via `if (APPLE) ... else() ... endif()`. The `.mm` ObjC++ files build on macOS; sibling `_Win.cpp` files build on Windows. Both files are always present in the tree; only one is compiled per build. Apple frameworks (`AVFoundation`, `MediaPlayer`, `IOKit`) and the `juceaide macicon` custom command are also under `if (APPLE)`. JUCE auto-runs `juceaide winicon` on Windows from the same `ICON_BIG` PNG, so the icon pipeline is symmetric.

2. **`#if JUCE_MAC` guards in headers** when the *base class* differs per platform (`RecordSpinnerLayer` derives from `juce::NSViewComponent` on Mac, plain `juce::Component + Timer` on Windows) or when a call site is mac-only (`MenuBarModel::setMacMainMenu` in `MainWindow.h`). Prefer the CMake split for whole-file divergence; reserve `#if` for cases where a single header has to shapeshift.

The Mac-helper free functions (`Stylus_activateAndShowWindow`, `Stylus_setDockReopenCallback`, etc.) are declared in `MacWindowHelper.h` with no platform guard, so call sites in `MainWindow.h` stay clean. The Windows `_Win.cpp` provides empty implementations; the actual activation on Windows is `toFront(true)` (guarded `#if ! JUCE_MAC` in `MainWindow::showWindow()`).

**What runs on Windows** (parity with the macOS feature set unless noted):
- `NowPlayingBridge_Win.cpp` — wires SMTC (Windows.Media.SystemMediaTransportControls) via C++/WinRT. Media keys, the small media flyout above the volume slider, and the lock-screen Now Playing card all drive the same play / pause / next / previous callbacks. SMTC is HWND-bound on the desktop, so initialisation is deferred to the first call once MainWindow's peer exists.
- `StatusBarItem_Win.cpp` — `Shell_NotifyIcon` system-tray entry with a "Show Stylus" / "Quit Stylus" context menu and a message-only HWND for callbacks. The icon is pulled from the .exe's first icon resource (juceaide bakes the app icon in). State-aware play / pause / square glyphs like the macOS version are not yet implemented; `setState()` is a no-op.
- `AlbumArtExtractor_Win.cpp` — reads embedded art via the Windows Shell property system (`IPropertyStore` + `PKEY_ThumbnailStream`). Covers MP3 / MP4 / M4A / AAC / FLAC / WMA without any third-party metadata library. Ogg Vorbis isn't reliably handled by the stock shell handler; the cross-platform folder/sidecar fallback in `AlbumArtExtractor.cpp` covers it.
- `MediaFoundationReader.h/.cpp` — Windows-only `juce::AudioFormatReader` subclass that wraps an `IMFSourceReader` configured for 32-bit float PCM with `SetCurrentPosition` for seek. `AudioEngine::loadTrack` falls back to it when the standard `AudioFormatManager` returns nullptr, so AAC / Apple Lossless / WMA play back via the OS-provided codecs (no FFmpeg dependency).
- **Menu bar** — `MenuBarComponent` hosted inside `MainComponent` when `! JUCE_MAC`, bound to MainComponent's `MenuBarModel` impl. Renders the same File / Window menus that `setMacMainMenu` installs into the system menu on Mac. Preferences moves into File on Windows because there's no Apple-style application menu.
- `.styl` sidecars are marked `FILE_ATTRIBUTE_HIDDEN` after each `replaceWithText` so Explorer doesn't show them. macOS hides them implicitly via the dot-prefix.

**What's deliberately not done yet:**
- Tray-icon state glyph (`StatusBarItem::setState` is a no-op on Windows; macOS swaps in different play / pause / square images).
- TagLib-grade Ogg / WAV / AIFF embedded-art reads; the shell handlers don't expose those, so a per-format reader (or TagLib) is the only way.

### Format registration
`registerBasicFormats()` already includes CoreAudioFormat on macOS: do not call `registerFormat(new CoreAudioFormat(), ...)` separately or JUCE will assert on the duplicate.

### Audio pipeline
```
AudioDeviceManager
  -> AudioSourcePlayer (registered as device callback)
     -> AudioTransportSource
        -> AudioFormatReaderSource (swapped per track)
           -> AudioFormatReader (CoreAudioFormat: MP3/AAC/ALAC/AIFF/WAV)
```
All owned by `AudioEngine`. Never call `transportSource_` from an audio thread callback: JUCE handles thread safety internally.

### Track-end detection
`AudioEngine` registers as a `ChangeListener` on `AudioTransportSource`. On change, if `!isPlaying() && !paused_` the track has finished naturally. `onTrackFinished` callback is fired on the message thread.

### Play queue
When a library row is activated, all tracks from that row to the end of the current visible (filtered) order become the new queue. `PlayQueue` stores `std::vector<TrackInfo>` + `int currentIndex`.

### Library data ownership
`fullLibrary_` in `MainComponent` is the single source of truth for all scanned tracks. `LibraryTableComponent` holds a filtered view (pointers into a local copy). When switching sidebar items, `MainComponent` calls `libraryTable_.setTracks(subset)`: it never reads back from the table as the authoritative source, except via `allTracks()` when `onLibraryChanged` fires (hidden state change).

### Sidebar IDs
- `1` = Library (all non-podcast tracks)
- `2` = Podcasts (all podcast tracks)
- `1000 + playlistStore_.id` = a specific playlist (range `1000..1999`)
- `2000..2999` = Artists, rebuilt by `refreshSidebarArtists()` in sorted order
- `3000..3999` = Albums, rebuilt by `refreshSidebarAlbums()` from sorted "[ARTIST] - [ALBUM]"
- `4000..4999` = Podcasts (per-show), rebuilt by `refreshSidebarPodcasts()`
- `5000..5999` = Genres (`Constants::noGenreId = 5999` reserved for "no genre")

### Sidebar is scrollable
`SidebarComponent` is a plain painted component hosted inside an `AutoHideViewport` owned by `MainComponent`. JUCE's Viewport doesn't auto-size its viewed component's width, so `MainComponent::resized()` explicitly syncs the sidebar width to `sidebarViewport_.getMaximumVisibleWidth()`; the sidebar's own `layoutItems()` then grows its height to either its content height or the viewport height (so the background fills when there's leftover room). Leaving either of those steps out makes the sidebar render blank.

### Icons (binary-embedded)
SVG icons are baked into the binary via a `juce_add_binary_data` target called `StylusIcons` (see CMakeLists.txt). JUCE's symbol naming strips hyphens, so `speaker-high-fill.svg` becomes `BinaryData::speakerhighfill_svg` + `BinaryData::speakerhighfill_svgSize`. New icons must be added to both `resources/icons/` AND the `juce_add_binary_data` SOURCES list. The icon set was pruned from 1500+ Phosphor icons down to a curated ~255 covering transport, volume, waveforms, lists/folders, navigation carets/arrows, UI chrome, and DJ-adjacent glyphs (key, metronome, sliders). Add back individual icons as needed - don't re-import the whole set.

### Fonts
All on-screen text uses the macOS system font (SF Pro on 10.11+). No `setDefaultSansSerifTypefaceName` is set, so every `juce::Font` falls through to JUCE's platform default, which is what `[NSFont systemFontOfSize:]` returns. Sites that need explicit sizing/weight construct `juce::Font(juce::FontOptions().withHeight(N).withStyle("Bold"))` without `withName(...)` so they inherit the same default. (The earlier "Helvetica Neue" override was a pre-SF holdover; macOS still ships Helvetica Neue but it stopped being the system font with El Capitan.)

### Playlists
`PlaylistStore` persists playlists as JSON in `ApplicationProperties`. Each playlist stores ordered file paths. `onPlaylistsChanged` callback fires whenever a playlist is created or modified.

### Companion file format (.styl)
Each `track.mp3` gains a hidden sibling `.track.mp3.styl` (JSON). Stores: title, artist, album, genre, year, trackNumber, bpm, key, lufs, hidden, playCount, isPodcast, podcast, dateAdded. `StylFile::load()` is called by `LibraryScanner` after metadata extraction so user edits always win. `StylFile::save()` is called on analysis completion, hide/unhide, edit, and play count increment.

`dateAdded` is set to `juce::Time::currentTimeMillis()` on the first scan that sees a track (i.e. when its styl didn't already have one), and stays stable across rescans. The Library "Date Added" column reads it.

### Analysis safety: don't clobber user edits
`AnalysisEngine` runs on a background thread. It receives a `TrackInfo` snapshot at queue time and emits the same struct back with `bpm` / `musicalKey` / `lufs` filled in. Two protections keep concurrent user edits from being overwritten:
- **Disk side**: before `StylFile::save(track)`, the engine calls `StylFile::load(track)` to refresh title/artist/album/etc. from the latest on-disk state, then re-applies the just-computed bpm/key/lufs. Without this, a styl written by the user mid-analysis would be silently reverted.
- **In-memory side**: `analysisEngine_.onTrackAnalysed` in `MainComponent` finds the track in `fullLibrary_` by file path and merges *only* `bpm`, `musicalKey`, `lufs`. It does **not** call `updateTrackInLibrary(analysed)` (which would copy the full snapshot whole): that's reserved for paths that legitimately want to overwrite metadata fields, like Apple Music lookups.

### Apple Music lookups (single vs batch)
`AppleMusicLookup` runs on its own thread and dispatches results back via `onLookupCompleted(track, status, isBatch)`. The flag flips behaviour in two places:
- `MainComponent::updateTrackInLibrary(t, /*followTrack*/ ! isBatch)`: single lookups follow the track to its new view and select+scroll to it; batch lookups update silently so the user's selection isn't yanked from row to row as each result comes in.
- The "Found nothing" alert is shown only for single (non-batch) lookups; batch failures stay silent.
Network errors are auto-retried after a 30 s delay (collected into `pendingRetryLookups_`). After `maxConsecutiveFailures` consecutive failures, the engine self-suspends and shows a single alert.

### Library cache on disk
`LibraryCache` writes `fullLibrary_` to a JSON file in `ApplicationProperties` under a key derived from the music + podcast folder list. On launch, `setMusicFolders(folders, /*keepLibrary=*/ true)` reads it before the scan, so the user sees their library immediately. The fresh scan still runs: its results accumulate into `scanBuffer_` (because `scanReplacingCachedLibrary_` is true), and at scan complete the buffer atomically replaces `fullLibrary_`. The `LIBRARY` sidebar row shows a small spinner during the background scan; the centred `LoadingIndicator` only appears when there's no cached library yet.

### Per-view selection memory
`MainComponent::savedSelectionByView_` is `std::map<int, std::vector<juce::File>>` keyed by sidebar id. `showSidebarItem(id)` saves the outgoing view's `libraryTable_.selectedFiles()` before changing `activeSidebarId_`, then restores the new view's saved selection (empty vector = nothing selected). `LibraryTableComponent::onSelectionChanged` fires only on user-driven changes; programmatic restoration uses `setSelectedRows(rows, dontSendNotification)` so it doesn't trip the "user touched a row" path. When that callback does fire, `MainComponent` clears every other view's saved selection (so coming back to view A after selecting in view B no longer restores A's stale selection).

### Play queue: shuffle, append, remove
`PlayQueue` keeps both the live `tracks_` and a snapshot `originalTracks_` (set when shuffle is enabled). Three shuffle entry points:
- `shuffleAll()`: move current track to index 0, Fisher-Yates shuffle every other track, save originals. This is what the shuffle button calls (and what session restore re-applies).
- `shuffleRemaining()`: shuffle only what's after the current index. Used internally only.
- `unshuffleRemaining()`: restore `originalTracks_` whole, place the playing track at its original index.
`appendTracks()` appends to both `tracks_` and `originalTracks_` when shuffled, so disabling shuffle later restores the appended tracks too. `removeAt(idx)` strips from both lists by file-path match. `removeAt` no-ops on the currently playing index: the right-click "Remove from Queue" menu disables that row.

### Queue panel resize divider
A second `SidebarDivider` (with `dragSign = -1`) sits at the queue panel's left edge. Min width matches the sidebar's minimum (44 px); max width is 40% of `getWidth()`. Width persists in `sessionQueueWidth`. The divider is `addChildComponent`-ed (initially hidden) and its visibility tracks `queueVisible_`.

### Queue scroll: ghost rows + centred-on-shuffle
`QueueView::getNumRows()` pads with ghost rows so the alternating stripe pattern extends below the last real item, but caps at `listH / rowHeight` (no overshoot) so an empty queue isn't falsely scrollable. After shuffle / unshuffle, `MainComponent` calls `queueView_.centerPlayingRow()`, which uses `viewport->setViewPosition(0, max(0, rowY - (viewportH - rowH)/2))` to centre the playing track. Clamping to `max(0, ...)` means short queues just show from the top.

### Edit Info: Next/Previous uses live order
`showSongInfoEditor` is given a `peerList` snapshot at open time, but `onSaveAndNavigate` ignores it and reads `libraryTable_.visibleTracks()` again at click time. It finds the just-edited track's position in the *current* sort/filter, then steps `±1` from there. So if a lookup re-sorts the row, Next walks to whatever's actually adjacent now. If the edit pushed the track out of the view entirely, Next becomes a no-op rather than walking unrelated peers.

### Editor lookup: re-anchor selection after success
When an Apple Music lookup is triggered from inside the Edit Info dialog, the lookup completion path:
1. Calls `updateTrackInLibrary(t, /*followTrack*/ true)`: re-sorts the table.
2. Fires the editor's pending callback so the dialog's fields refresh.
3. Async-dispatches `libraryTable_.scrollToFile(t.file)` so the underlying view re-selects the looked-up track by file path. Without this, JUCE's TableListBox would keep the old row index and a *different* track (whatever now sits at that index) would appear selected once the sort shifted things.

### Dock icon behaviour (macOS)
`MacWindowHelper.mm` swizzles `applicationShouldHandleReopen:hasVisibleWindows:` on the JUCE app delegate so a Dock click never quits the app. Two activation helpers:
- `Stylus_activateAndShowWindow`: temporarily sets `NSWindowCollectionBehaviorMoveToActiveSpace`, used when re-showing a hidden window so it appears on the user's current Space.
- `Stylus_activateExistingWindow`: just `[NSApp activate] + makeKeyAndOrderFront:`, no Move-to-active-Space. Used when the window is already visible: macOS switches to the window's Space rather than dragging the window to the user.

`MainWindow::showWindow()` branches on `isVisible()` to pick between them; the hidden case also re-centres on whichever display the mouse is on.

### Native menus stay routable
`commandManager_.setFirstCommandTarget(this)` is set once in `MainComponent`'s constructor and never cleared. The "first" command target is JUCE's fallback when nothing in the focused-component chain claims a command: installing it permanently means File / Window menu items stay enabled even when focus is on a child window (Analysis Log, Preferences, Edit Info dialog) whose parent chain doesn't lead back to MainComponent.

### Now Playing + media keys + status bar
`NowPlayingBridge.mm` populates `MPNowPlayingInfoCenter` (track name, artist, album, art, elapsed) and registers `MPRemoteCommandCenter` handlers for play / pause / next / previous / togglePlayPause / changePlaybackPosition. The handlers post back to the `nowPlaying_` object's `std::function` callbacks, which `MainComponent` wires to the same logic the on-screen transport buttons use. Media keys, the macOS lock screen, AirPods controls all flow through this.

`StatusBarItem.mm` creates an `NSStatusItem` with a small icon and a menu providing play/pause + next/prev, hooked through the same callback surface.

### Header sort arrow placement
`LibraryTableComponent::HeaderLnF` overrides `LookAndFeel_V4::drawTableHeaderColumn` so the sort arrow sits immediately after the column title (with a 4 px gap), suppressed if there's not enough room, instead of pinned at the cell's right edge. Background highlights and column text rendering match the V4 default otherwise.

### AlbumArtExtractor: ObjC/JUCE split
AVFoundation and JUCE cannot share a translation unit: CarbonCore's `Point`/`Component` clash with JUCE's. Solution:
- `AlbumArtExtractor.mm`: pure ObjC++, no JUCE headers, exposes `extern "C" unsigned char* Stylus_extractEmbeddedArtwork(const char*, size_t*)`
- `AlbumArtExtractor.cpp`: pure C++/JUCE, calls the C bridge, creates `juce::Image`, falls back to cover.jpg/folder.jpg/artwork.jpg in the same directory

### Drag-and-drop (library → playlist)
`MainComponent` inherits `juce::DragAndDropContainer`. `LibraryTableComponent` adds itself as a mouse listener on its internal `table_` and calls `startDragging()` with `\n`-separated file paths when a drag gesture exceeds the threshold. `SidebarComponent` implements `juce::DragAndDropTarget`; only playlist items (id >= 1000) are valid drop targets. On drop, `onTracksDropped` fires on `MainComponent`, which checks for duplicates and shows an `AlertWindow` for single-track drops.

### Non-ASCII strings in JUCE
JUCE interprets `const char*` as Latin-1. Always wrap UTF-8 emoji/non-ASCII with `juce::String(juce::CharPointer_UTF8("..."))`. Example: `"\xe2\x99\xab"` = ♫, `"\xf0\x9f\x93\x84"` = 📄.

### MIDI suppression
`JUCE_USE_MIDI_INPUTS=0` and `JUCE_USE_MIDI_OUTPUTS=0` are set in CMakeLists.txt to prevent CoreMIDI assertions at startup.

## Conventions
- All tunable values (colors, sizes, column IDs) live in `Constants.h`: never inline.
- `juce::ignoreUnused()` for intentionally unused parameters (no `-Wunused` warnings).
- Callbacks are `std::function<>` members: assign directly, never subclass for simple notifications.
- No `new`/`delete` for owned resources: use `std::unique_ptr` or JUCE's `std::make_unique`.
- Geometry is manual bounds math via `juce::Rectangle`: no physics.
- Hidden files/folders (any path segment starting with `.`) are skipped during library scanning.

### Splash screen
`SplashWindow` is a transparent borderless `juce::DocumentWindow` shown immediately in `JUCEApplication::initialise()`, then dismissed via `juce::Timer::callAfterDelay`. Content is just the embedded `BinaryData::appicon_png` drawn centred via `drawImageWithin(... onlyReduceInSize)`: no background rectangle since the icon already carries its own squircle + shadow. `MainWindow` creation is deferred ~60 ms so the splash gets a paint cycle before `MainComponent`'s heavy constructor blocks the message thread.

### Preferences window
`PreferencesWindow` is owned by `MainComponent`, created hidden, and shown via **Cmd-,** (File menu → "Preferences..."). Sidebar on the left lists categories (Audio, Library, Display, Misc); right-hand panel hosts the current category. `AudioPreferencesPanel` wraps an output-device `ComboBox`, a buffer-size `ComboBox`, and a "Normalize playback volume" toggle:
- The top row of the device combo is "System default (currentDefaultDeviceName)". Picking it sets `usingDefault_ = true` and calls `setAudioDeviceSetup` with the *resolved* default-device name - passing an empty `outputDeviceName` tells JUCE to close the device entirely, so audio goes silent.
- A 2-second `juce::Timer` polls `type->getDefaultDeviceIndex(false)` because JUCE's `ChangeListener` doesn't always broadcast OS-default flips on macOS. When the default changes, we rebuild the combo (so the label refreshes) and, if `usingDefault_`, re-apply the new default to follow the OS.
- The normalize toggle persists at `audio.normalizeVolume` and has a mirror button on the transport bar; both stay in sync via `setNormalizeVolumeChecked` and the `onNormalizeVolumeChanged` callback. See "Volume normalisation" below.

`LibraryPreferencesPanel` shows two stacked sections (Music, Podcasts) each with Add / Remove / Rescan Folders buttons. Rescan triggers `setMusicFolders(musicFolders_, /*keepLibrary*/ true)`, which keeps the cached library visible while a fresh scan runs. Both Rescan buttons trigger the same scan (the scanner processes music + podcast roots in a single pass).

### Mini-player / responsive transport bar
The app shrinks down to a compact "mini player" when the user resizes the window small. All thresholds live in `Constants.h`:
- `minWindowWidth` / `minWindowHeight`: hard floors (`MainWindow::resizeLimits`).
- `miniModeWidth`: threshold below which the transport bar drops its info text area and uses a compact centred "Artist - Title" (or filename) line above the buttons.
- `compactHeight`: threshold below which the library/sidebar area is effectively invisible.

When `width < miniModeWidth` **or** `height < compactHeight`, `MainWindow::resized()` sets the window title to "Stylus" (blank otherwise) so the compact window stays identifiable without the library header.

Inside `TransportBar::resized()`:
- All five buttons (shuffle / prev / play / next / repeat) and the seek bar are **always** visible, at every width.
- Seek bar width scales smoothly via `juce::jlimit(80, 400, getWidth() / 3)` - no discontinuity.
- Album art size and position both use a shared `artT` interp factor (`0.0` at `miniModeWidth` → `1.0` at 540px). Size goes `44 → 80`, and position goes `(x0 - artDim) / 2` (centred between the window's left edge and the shuffle button) → `pad` (flush-left). One smooth continuum.
- Info text (three-line left-side) fades out by multiplying `Color::xxx.withMultipliedAlpha(infoAlpha)` over the same 540→miniModeWidth range. At mini widths the compact "Artist - Title" line takes over, *centered on the play button* via symmetric bounds `[centerX - halfW, centerX + halfW]` (where `halfW = min(dist-to-art, dist-to-speaker)`).

### Title-fade gradient
`TransportBar::paint()` draws a horizontal `juce::ColourGradient` centred on the play button after the info text: transparent at both edges, solid `Color::transportBg` at the midpoint. Title text that crosses into the gradient dissolves into the transport-bar background. The radius is capped so the gradient's transparent edge lands just past the album art's right edge (`cx - (albumArt.right + 6)`) - keeps the art from ever being touched by the fade. Clipped vertically to stay above the seek row.

### App icon pipeline + CMake workaround
`resources/make_app_icon.py` (Python + Pillow) renders `resources/app-icon.png`. JUCE's `juce_add_gui_app ICON_BIG` points at that PNG and runs `juceaide macicon` at **CMake configure time** (not build time) to produce `Stylus_artefacts/JuceLibraryCode/Icon.icns`. Without help, a PNG-only change wouldn't re-trigger that step, so the bundle kept copying the stale icns. The `StylusIconRefresh` custom target in `CMakeLists.txt` lists the PNG as a `DEPENDS`, and when the PNG is newer than the stamp it calls `$<TARGET_FILE:juce::juceaide>` directly with `macicon` to regenerate both the build-side and bundle-side icns, then stamps. After running the Python script, a plain `cmake --build build --parallel` is enough - no reconfigure, no bundle deletion needed. macOS still caches icons in the Dock, so if the UI doesn't update, `lsregister -f <app>` + `killall Dock Finder` is the cheap fix (or `find /private/var/folders -name 'com.apple.dock.iconcache' -delete` for a harder reset).

### Analysis log window
`AnalysisLogWindow` is owned by `MainComponent` (created hidden) and toggled via the Window menu (Shift-Cmd-L). `AnalysisEngine` fires `onTrackQueued` / `onTrackStarted` / `onTrackAnalysed` callbacks; `MainComponent` forwards each to the log component, which keeps a per-file row state machine (Queued → Analyzing → Done).

### Queue button visibility
The queue toggle button is `addChildComponent`-ed (hidden) at startup. `queue_.onQueueChanged` shows/hides it based on `queue_.size() > 0` and force-collapses the queue panel (and its divider) when the queue empties out.

### Right-edge mod buttons (pin + normalize)
Two `TransportButton` instances sit on the right edge of the transport bar, mirroring the album-art / play-cluster on the left. Both are `toggleStyle` mods (no surrounding circle), and both share the speaker icon's `Color::textDim` resting tone so the three rightmost controls read as one family at rest:
- **Pin (`Icon::Pin`)**: top third. Off = textDim grey, On = red. Toggle persists in `sessionAlwaysOnTop` and applies via `juce::DocumentWindow::setAlwaysOnTop`. Shift-Cmd-P toggles it from anywhere.
- **Normalize (`Icon::Normalize`)**: bottom third, slightly larger (21 px vs the pin's 17 px) so the slider+check composition reads. Base is the `sliders-fill` icon, dim (`#404040`) when off, brightening to textDim when on; a `check-fat-fill` overlay drawn at 0.78 opacity in green (`#3ec25e`) marks the active state, leaving the underlying sliders visible behind/around it. Toggle persists at `audio.normalizeVolume`. Tooltip says "Normalize playback volume". See "Volume normalisation" for the engine side.

Speaker icon (paint-drawn by `TransportBar`, between the two mod buttons) carries a position-aware tooltip ("Mute" / "Unmute") via `TransportBar`'s `TooltipClient` override; the pin and normalize buttons get their tooltips through `SettableTooltipClient` on `TransportButton`.

### Volume normalisation (BS.1770 LUFS)
When enabled in Audio preferences (or via the transport-bar mirror button), `AudioEngine` applies a per-track gain offset on top of the user volume so all tracks play at a similar level. Target is **-14 LUFS** (the Apple Music / Spotify / YouTube Music streaming standard); offset is `pow(10, (target − track.lufs) / 20)`, clamped to ±6 dB so a wildly miscalibrated reading can't push toward clipping or near-silence. `LufsDetector` implements the BS.1770 K-weighting (high-shelf pre-filter + RLB high-pass) using hardcoded 48 kHz coefficients applied at all sample rates - within ~1 LU of strict BS.1770 for typical music, fine for matching levels. Full-file integrated mean square; per-channel sums combined per spec (`sumSquares / totalSamples`, NOT `/ (totalSamples * numChannels)`).

Key state machine in `AudioEngine` for the loaded track:
- `currentTrack_.lufs != 0`: a measured value is available, gain uses it.
- `currentTrack_.lufs == 0 && ! lufsKnown_`: lazy analysis is genuinely pending; gain pre-rolls at -3 dB so an unanalysed track right after a normalised one doesn't burst in at full level then drop mid-track.
- `currentTrack_.lufs == 0 && lufsKnown_`: analysis ran but produced no value (file unreadable / decode failure); gain falls back to unity instead of holding the cut.

`AnalysisEngine::enqueueLufsOnly` runs the loudness pass without BPM / key (no log entries either, so casual playback doesn't spam the developer-facing log). Triggered from `engine_.onTrackStarted` whenever normalisation is on, the track's `lufs == 0`, and the file exists. Result writes through the existing `analyseOne` save path (which re-loads the .styl first to avoid clobbering user edits). `MainComponent::analysisEngine_.onTrackAnalysed` routes a non-zero result to `engine_.updateCurrentTrackLufs`, a zero result to `engine_.markLufsAnalysisFailed`.

Gain transitions for normalisation (toggle on/off, LUFS landing, analysis-failed) ramp over **2 s** in dB-space with a smoothstep ease, driven by `AudioEngine::RampTimer` (60 Hz). Volume slider, mute toggle, and `loadTrack` use the immediate path (`applyCombinedGain(false)`) so they stay responsive and so cross-track changes don't fade in awkwardly. Direct-form-II transposed biquads in `LufsDetector`; coefficients constexpr.

### Keyboard navigation
Three focusable panes with consistent navigation. The "active" sidebar row dictates the library view (indicator bar + white text); the "focused" row additionally draws the gray overlay. Visual mutex: only one of {sidebar focused, library has selection, queue has selection} renders a highlight at any time. Each pane saves its cursor position when focus leaves and restores it on return.

Within-pane keys (after the pane has focus):
- **Sidebar**: Up/Down/PgUp/PgDn/Home/End (skipping headers / collapsed-section items / pseudo-rows like "+ New Playlist"); type-ahead (printable letter -> jump to first label-prefix match in the *current section*, multi-letter refinement within `kTypeAheadTimeoutMs = 1000`); Alt+Up/Alt+Down section nav (collapses current, expands target, lands on first row, no wrap-around); Cmd+Up/Cmd+Down to first/last row of the current section.
- **Library** (native TableListBox + KeyListener overrides): arrows / PgUp / Home / End for selection; Cmd+Up/Cmd+Down to first/last row; Up from row 0 pops focus into the search box (and `deselectAll`s for visual mutex); search-box Down pulls focus to the table on the first matching row.
- **Queue** (native ListBox + KeyListener overrides): same arrow / Cmd+Up/Down model.

Cross-pane keys:
- **Right** or **Tab** or **Enter** from sidebar → library.
- **Left** or **Shift-Tab** from library → sidebar; **Right** or **Tab** from library → queue (silent no-op if queue is empty; opens the queue panel automatically if hidden).
- **Left** or **Shift-Tab** from queue → library (queue stays open); **Tab** continues forward to sidebar; **Shift-Tab** from sidebar reverse-cycles into queue.

`MainComponent`'s `focusSidebar` / `focusLibrary` / `focusQueueIfPossible` lambdas centralise the focus transitions and explicitly call `deselectAll` / `clearFocus` on the panes being left, because cross-pane focus arriving via `setSelectedFiles` / `selectRow(samerow)` doesn't always trip the `onSelectionChanged`-based mutex.

Each pane keeps a "saved selection on leave" snapshot independent of its visible selection: `savedSelectionForRefocus_` (file paths) on `LibraryTableComponent`, `savedSelectedRowForRefocus_` (row index) on `QueueView`. Sidebar's `focusedId_ = -1` on focusLost + `focusedId_ = selectedId_` on focusGained achieves the same thing without a separate slot.

`KeyPress::operator==(int)` is **modifier-sensitive** in JUCE: it returns false if any modifier is held. So bare-arrow handlers use `key == juce::KeyPress::upKey`, but Alt-, Cmd-, or Shift-modified arrows compare via `key.getKeyCode() == juce::KeyPress::upKey` instead. Mixing the two forms is a frequent foot-gun.

### Q-key gate during sidebar type-ahead
The Q hotkey toggles the queue panel via `cmdToggleQueue` (no modifier). `ApplicationCommandManager` runs commands before `Component::keyPressed`, so a bare Q would normally never reach the sidebar's type-ahead - meaning a user trying to type-ahead to a Q-prefixed row would just toggle the queue. `cmdToggleQueue::getCommandInfo` calls `info.setActive(! sidebar_.isTypeAheadActive())`; `setActive` is queried fresh on each keypress, so the gate flips back to active as soon as the type-ahead's 1 s timeout passes (or focus moves elsewhere).

### Tooltip routing
A plain `juce::TooltipWindow` is attached to `MainComponent`. Earlier the window subclass restricted lookups to `SidebarComponent`, which silently swallowed tooltips on the transport bar mods - if you're adding a tooltip somewhere new, no scoping work is needed. `TransportButton` inherits `juce::SettableTooltipClient` so `setTooltip("…")` works on the right-edge mods directly. `TransportBar` itself implements `juce::TooltipClient` for the speaker icon, since it's paint-drawn rather than a child component; `getTooltip()` reads the live mouse position and returns "Mute" / "Unmute" based on `muted_` and the slider value.

### Hover-forwarder pattern (avoid double-fire)
`TransportBar` forwards `mouseMove` / `mouseExit` from its children up to `refreshHoverState` so hover indicators don't go stale when the cursor is over a child. The wiring lives on a tiny `HoverForwarder : juce::MouseListener` class, NOT on `TransportBar` itself: registering `this` as a listener with `addMouseListener(this, true)` causes JUCE to call your `mouseDown` / `mouseUp` twice for direct events (once via virtual dispatch, once via the listener mechanism), which silently breaks any toggle-style click handler. The mute button regression that resulted from this was the giveaway. Reach for the same pattern (separate listener forwarder) any time you need child-event forwarding without polluting the receiving component's own click semantics.

## Adding a New Feature Checklist
1. If it needs a tunable value, add it to `Constants.h` first.
2. If it needs a new audio format, register it in `AudioEngine` constructor.
3. If it stores per-track data, add a field to `TrackInfo` and update `StylFile::load()`/`save()`.
4. If it needs periodic UI updates, use `juce::Timer` (not a background thread for UI).
5. If it modifies playlist contents, go through `PlaylistStore`: never mutate `Playlist::trackPaths` directly.
6. If it needs an icon, check `resources/icons/` first; if missing, drop the SVG in there AND add it to the `juce_add_binary_data` block in `CMakeLists.txt`, then reconfigure CMake (`cmake -B build`) so the BinaryData header regenerates.
