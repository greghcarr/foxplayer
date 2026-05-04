# Stylus iOS Port: Planning Document

A planning doc for porting Stylus to iPhone / iPad as a hybrid SwiftUI + C++ app. The desktop project is the single source of truth for analysis, library, and `.styl` formats; the iOS project vendors that core verbatim and wraps it in a native iOS shell.

> **Distribution / license note.** Phase 9 (Mac-side cable sync) depends on **libimobiledevice**, which is GPL-2.0+. As long as Stylus stays a personal / non-distributed project, the GPL has no practical effect. If Stylus is ever distributed (App Store, public binary, paid product), the sync engine must ship as a **separate GPL helper binary** that the main app launches as a child process and talks to via stdin/stdout or an XPC service. This keeps the GPL boundary at the helper, not the main app. None of this affects iOS at all (only the Mac side links libimobiledevice). See section 11 for the full architecture.

## 1. Architecture at a glance

```
+----------------------------------------------------+
|  StylusApp (Swift / SwiftUI)                       |  <-- views, navigation, gestures
|  +----------------------------------------------+  |
|  |  AVAudioEngine wrapper (Swift)               |  |  <-- playback, seek, level meter,
|  |  MPNowPlayingInfoCenter / MPRemoteCommand    |  |      AVAudioSession, route changes,
|  |  Files-app integration, document picker      |  |      lock-screen art, background audio
|  +----------------------------------------------+  |
|                       |                            |
|                       v                            |
|  +----------------------------------------------+  |
|  |  StylusBridge.mm (Objective-C++ shim)        |  |  <-- thin C-callable facade:
|  |  extern "C" Stylus_* free functions          |  |      converts std::string,
|  |                                              |  |      std::vector<TrackC>, etc.
|  +----------------------------------------------+  |
|                       |                            |
|                       v                            |
|  +----------------------------------------------+  |
|  |  StylusCore (C++ static lib)                 |  |  <-- vendored / submoduled from
|  |    - audio/{TrackInfo,PlayQueue,StylFile}    |  |      the desktop repo
|  |    - audio/AlbumArtExtractor.{mm,h}          |  |
|  |    - analysis/{BpmDetector,KeyDetector,      |  |
|  |                AnalysisEngine,               |  |
|  |                AppleMusicLookup}             |  |
|  |    - library/{LibraryScanner,LibraryCache,   |  |
|  |               PlaylistStore}                 |  |
|  |    + JUCE modules (no GUI):                  |  |
|  |        juce_core                             |  |
|  |        juce_data_structures                  |  |
|  |        juce_audio_basics                     |  |
|  |        juce_audio_formats                    |  |
|  +----------------------------------------------+  |
+----------------------------------------------------+
```

Rule of thumb: nothing above the bridge knows about JUCE, nothing below it knows about Swift / UIKit / SwiftUI.

## 2. New project layout

```
stylus-ios/
  README.md
  IOS_PORT_PLAN.md                      (copy of this file)
  Package.swift                         (or .xcodeproj — see section 9)
  StylusApp/                            (Swift target)
    StylusApp.swift                     (App entry)
    Audio/
      AudioPlaybackEngine.swift         (AVAudioEngine + AVAudioPlayerNode)
      AudioSessionController.swift      (AVAudioSession, route changes)
      NowPlayingController.swift        (MPNowPlayingInfoCenter + remote commands)
    Library/
      LibraryStore.swift                (ObservableObject, calls bridge)
      PlaylistStore.swift
      ImportController.swift            (Files app + document picker)
    UI/
      RootTabView.swift                 (Library / Artists / Albums / Playlists / Search)
      LibraryListView.swift
      NowPlayingSheet.swift             (mini-player + full-screen player)
      QueueView.swift
      TrackContextMenu.swift            (long-press menu)
    Resources/
      Assets.xcassets/                  (app icon, accent color)
  StylusBridge/                         (Objective-C++ target)
    StylusBridge.h                      (Swift-importable C header)
    StylusBridge.mm                     (calls into StylusCore)
    StylusBridge.modulemap
  StylusCore/                           (C++ target, vendored)
    CMakeLists.txt                      (or sources listed in Package.swift)
    src/                                (mirror of desktop project's src/)
      audio/{TrackInfo.h, PlayQueue.{h,cpp}, StylFile.{h,cpp},
              AlbumArtExtractor.{h,cpp,mm}}
      analysis/{BpmDetector,KeyDetector,AnalysisEngine,AppleMusicLookup}.{h,cpp}
      library/{LibraryScanner,LibraryCache,PlaylistStore}.{h,cpp}
      Constants.h                       (audio/analysis tunables only,
                                         macOS UI constants stripped)
    JUCE/                               (juce_core + juce_data_structures +
                                         juce_audio_basics + juce_audio_formats,
                                         pinned to 8.0.4)
```

