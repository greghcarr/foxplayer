# FoxPlayer — Architecture Reference

## Overview
C++17/JUCE 8.0.4 audio player for macOS. Current state: library browser, basic playback, play queue, sidebar with playlists, album art display, BPM/key analysis, play count tracking.
Long-term: DJ mode, beatgrid detection, Rekordbox/Serato metadata export.

## Build
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
open build/FoxPlayer_artefacts/Debug/FoxPlayer.app
```
Requires: CMake 3.22+, Xcode CLT. JUCE 8.0.4 is fetched automatically via FetchContent.

## File Layout
```
src/
  Main.cpp                      — JUCEApplication entry point
  MainWindow.h                  — DocumentWindow shell
  MainComponent.h/.cpp          — Top-level layout, wires all subsystems together
  Constants.h                   — All magic numbers, colors, column IDs
  audio/
    TrackInfo.h                 — Plain struct: file + metadata + duration + playCount
    PlayQueue.h/.cpp            — Ordered track list + current index; advance/prev/jump
    AudioEngine.h/.cpp          — Full audio pipeline; owns DeviceManager/TransportSource
    FoxpFile.h/.cpp             — Read/write .foxp JSON sidecar files (dot-prefixed, hidden)
    AlbumArtExtractor.h         — JUCE-side: calls ObjC bridge, falls back to folder art
    AlbumArtExtractor.cpp       — JUCE-side implementation (no ObjC here)
    AlbumArtExtractor.mm        — Pure ObjC++: AVFoundation album art extraction (no JUCE types)
  analysis/
    BpmDetector.h/.cpp          — BPM detection
    KeyDetector.h/.cpp          — Musical key detection
    AnalysisEngine.h/.cpp       — Queue-based analysis; fires onTrackAnalysed callback
  library/
    LibraryScanner.h/.cpp       — Background thread: recursive scan + metadata extraction
    LibraryTableComponent.h/.cpp — TableListBox showing the library; drag source for DnD
    PlaylistStore.h/.cpp        — Persists playlists to ApplicationProperties as JSON
  ui/
    TransportBar.h/.cpp         — Bottom bar: album art, play/pause, seek, volume, queue toggle
    QueueView.h/.cpp            — Right-side panel showing current play queue
    SidebarComponent.h/.cpp     — Left sidebar: Music + Playlists; DragAndDropTarget
    SongInfoEditor.h/.cpp       — Modal dialog for editing track metadata
```

## Key Patterns

### Format registration
`registerBasicFormats()` already includes CoreAudioFormat on macOS — do not call `registerFormat(new CoreAudioFormat(), ...)` separately or JUCE will assert on the duplicate.

### Audio pipeline
```
AudioDeviceManager
  -> AudioSourcePlayer (registered as device callback)
     -> AudioTransportSource
        -> AudioFormatReaderSource (swapped per track)
           -> AudioFormatReader (CoreAudioFormat: MP3/AAC/ALAC/AIFF/WAV)
```
All owned by `AudioEngine`. Never call `transportSource_` from an audio thread callback — JUCE handles thread safety internally.

### Track-end detection
`AudioEngine` registers as a `ChangeListener` on `AudioTransportSource`. On change, if `!isPlaying() && !paused_` the track has finished naturally. `onTrackFinished` callback is fired on the message thread.

### Play queue
When a library row is activated, all tracks from that row to the end of the current visible (filtered) order become the new queue. `PlayQueue` stores `std::vector<TrackInfo>` + `int currentIndex`.

### Library data ownership
`fullLibrary_` in `MainComponent` is the single source of truth for all scanned tracks. `LibraryTableComponent` holds a filtered view (pointers into a local copy). When switching sidebar items, `MainComponent` calls `libraryTable_.setTracks(subset)` — it never reads back from the table as the authoritative source, except via `allTracks()` when `onLibraryChanged` fires (hidden state change).

### Sidebar IDs
- `1` = Music (full library)
- `1000 + playlistStore_.id` = a specific playlist

### Playlists
`PlaylistStore` persists playlists as JSON in `ApplicationProperties`. Each playlist stores ordered file paths. `onPlaylistsChanged` callback fires whenever a playlist is created or modified.

### Companion file format (.foxp)
Each `track.mp3` gains a hidden sibling `.track.mp3.foxp` (JSON). Stores: title, artist, album, genre, year, trackNumber, bpm, key, lufs, hidden, playCount. `FoxpFile::load()` is called by `LibraryScanner` after metadata extraction so user edits always win. `FoxpFile::save()` is called on analysis completion, hide/unhide, edit, and play count increment.

Old-style (non-dot-prefixed) `.foxp` files are deleted on `loadMusicFolder()`.

### AlbumArtExtractor — ObjC/JUCE split
AVFoundation and JUCE cannot share a translation unit: CarbonCore's `Point`/`Component` clash with JUCE's. Solution:
- `AlbumArtExtractor.mm` — pure ObjC++, no JUCE headers, exposes `extern "C" unsigned char* FoxPlayer_extractEmbeddedArtwork(const char*, size_t*)`
- `AlbumArtExtractor.cpp` — pure C++/JUCE, calls the C bridge, creates `juce::Image`, falls back to cover.jpg/folder.jpg/artwork.jpg in the same directory

### Drag-and-drop (library → playlist)
`MainComponent` inherits `juce::DragAndDropContainer`. `LibraryTableComponent` adds itself as a mouse listener on its internal `table_` and calls `startDragging()` with `\n`-separated file paths when a drag gesture exceeds the threshold. `SidebarComponent` implements `juce::DragAndDropTarget`; only playlist items (id >= 1000) are valid drop targets. On drop, `onTracksDropped` fires on `MainComponent`, which checks for duplicates and shows an `AlertWindow` for single-track drops.

### Non-ASCII strings in JUCE
JUCE interprets `const char*` as Latin-1. Always wrap UTF-8 emoji/non-ASCII with `juce::String(juce::CharPointer_UTF8("..."))`. Example: `"\xe2\x99\xab"` = ♫, `"\xf0\x9f\x93\x84"` = 📄.

### MIDI suppression
`JUCE_USE_MIDI_INPUTS=0` and `JUCE_USE_MIDI_OUTPUTS=0` are set in CMakeLists.txt to prevent CoreMIDI assertions at startup.

## Conventions
- All tunable values (colors, sizes, column IDs) live in `Constants.h` — never inline.
- `juce::ignoreUnused()` for intentionally unused parameters (no `-Wunused` warnings).
- Callbacks are `std::function<>` members — assign directly, never subclass for simple notifications.
- No `new`/`delete` for owned resources — use `std::unique_ptr` or JUCE's `std::make_unique`.
- Geometry is manual bounds math via `juce::Rectangle` — no physics.
- Hidden files/folders (any path segment starting with `.`) are skipped during library scanning.

## Adding a New Feature Checklist
1. If it needs a tunable value, add it to `Constants.h` first.
2. If it needs a new audio format, register it in `AudioEngine` constructor.
3. If it stores per-track data, add a field to `TrackInfo` and update `FoxpFile::load()`/`save()`.
4. If it needs periodic UI updates, use `juce::Timer` (not a background thread for UI).
5. If it modifies playlist contents, go through `PlaylistStore` — never mutate `Playlist::trackPaths` directly.
