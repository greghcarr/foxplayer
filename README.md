# Stylus

A personal music and podcast player for **macOS and Windows 11**, built with C++17 and JUCE 8.

## Requirements

### macOS

- Apple Silicon or Intel
- Xcode Command Line Tools
- CMake 3.22 or later

### Windows 11

- Visual Studio 2022 with the "Desktop development with C++" workload (MSVC v14.4x + Windows 10/11 SDK)
- CMake 3.22 or later

JUCE 8.0.4 is downloaded automatically at configure time via CMake FetchContent on both platforms; no manual installation needed.

## Build

### macOS

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

For a release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

### Windows

Visual Studio uses a multi-config generator, so the configuration is selected at build time rather than configure time:

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug --parallel
```

For a release build, swap `--config Debug` for `--config Release`.

## Launch

### macOS

```bash
open build/Stylus_artefacts/Debug/Stylus.app
```

Or double-click the `.app` in Finder after building.

### Windows

```powershell
& "build\Stylus_artefacts\Debug\Stylus.exe"
```

Or double-click `Stylus.exe` in Explorer at `build\Stylus_artefacts\Debug\`.

## Features

### Library

- Recursive folder scanning for music and podcasts (multiple roots supported)
- On-disk library cache so the app starts with the previous library visible while a fresh scan runs in the background
- Supports MP3, FLAC, WAV, AIFF, OGG, Opus on both platforms; AAC, ALAC, M4A via CoreAudio on macOS and via Media Foundation on Windows
- Search, multi-column sort, hidden tracks
- Columns: Title, Artist, Album, Genre, Time, BPM, Key, Plays, Format, Date Added (each sortable, hideable, resizable)
- Column visibility, width, and order persist per view mode
- Sidebar sections: Library, Artists, Albums, Genres, Playlists, Podcasts (each collapsible)
- Scrollable sidebar with drag-to-resize divider
- Per-view selection memory: each sidebar view remembers its own selection by file path; switching views shows nothing selected unless that view had a prior selection, returning restores it

### Playback

- Play, pause, seek, volume, mute
- Spinning CD artwork with embedded album art, falls back to a "now playing" label
- Embedded album art read via AVFoundation on macOS and via the Windows Shell property system on Windows; folder / `cover.jpg` / sidecar fallback on both
- Clickable artist, title, and "Playing from" links in the transport bar for quick navigation
- Previous / next / shuffle / repeat (off, repeat all, repeat one)
- Pressing previous within the first 3 seconds restarts the current track
- Automatic track advance at end of song
- Per-track play count, incremented when playback begins
- Mini-player mode: shrink the window past the threshold and the transport bar reflows to a compact centred layout; the library / sidebar collapse out of view
- Always-on-top pin button (toggle on the right edge of the transport bar)
- Media keys + lock-screen / Now Playing surface integration: macOS uses MPNowPlayingInfoCenter + MPRemoteCommandCenter, Windows uses SystemMediaTransportControls (the small media flyout above the volume slider, the lock screen, Bluetooth headset controls)
- System-tray / status-bar icon with a "Show / Quit" context menu

### Queue

- Resizable side panel (drag the left edge); min = sidebar minimum width, max = 40% of window width
- "Up Next" header reads "Play Queue"
- Toggling shuffle moves the currently playing track to index 0 and Fisher-Yates shuffles everything else; toggling off restores the original order with the playing track at its original position
- After shuffle / unshuffle, the panel scrolls so the playing track is vertically centred
- Activating a row from the library queues every track in the current visible (filtered, sorted) view, with the activated track playing
- Appending a folder / album to a shuffled queue extends both the live order and the saved natural order so un-shuffle covers the additions
- Right-click any queue row to "Remove from Queue" (disabled on the currently playing track)
- Queue toggle button auto-hides when the queue is empty

### Playlists

- Create, rename, delete playlists
- Drag tracks from the library onto a playlist in the sidebar
- Drag rows within a playlist to reorder
- Drag playlists in the sidebar to reorder them

### Metadata

- Per-track Edit Info dialog (title, artist, album, genre, year, track number)
- Bulk editing across multiple selected tracks of the same type, with prefix/suffix helpers
- "Clear Info" resets fields to embedded tag values
- "Look up on Apple Music" auto-fills metadata from the iTunes Search API, with undo
- "Look up Album Art" fetches and embeds cover art, with undo
- Batch Apple Music lookups (multi-select) update silently in the background; single lookups follow the track to its new position
- Network failures are auto-retried after a delay, with a circuit-breaker after consecutive failures
- Metadata persists in hidden `.styl` JSON sidecar files alongside each audio file (dot-prefixed and `FILE_ATTRIBUTE_HIDDEN` on Windows so they stay out of Explorer's way)
- Tab through fields auto-selects all text in the focused field
- Edit Info Next / Previous re-reads the current sorted order at click time, so a lookup that re-sorts the row still navigates to the correct neighbour

### Analysis

- On-demand BPM, musical key, and LUFS loudness detection via right-click
- Analysis log window with queued / running / completed state per track
- Background analysis only writes BPM / key / LUFS to disk (it re-loads the styl first), so concurrent user edits are never clobbered

### Audio

- Output device selection in Preferences (follows system default automatically)
- Buffer size selection
- Persistent volume and mute across launches
- **Volume normalization** (BS.1770 K-weighted LUFS, -14 LUFS target with ±6 dB cap): when enabled, each track's playback gain is offset based on its measured loudness so quiet tracks come up and loud tracks come down. Tracks that haven't been analyzed yet trigger a quick lazy LUFS measurement on first play (the result is cached in the .styl sidecar). Gain transitions ramp over 2 seconds in dB-space so toggling the feature mid-track doesn't lurch. Toggle in Preferences → Audio, or via the small slider+check button on the right side of the transport bar (below the speaker icon).

### Podcast Support

- Separate podcast folder scanning
- Episode-number auto-detection from filenames (multiple heuristic patterns)
- Show name auto-detected from album tag or parent folder
- "Look up on Podcast Index" from the right-click menu

## Right-Click Context Menu

Right-clicking one or more tracks in the library shows:

- **Play Next**: insert immediately after the currently playing track
- **Add to Queue**: append to the current play queue
- **Add to Playlist** ▸ existing playlists, or **+ Create New Playlist** (intelligent name from selection: same album wins, otherwise same artist, otherwise "New Playlist")
- **Edit Info**: open the metadata editor (also Cmd/Ctrl-R)
- **Clear Info**: revert to embedded tag values
- **Look up on Apple Music**: auto-fill metadata, with undo
- **Look up Album Art**: fetch and embed cover art, with undo
- **Look up on Podcast Index**: fetch podcast episode metadata (podcasts only)
- **Analyze for Key and BPM**: queue background analysis (also fills LUFS for normalization)
- **Go to Artist / Album / Podcast**: jump to the track's sidebar view
- **Hide from Library / Unhide from Library**: toggle visibility
- **Show in Finder** (single selection)
- **Remove from Queue / Remove from Playlist**: contextual on the queue panel and playlist views

Right-clicking sidebar items (artists, albums, playlists, genres, per-show podcasts) offers Play Next, Add to Queue, and Add to Playlist (with the same "+ Create New Playlist" option that uses the row's label as the suggested name). Playlist rows additionally offer Rename, Duplicate, and Delete.

**Double-click** any sidebar row (playlist, artist, album, genre, podcast) to inline-rename it. Renaming an artist / album / genre / podcast updates every track in that group on disk; album rows expect "Artist - Album" and silently revert if the separator's missing.

## Keyboard Shortcuts

The modifier is **Cmd** on macOS and **Ctrl** on Windows; JUCE auto-translates.

### Global

| Shortcut | Action |
|---|---|
| Space | Play / Pause |
| Q | Show / hide queue panel |
| Cmd/Ctrl-, | Open Preferences |
| Cmd/Ctrl-F | Focus search box |
| Cmd/Ctrl-R | Edit Info for the current selection |
| Shift-Cmd/Ctrl-L | Toggle Analysis Log window |
| Shift-Cmd/Ctrl-P | Toggle Always on Top |
| Enter | Play selected track (when in library) |
| Delete | Hide selected track(s) from library / remove from playlist or queue |

### Navigation between panes

The sidebar, library, and queue act as three focusable panes; the visual highlight always lives on the currently focused one. Each pane remembers its cursor position when you leave and restores it on return.

| Shortcut | Action |
|---|---|
| Right / Tab | Move focus from sidebar → library |
| Left / Shift-Tab | Move focus from library → sidebar |
| Right / Tab | Move focus from library → queue (silent no-op if empty; opens panel if hidden) |
| Left / Shift-Tab | Move focus from queue → library (queue stays open) |
| Tab | From queue → sidebar |
| Shift-Tab | From sidebar → queue |
| Up | From row 0 of library → search box |
| Down | From search box → first matching library row |

### Within the sidebar

| Shortcut | Action |
|---|---|
| Up / Down | Move cursor between rows |
| PgUp / PgDn | Jump 8 rows |
| Home / End | First / last row across the whole sidebar |
| Cmd/Ctrl-Up / Cmd/Ctrl-Down | First / last row of the *current section* |
| Alt/Option-Up / Alt/Option-Down | Jump to first row of the previous / next section (collapses current, expands target) |
| Enter | Hand focus to the library |
| (any letter) | Type-ahead: jump to first row in current section starting with the typed prefix; multi-letter refinement within 1 s |

### Within the library or queue

| Shortcut | Action |
|---|---|
| Up / Down / PgUp / PgDn / Home / End | Native list navigation |
| Cmd/Ctrl-Up / Cmd/Ctrl-Down | Jump to first / last row |

## Preferences

Open with **Cmd/Ctrl-,** or via the **File** menu (the **Stylus** application menu on macOS).

- **Audio**: output device (defaults to system default), buffer size, "Normalize playback volume" toggle
- **Library**: add, remove, or rescan music and podcast folders independently
- **Display**: toggle between spinning vinyl artwork and a static square
- **Misc**: Ask before quitting toggle

## Window Behaviour

### macOS

- **Closing the window** hides it instead of quitting (app-style). Cmd-Q (or "Quit Stylus" from the app menu) actually quits.
- **Clicking the Dock icon** while a window is hidden re-shows it on the display under the mouse cursor.
- **Clicking the Dock icon** while the window is visible on another macOS Space switches to that Space rather than dragging the window to the active Space.

### Windows

- **Closing the window** quits the app via the standard quit-confirmation flow (matching the OS convention; closing a music player's main window shouldn't leave music playing in the background).
- **Title bar** uses Win11's immersive dark variant so it matches the dark JUCE-rendered window body instead of clashing with the rest of the UI.

## Session Persistence

Stylus remembers between launches:

- Music and podcast folder selections
- Active sidebar view
- Current play queue and playing track position
- Shuffle and repeat state
- Volume and mute state
- Sidebar width, queue panel width
- Column visibility, widths, and order per view mode

Playback never auto-resumes on launch, by design.

## Architecture Reference

See [CLAUDE.md](CLAUDE.md) for the internal architecture reference (audio pipeline, file layout, key patterns, cross-platform structure, conventions).