Recommendation: keep the desktop and iOS projects as **separate repos**, with `StylusCore/src/` either git-submoduled or vendored from the desktop repo. Either way, the rule is: changes to core files happen in the desktop repo first, then propagate.

## 3. Core extraction (desktop side)

Before starting on iOS, do this **in the desktop repo** as a preliminary commit so the iOS project has clean ground to vendor from:

1. Create a new CMake target `StylusCore` with no GUI dependencies. It links only `juce_core`, `juce_data_structures`, `juce_audio_basics`, `juce_audio_formats`. Sources:
   - `src/audio/PlayQueue.cpp`
   - `src/audio/StylFile.cpp`
   - `src/audio/AlbumArtExtractor.mm` (no JUCE deps, AVFoundation works on iOS)
   - `src/analysis/BpmDetector.cpp`
   - `src/analysis/KeyDetector.cpp`
   - `src/analysis/AnalysisEngine.cpp`
   - `src/analysis/AppleMusicLookup.cpp`
   - `src/library/LibraryScanner.cpp`
   - `src/library/LibraryCache.cpp`
   - `src/library/PlaylistStore.cpp`

2. Make `Stylus` (the GUI app) link `StylusCore` instead of compiling those sources directly. This proves the split is real.

3. **Drop** `src/audio/AlbumArtExtractor.cpp` from `StylusCore`. The `.mm` file is enough; the `.cpp` wrapper is a JUCE-flavoured convenience that returns `juce::Image`. iOS can load the bytes into a `UIImage` directly, and macOS can keep its `.cpp` wrapper out-of-core.

4. Audit `src/Constants.h`. Split it: `Constants.h` keeps audio/analysis constants (sample rates, buffer sizes, BPM ranges); `UIConstants.h` (new, app-only) holds colors, sizes, sidebar IDs, mini-mode thresholds. iOS won't need `UIConstants.h`.

This single refactor is what unlocks the port. Until it lands, the iOS project has no clean dependency to vendor.

## 4. Bridge design

Swift ↔ C++ interop has matured (Xcode 15+), but JUCE's templates and value-semantics types don't bridge cleanly. So: write a thin `extern "C"` facade.

`StylusBridge.h` (Swift-importable, C only):

```c
typedef struct {
    const char* filePath;
    const char* title;
    const char* artist;
    const char* album;
    const char* genre;
    int    year;
    int    trackNumber;
    double durationSeconds;
    double bpm;
    int    musicalKey;
    double lufs;
    int    isPodcast;
    const char* podcast;
    int64_t dateAddedMillis;
    int    playCount;
} StylusTrackC;

// Library
typedef void* StylusLibraryHandle;
StylusLibraryHandle Stylus_LibraryCreate(const char* const* folders, int count);
void Stylus_LibraryDestroy(StylusLibraryHandle);
void Stylus_LibraryStartScan(StylusLibraryHandle,
                             void (*onTrack)(const StylusTrackC*, void*),
                             void (*onDone)(void*),
                             void* userData);

// .styl I/O
int  Stylus_StylLoad(const char* trackPath, StylusTrackC* outTrack);
void Stylus_StylSave(const char* trackPath, const StylusTrackC* track);

// Analysis
typedef void* StylusAnalysisHandle;
StylusAnalysisHandle Stylus_AnalysisCreate(void (*onAnalysed)(const StylusTrackC*, void*),
                                           void* userData);
void Stylus_AnalysisDestroy(StylusAnalysisHandle);
void Stylus_AnalysisQueue(StylusAnalysisHandle, const StylusTrackC* track);

// Apple Music lookup
typedef void* StylusLookupHandle;
StylusLookupHandle Stylus_LookupCreate(void (*onResult)(const StylusTrackC*, int status, void*),
                                       void* userData);
void Stylus_LookupQuery(StylusLookupHandle, const char* artistGuess, const char* titleGuess);

// Album art
unsigned char* Stylus_ExtractArtwork(const char* trackPath, size_t* outSize);
void           Stylus_FreeArtworkBytes(unsigned char*);
```

