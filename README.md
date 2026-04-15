# FoxPlayer

A personal music player for macOS, built with C++17 and JUCE 8.

## Features

- Library browser with search, sorting, and hidden track support
- Playback with seek bar, volume control, and album art display
- BPM and musical key analysis (on demand via right-click)
- Play queue panel
- Sidebar with Music and Playlists views
- Drag-and-drop tracks from library onto playlists
- Playlist create, rename, and delete
- Per-track metadata editing
- Play count tracking
- `.foxp` JSON sidecar files for storing analysis results and user metadata

## Build

Requires CMake 3.22+ and Xcode Command Line Tools. JUCE 8.0.4 is fetched automatically.

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
open build/FoxPlayer_artefacts/Debug/FoxPlayer.app
```

## Architecture

See [CLAUDE.md](CLAUDE.md) for a full architecture reference.
