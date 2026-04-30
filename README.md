# FoxPlayer

A personal music and podcast player for macOS, built with C++17 and JUCE 8.

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools
- CMake 3.22 or later

JUCE 8.0.4 is downloaded automatically at configure time via CMake FetchContent, no manual installation needed.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

For a release build:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
```

## Launch

```bash
open build/FoxPlayer_artefacts/Debug/FoxPlayer.app
```

Or double-click the `.app` in Finder after building.

## Features

### Library
- Recursive folder scanning for music and podcasts (multiple roots supported)
- On-disk library cache so the app starts with the previous library visible while a fresh scan runs in the background
- Supports MP3, AAC, ALAC, FLAC, WAV, AIFF, OGG, Opus via CoreAudio
- Search, multi-column sort, hidden tracks
- Columns: Title, Artist, Album, Genre, Time, BPM, Key, Plays, Format, Date Added (each sortable, hideable, resizable)
- Column visibility, width, and order persist per view mode
- Sidebar sections: Library, Artists, Albums, Genres, Playlists, Podcasts (each collapsible)
- Scrollable sidebar with drag-to-resize divider
- Per-view selection memory: each sidebar view remembers its own selection by file path; switching views shows nothing selected unless that view had a prior selection, returning restores it

### Playback
- Play, pause, seek, volume, mute
- Spinning CD artwork with embedded album art, falls back to a "now playing" label
- Clickable artist, title, and "Playing from" links in the transport bar for quick navigation
- Previous / next / shuffle / repeat (off, repeat all, repeat one)
- Pressing previous within the first 3 seconds restarts the current track
- Automatic track advance at end of song
- Per-track play count, incremented when playback begins
- Mini-player mode: shrink the window past the threshold and the transport bar reflows to a compact centred layout, the library / sidebar collapse out of view
- Always-on-top pin button (toggle on the right edge of the transport bar)
- Media key support (play / pause / next / prev) and macOS "Now Playing" integration
- System tray (status bar) item with quick controls

### Queue
- Resizable side panel (drag the left edge), min = sidebar minimum width, max = 40% of window width
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
- Batch Apple Music lookups (multi-select) update silently in the background, single lookups follow the track to its new position
- Network failures are auto-retried after a delay, with a circuit-breaker after consecutive failures
- Metadata persists in hidden `.foxp` JSON sidecar files alongside each audio file
- Tab through fields auto-selects all text in the focused field
- Edit Info Next / Previous re-reads the current sorted order at click time, so a lookup that re-sorts the row still navigates to the correct neighbour

### Analysis
- On-demand BPM and musical key detection via right-click
- Analysis log window with queued / running / completed state per track
- Background analysis only writes BPM / key / LUFS to disk (it re-loads the foxp first), so concurrent user edits are never clobbered

### Audio
- Output device selection in Preferences (follows system default automatically)
- Buffer size selection
- Persistent volume and mute across launches

### Podcast Support
- Separate podcast folder scanning
- Episode-number auto-detection from filenames (multiple heuristic patterns)
- Show name auto-detected from album tag or parent folder
- "Look up on Podcast Index" from the right-click menu

## Right-Click Context Menu

Right-clicking one or more tracks in the library shows:

- **Edit Info**: open the metadata editor (also Cmd-R)
- **Look up on Apple Music**: auto-fill metadata, with undo
- **Look up Album Art**: fetch and embed cover art, with undo
- **Look up on Podcast Index**: fetch podcast episode metadata (podcasts only)
- **Add to Queue**: append to the current play queue
- **Go to Artist / Album / Genre / Podcast**: jump to the track's sidebar view
- **Analyze**: queue BPM and key detection
- **Hide / Show**: toggle track visibility in the library
- **Remove from Playlist**: remove selected tracks (playlist view only)

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Space | Play / Pause |
| Cmd-, | Open Preferences |
| Cmd-F | Focus search box |
| Cmd-R | Edit Info for the current selection |
| Shift-Cmd-L | Toggle Analysis Log window |
| Shift-Cmd-P | Toggle Always on Top |
| Enter | Play selected track |
| Delete | Hide selected track(s) from library |

## Preferences

Open with **Cmd-,** or via the File menu.

- **Audio**: output device (defaults to system default), buffer size
- **Library**: add, remove, or rescan music and podcast folders independently
- **Misc**: Ask before quitting toggle
- **Debug**: developer toggles (e.g. nuke `.foxp` sidecars)

## Window Behaviour

- **Closing the window** hides it instead of quitting (App-style). Cmd-Q (or "Quit FoxPlayer" from the app menu) actually quits.
- **Clicking the Dock icon** while a window is hidden re-shows it on the display under the mouse cursor.
- **Clicking the Dock icon** while the window is visible on another macOS Space switches to that Space rather than dragging the window to the active Space.

## Session Persistence

FoxPlayer remembers between launches:

- Music and podcast folder selections
- Active sidebar view
- Current play queue and playing track position
- Shuffle and repeat state
- Volume and mute state
- Sidebar width, queue panel width
- Column visibility, widths, and order per view mode

Playback never auto-resumes on launch, by design.

## Architecture Reference

See [CLAUDE.md](CLAUDE.md) for the internal architecture reference (audio pipeline, file layout, key patterns, conventions).