Swift then wraps each of these in a class that:
- holds the opaque handle in a property,
- adapts C callbacks via `Unmanaged.passUnretained(self).toOpaque()` for `userData`,
- converts `StylusTrackC` <-> a Swift `Track` value type at the boundary.

Memory ownership rule: the bridge always returns C strings/buffers that the caller must release via a paired free function. Don't try to do RAII across the boundary.

## 5. Audio playback (Swift side)

**Drop** `src/audio/AudioEngine.cpp` for iOS. JUCE's `AudioDeviceManager` on iOS is workable but doesn't integrate as cleanly with `AVAudioSession`, route changes, AirPods spatial audio, or background mode as a native `AVAudioEngine` setup does.

`AudioPlaybackEngine.swift` responsibilities:
- `AVAudioEngine` + `AVAudioPlayerNode` graph
- Decode files via `AVAudioFile` (or use the bridge to decode via JUCE's `AudioFormatReader` if you want bit-identical decode behaviour with the desktop)
- Seek, play, pause, stop, level meter
- Track-end detection via `scheduleFile(...) completionCallback:`
- Volume, mute
- Publish state via `@Published` for SwiftUI

`AudioSessionController.swift` responsibilities:
- Set category to `.playback` with `.allowAirPlay`, `.allowBluetoothA2DP`, `.mixWithOthers = false`
- Activate / deactivate
- Listen for `AVAudioSession.routeChangeNotification` and `interruptionNotification`
- Pause on headphone unplug (route change reason `.oldDeviceUnavailable`)

Background mode: add the `audio` value to `UIBackgroundModes` in Info.plist. Without this, audio stops when the app backgrounds.

## 6. Now Playing + remote commands

`src/ui/NowPlayingBridge.h/.mm` is **mostly portable as-is**. `MPNowPlayingInfoCenter` and `MPRemoteCommandCenter` are identical APIs on iOS. Differences:
- The `StatusBarItem` (menu bar) is gone on iOS. Drop it entirely.
- The `MacWindowHelper` Dock-reopen swizzle is gone. Drop it.
- `MPRemoteCommandCenter` wires the same commands (play, pause, next, prev, togglePlayPause, changePlaybackPosition). On iOS these surface in Control Center, on the lock screen, and on AirPods.

Recommendation: rewrite `NowPlayingController.swift` natively in Swift rather than using the macOS `.mm` file. It's <100 lines and the Swift version reads better. The desktop's `.mm` stays in the desktop repo as the macOS implementation.

## 7. iOS-specific concerns

### File system / sandbox
- Set `UIFileSharingEnabled = YES` and `LSSupportsOpeningDocumentsInPlace = YES` in Info.plist. The app's Documents folder then appears in the Files app under "On My iPhone > Stylus".
- User flow to populate the library: open Files on the iPhone, drag artist folders from iCloud / a USB-C drive / AirDrop into `Stylus/Music/`. Or use AirDrop directly to send a folder to Stylus.
- The library scanner reads `Documents/Music/` recursively, same as the desktop scanner reads the user's chosen folders.
- For external storage (USB-C drives), use `UIDocumentPickerViewController` and `startAccessingSecurityScopedResource()`.
- `.styl` sidecar files travel with the audio files. Metadata, play counts, analysis, date-added all carry over from the desktop.

### Library cache + prefs location
- Cache JSON: `Application Support/StylusLibraryCache.json` (not user-visible in Files app)
- Playlists: `juce::ApplicationProperties` already resolves to a plist in Application Support on iOS — no change needed
- Cache key (folder list hash) is identical to desktop, so a desktop-built cache could in theory be copied over to skip the first scan. Optional polish.

### Analysis on a phone
- BPM + key analysis is CPU-bound and battery-hungry. Default behaviour: only run when charging, or only when the user explicitly taps "Analyse".
- Throttle worker count to 1 on iPhone, 2 on iPad. (Desktop currently uses logical cores.)

### What to drop (macOS-only)
- Mini-player resize mode (no resize on iOS)
- Always-on-top pin button
- Status bar item
- Cmd-key shortcuts and the menu bar
- `MacWindowHelper` Dock swizzle
- The Preferences window's audio-device picker (iOS handles output routing via AVAudioSession + Control Center)
- "System default" device follow logic

### What to add (iOS-only)
- Tab bar navigation (iPhone) / `NavigationSplitView` (iPad)
- Now Playing sheet that swipes between mini-player and full-screen
- Long-press context menus on rows
- Swipe actions: "Add to Queue", "Remove from Queue", "Play Next"
- AirPlay route picker (`AVRoutePickerView`)
- Share sheet for tracks (export `.styl` + audio together)
- Pull-to-refresh on library views to trigger a rescan

## 8. UI structure

### iPhone
- Bottom tab bar: Library, Artists, Albums, Playlists, Search
- Each tab is a `NavigationStack` with drill-down (Artist -> Albums by Artist -> Tracks)
- Mini-player anchored above the tab bar, full track title + play/pause + art
- Tap mini-player -> `NowPlayingSheet` slides up: art, scrubber, transport, lyrics if present, queue
- Long-press a track row: context menu (Play Now, Play Next, Add to Queue, Add to Playlist, Edit Info, Show in Files, Delete)
- Pull down on a sidebar tab to rescan its source

### iPad
- `NavigationSplitView`: sidebar (Library / Artists / Albums / Playlists), middle column (track list), detail column (Now Playing)
- Closer to the macOS layout
- Drag tracks between playlists like desktop

### Shared
- SwiftUI `List` with `LazyVStack` for >5k tracks
- Custom row view with album art thumbnail (loaded async from bridge), title, artist, BPM, key, duration

## 9. Project format: SPM vs. Xcode project

Two options for the new project:

**(a) Xcode project (.xcodeproj)** — standard, full GUI tooling, easy code signing setup, easy embedded frameworks. Editing in VS Code works for Swift but you lose previews and storyboard editing. Builds via `xcodebuild`.

**(b) Swift Package Manager (Package.swift)** — VS Code-friendly, easier to read, but mixed-language packages (C++ + ObjC++ + Swift in one package) require Swift 5.9+ and have caveats. Build still requires Xcode under the hood for signing and simulator. Previews work in Xcode but not VS Code.

Recommendation: **start with (b)** for editability in VS Code, then accept that iteration on UI happens in Xcode (for previews and the simulator). The C++ static lib + ObjC++ bridge + Swift app split maps cleanly onto SPM targets.

Either way: code signing, provisioning profiles, App Store submission all require Xcode. VS Code is for daily editing, not for shipping.

## 10. Phased delivery

Each phase ends with a runnable build on a real device or the simulator. Don't move to the next phase until the current one runs.

**Phase 0: Desktop core extraction (this repo)**
- Add `StylusCore` CMake target as described in section 3
- Refactor `Constants.h` split
- Drop `AlbumArtExtractor.cpp` from core (keep `.mm`)
- Verify desktop still builds and runs identically

**Phase 1: iOS skeleton**
- New repo, Package.swift with three targets
- Vendor or submodule `StylusCore/`
- Bridge implements `Stylus_LibraryCreate` + `Stylus_LibraryStartScan` only
- SwiftUI: one `List` view of tracks from `Documents/Music/`
- Tap a row: `AVAudioFile` + `AVAudioPlayerNode` plays it (no queue, no transport bar, no metadata)
- Run on the simulator with a folder of test files dropped via the Files app

**Phase 2: `.styl` and full metadata**
- Bridge: `Stylus_StylLoad` / `Stylus_StylSave`
- LibraryStore reads sidecar metadata, falls back to AVFoundation tags when missing
- Album art: bridge `Stylus_ExtractArtwork`, render in row + Now Playing sheet
- Track row shows title / artist / album / duration / bpm / key

**Phase 3: Transport + queue**
- `PlayQueue` via bridge
- AVAudioEngine wrapper handles next/prev/seek/track-end
- `NowPlayingSheet` mini-player + full-screen
- Now Playing center + remote commands (play/pause/next/prev/seek)
- AVAudioSession config + background mode
- Lock screen artwork

**Phase 4: Sidebar views**
- Tab bar: Library / Artists / Albums / Playlists / Search
- Drill-down navigation
- Filter / sort by metadata fields

**Phase 5: Analysis**
- Bridge AnalysisEngine
- Run on `.userInitiated` queue, capped concurrency
- "Analyse Library" button in Settings (no auto-analyse on iOS by default)
- Battery + thermal monitoring: pause analysis when device hot or unplugged

**Phase 6: Apple Music lookup**
- Bridge AppleMusicLookup
- Single + batch lookup flows
- Edit Info sheet (port `SongInfoEditor` to SwiftUI)

**Phase 7: Playlists + drag-and-drop**
- PlaylistStore via bridge
- Long-press / context-menu add to playlist
- iPad: drag tracks from list into sidebar playlist

**Phase 8: Polish**
- iPad NavigationSplitView layout
- AirPlay picker
- Share sheet export
- Settings: import folder picker, analyse toggle, cache rebuild
- iCloud Drive support (optional)

**Phase 9 (desktop side): cable + Wi-Fi sync engine**
- Add `src/sync/` module to the desktop repo: `DeviceMonitor`, `AfcClient`, `SyncEngine`
- Vendor or out-of-process libimobiledevice (see section 11)
- New "Devices" sidebar entry on Mac Stylus showing connected iPhone, free space, last synced
- "Sync Now" button: diff library, push new/changed audio files + `.styl` sidecars to the iOS app's `Documents/Music/`, delete removed files, push playlists JSON
- Two-way merge of `.styl` metadata (phone-side play counts and last-played timestamps come back to the Mac)
- iOS-side: rescan on app foreground to pick up files dropped during a sync session
- Optional: auto-sync on device connect (Preferences toggle)

**Phase X: CarPlay (aspirational, conditional)**

The "X" rather than a sequential number signals this is conditional on Apple's gatekeeping. Even with everything else shipped, Phase X may never happen.

- Gated on Apple's **CarPlay Audio Apps entitlement**. Requires the paid Apple Developer Program plus an explicit application and approval from Apple, which is not guaranteed. Until the entitlement is granted, no CarPlay code can run in an actual vehicle (the simulator can preview the UI without it, but installs to real cars will fail to surface the app).
- Add a `CarPlay` scene to `Info.plist` via `UIApplicationSceneManifest`.
- Implement a `CPTemplateApplicationSceneDelegate` exposing:
  - `CPNowPlayingTemplate` for the playing view (transport, art, queue access).
  - `CPListTemplate` hierarchies for browse (Library / Artists / Albums / Playlists), drilled via `pushTemplate(_:animated:)`.
  - `CPSearchTemplate` for voice / text search (driver-friendly typing is intentionally limited; voice covers most of it).
- Reuse `LibraryStore`, `ArtworkCache`, `AudioPlayer`, and the Phase 3 Now Playing wiring as-is. `MPNowPlayingInfoCenter` already drives CarPlay once the entitlement is active, so phone-screen playback / lock-screen / CarPlay all share one truth.
- iOS app UI is unchanged; CarPlay UI is parallel.
- Distance constraint per Apple's CarPlay HIG: list rows must be reachable while driving (large hit targets, no fine-grained metadata in cells). Treat the iOS row's BPM / key surfacing as iOS-only; on CarPlay the row is just title + artist + duration.

## 11. Mac-side cable sync engine (Phase 9 architecture)

iOS cannot expose app sandbox containers to a Mac directly. The sanctioned path is the iOS app's Documents folder, exposed via the `UIFileSharingEnabled` + `LSSupportsOpeningDocumentsInPlace` flags (already part of the iOS plan, section 7). With those flags on, Apple's **AFC** (Apple File Conduit) protocol — running over `usbmuxd`, the same daemon Finder and iTunes use — gives the Mac read/write access to that folder. This is what makes "cable sync" possible.

### Implementation: libimobiledevice (out-of-process)

[libimobiledevice](https://libimobiledevice.org/) is the standard open-source AFC + usbmuxd wrapper. It also handles device discovery, pairing, and falls back to Wi-Fi sync once the device has been USB-paired once. It's stable, used by countless cross-platform iOS tools, and tracks Apple's protocol changes promptly.

It's **GPL-2.0+**. The cleanest license boundary for a non-GPL main app is to compile a small **`stylus-sync-helper`** binary that statically links libimobiledevice, exposes a stdin/stdout (JSON-RPC or simple line protocol) interface, and is itself GPL-licensed. The main Stylus desktop app launches the helper as a child process when sync is needed, communicates over pipes, and never directly links the GPL code. The helper binary ships alongside the app inside its `.app` bundle.

```
Stylus.app/Contents/
  MacOS/
    Stylus                  <- main app, your license
    stylus-sync-helper      <- GPL, statically links libimobiledevice
  Resources/
    LICENSES/
      libimobiledevice.txt  <- redistribution requires source availability
                                or a written offer; a link to the upstream
                                project's tag suffices
```

### Architecture (Mac side)

```
+-------------------------------------------------------+
|  Stylus (main desktop app, your license)              |
|  +-------------------------------------------------+  |
|  |  src/sync/SyncEngine                            |  |  <-- diff Mac library
|  |    - enumerate Mac library                      |  |      against device snapshot,
|  |    - request device snapshot                    |  |      decide what to push / delete
|  |    - merge .styl metadata both ways             |  |
|  |    - drive helper via line protocol             |  |
|  +-------------------------------------------------+  |
|  +-------------------------------------------------+  |
|  |  src/sync/DeviceMonitor                         |  |  <-- listen for device connect /
|  |    - polls / observes helper for device events  |  |      disconnect, expose state
|  +-------------------------------------------------+  |
|                          |                            |
|                          v   (stdin/stdout pipes)     |
+-------------------------------------------------------+
+-------------------------------------------------------+
|  stylus-sync-helper (GPL, static libimobiledevice)    |
|    commands: list-devices, list-files, read-file,     |
|              write-file, delete-file, mkdir, stat     |
|    AFC <-> usbmuxd <-> iPhone (Stylus.app sandbox)    |
+-------------------------------------------------------+
```

The helper is dumb: just a transport. All sync logic (what to push, conflict resolution, progress reporting) stays in the main app, in `SyncEngine`.

### What gets synced

| Item                          | Direction       | Notes                                                          |
|-------------------------------|-----------------|----------------------------------------------------------------|
| Audio files (mp3/m4a/aiff/wav)| Mac -> iPhone   | New / changed pushed; removed deleted on iPhone                |
| `.styl` sidecars              | both directions | Mac wins for title/artist/album/bpm/key; iPhone wins for `playCount` and last-played timestamp; merged file written back to both sides |
| Playlists                     | Mac -> iPhone   | Serialized as a single `Playlists.json` file                   |
| Library cache JSON            | not synced      | iPhone rebuilds its own cache after first scan                 |
| Album art override files (cover.jpg etc.) | Mac -> iPhone | Travel with the album folder                              |
| Embedded artwork              | n/a             | Already inside the audio file                                  |

The two-way `.styl` merge is the only non-trivial piece. Without it, every sync would clobber the phone's play counts. Logic:

1. Pull device's `.styl` for every track present on both sides
2. For each track: if `device.playCount > mac.playCount`, take device's count + last-played; else take Mac's
3. All other fields: Mac wins
4. Write merged file to both sides

### Triggering

- **Manual:** "Sync Now" button in the Devices sidebar entry
- **Auto on connect:** opt-in Preferences toggle. Default off (avoid the iTunes-circa-2010 "every cable plug starts a sync" annoyance)
- **No auto on file change:** background filesystem watching is more trouble than it's worth for v1

### Wi-Fi sync (free)

libimobiledevice supports Wi-Fi sync for any device that's been USB-paired at least once and has "Sync over Wi-Fi" enabled in iPhone settings. Same code path. The Devices sidebar entry just shows a Wi-Fi indicator vs. cable indicator.

### Selective sync (deferred to v2)

For v1, sync is whole-library. Users with a 500 GB library and a 128 GB iPhone need a "Selected playlists only" mode, but defer that until the basic sync engine is shipped and stable. The diff engine already operates per-track, so selective sync is a UI + filtering layer added on top, not a core architectural change.

### Doesn't change the iOS architecture

The iOS app sees its filesystem identically regardless of how files arrived (Files app drag, AirDrop, sync). The only iOS-side change for sync support is: **rescan on app foreground**. The library scanner already supports incremental rescans, so this is a one-liner observing `UIApplication.willEnterForegroundNotification`.

## 12. Risks and open questions

1. **Swift-C++ interop maturity (2026):** the `extern "C"` facade route is robust but verbose. Direct Swift-C++ interop (no facade) would let Swift call C++ classes directly, but JUCE's templates and `juce::String` semantics may trip it up. Recommendation: facade for v1, revisit interop once it's exercised.

2. **JUCE on iOS without juce_audio_devices:** confirmed by inspection that none of the core sources require it. `AudioFormatReader` (juce_audio_formats) handles file decoding without a device manager. Verify this in Phase 0 by linking only the four modules.

3. **`juce::ApplicationProperties` on iOS:** resolves to a plist in `Library/Preferences/`. This is the right location and works in the sandbox. Confirmed.

4. **File coordinator vs. plain NSFileManager:** if iCloud is added later, file reads need NSFileCoordinator. For v1 (Documents folder only) plain `juce::File` is fine.

5. **Simulator audio routing:** simulator doesn't fully simulate AVAudioSession. Test on real hardware before shipping.

6. **App Store review:** Stylus reads files the user supplies. No issue with review as long as you don't claim to bypass DRM or download copyrighted content. Apple Music lookup hits the public iTunes Search API which is allowed.

7. **`.styl` files as hidden dotfiles:** iOS Files app does show dotfiles in some contexts but treats them as hidden in others. Consider whether to keep the dot prefix on iOS or write `.styl` files non-hidden. Keeping the dot prefix preserves compatibility with desktop libraries copied over.

8. **libimobiledevice GPL contamination (Phase 9 only):** if Stylus is ever distributed, the sync engine must ship as a separate GPL helper binary (see section 11). The boundary is clean as long as the main app talks to the helper via stdin/stdout or XPC and never links libimobiledevice symbols. For a personal / non-distributed build, irrelevant.

9. **AFC protocol stability:** Apple has tightened private API access over recent macOS versions, but `usbmuxd` and AFC are used by Finder itself, so the surface libimobiledevice depends on isn't going anywhere. Low risk, but worth pinning a libimobiledevice version and re-testing on every major macOS release.

10. **Two-way `.styl` merge edge cases:** if the user edits the same track's metadata on both Mac and phone between syncs, the merge rules (Mac wins for everything except play count + last-played) will silently drop the phone-side edits. Acceptable for v1; revisit if it bites in practice.

## 13. First-week checklist

- [ ] Phase 0 in desktop repo: `StylusCore` target, `UIConstants.h` split, drop `AlbumArtExtractor.cpp` from core
- [ ] New repo `stylus-ios`, Package.swift with `StylusApp` / `StylusBridge` / `StylusCore` targets
- [ ] Vendor `StylusCore/src/` and JUCE submodule (sparse-checkout the four needed modules if size matters)
- [ ] Compile `StylusCore` standalone on iOS simulator (no app yet)
- [ ] Bridge: `Stylus_LibraryCreate` + `Stylus_LibraryStartScan` + `Stylus_StylLoad`
- [ ] Swift: one `List` of tracks, tap-to-play with `AVAudioPlayer` (skip AVAudioEngine for this milestone)
- [ ] Drop a small `Music/` folder into the simulator's Files app, see the list populate, hear a track play
- [ ] Take screenshot, declare phase 1 done

After this checklist runs end-to-end, the rest is incremental and the architecture is proven.
