# FoxPlayer

A personal music and podcast player for macOS, built with C++17 and JUCE 8.

## Requirements

- macOS (Apple Silicon or Intel)
- Xcode Command Line Tools
- CMake 3.22 or later

JUCE 8.0.4 is downloaded automatically at configure time via CMake FetchContent — no manual installation needed.

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
- Recursive folder scanning for music and podcasts
- Supports MP3, AAC, ALAC, FLAC, WAV, AIFF, and other formats via CoreAudio
- Search, multi-column sort, and hidden track support
- Column visibility and width persist per view mode
- Sidebar sections: All Music, Artists, Albums, Playlists, Podcasts
- Resizable sidebar with drag-to-resize divider

### Playback
- Play, pause, seek, and volume control
- Spinning CD artwork display with embedded album art or artist/title label
- Clickable artist, title, and "Playing from" links in the transport bar for quick navigation
- Previous/next track, shuffle, and repeat (off / repeat all / repeat one)
- Pressing previous within the first 3 seconds restarts the current track
- Automatic track advance at end of song
- Play count tracking per track
- Mini-player mode: resize the window small to get a compact transport-only controller

### Queue
- Play queue panel (toggle with the queue button in the transport bar)
- "Add to Queue" from the right-click context menu
- Queue clears and rebuilds when a new track is activated from the library

### Playlists
- Create, rename, and delete playlists
- Drag tracks from the library onto a playlist in the sidebar
- Drag rows within a playlist to reorder them

### Metadata
- Per-track metadata editing via "Edit Info" (title, artist, album, genre, year, track number)
- Bulk editing across multiple selected tracks of the same type
- "Clear Info" to reset fields to embedded tag values
- "Look up on Apple Music" to auto-fill metadata from the Apple Music catalog, with undo
- "Look up Album Art" to fetch and embed cover art, with undo
- Metadata stored in hidden `.foxp` JSON sidecar files alongside each audio file
- After editing, the sidebar automatically navigates to the track's new location

### Analysis
- On-demand BPM and musical key detection via right-click
- Analysis log window showing queued, running, and completed jobs

### Audio
- Output device selection in Preferences (follows system default automatically)
- Volume control with mute toggle
- Persistent volume and mute state across launches

### Podcast Support
- Separate podcast folder scanning
- Episode metadata auto-detection (episode number, show name)
- "Look up on Podcast Index" from the right-click menu

## Right-Click Context Menu

Right-clicking one or more tracks in the library shows:

- **Edit Info** — open the metadata editor
- **Look up on Apple Music** — auto-fill metadata from the Apple Music catalog
- **Look up Album Art** — fetch and embed cover art
- **Look up on Podcast Index** — fetch podcast episode metadata (podcasts only)
- **Add to Queue** — append to the current play queue
- **Go to Artist / Go to Album / Go to Podcast** — jump to the track's sidebar view
- **Analyze** — queue BPM and key detection
- **Hide / Show** — toggle track visibility in the library
- **Remove from Playlist** — remove selected tracks (playlist view only)

## Keyboard Shortcuts

| Shortcut | Action |
|---|---|
| Space | Play / Pause |
| Cmd-, | Open Preferences |
| Cmd-F | Focus search box |
| Shift-Cmd-L | Toggle Analysis Log window |
| Enter | Play selected track |
| Delete | Hide selected track(s) from library |

## Preferences

Open with **Cmd-,** or via the File menu.

- **Audio**: select output device (defaults to system default)
- **Library**: add or remove music and podcast folders; changes trigger a rescan

## Session Persistence

FoxPlayer remembers the following between launches:

- Music and podcast folder selections
- Active sidebar view
- Current play queue and playing track position
- Shuffle and repeat state
- Volume and mute state
- Sidebar width
- Column visibility and widths per view mode

Playback does not auto-resume on launch.

## Architecture Reference

See [CLAUDE.md](CLAUDE.md) for the internal architecture reference (audio pipeline, file layout, key patterns, etc.).
