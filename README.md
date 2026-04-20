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
- Search, multi-column sorting, and hidden track support
- Sidebar sections: All Music, Artists, Albums, Playlists, Podcasts
- Resizable sidebar with drag-to-resize divider

### Playback
- Play, pause, seek, and volume control
- Spinning CD artwork display with album art or artist/title label
- Clickable artist name and song title in the transport bar for quick navigation
- Previous/next track, shuffle, and repeat (off / repeat all / repeat one)
- Automatic track advance at end of song
- Play count tracking per track

### Queue
- Play queue panel (toggle with the queue button)
- "Add to Queue" from the right-click context menu
- Queue clears and rebuilds when a new track is activated from the library

### Playlists
- Create, rename, and delete playlists
- Drag tracks from the library onto a playlist in the sidebar
- Drag rows within a playlist to reorder them

### Metadata
- Per-track metadata editing (title, artist, album, genre, year, track number)
- Bulk editing across multiple selected tracks
- "Clear Info" to reset fields to embedded tag values
- Metadata stored in hidden `.foxp` JSON sidecar files alongside each audio file

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

Playback does not auto-resume on launch.

## Architecture Reference

See [CLAUDE.md](CLAUDE.md) for a full internal architecture reference (audio pipeline, file layout, key patterns, etc.).
