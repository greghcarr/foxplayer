#pragma once

#include "JuceCore.h"

namespace Stylus
{

struct TrackInfo
{
    // --- ID3 / file metadata (populated by LibraryScanner) ---
    juce::File   file;
    juce::String title;
    juce::String artist;
    juce::String album;
    juce::String genre;
    juce::String year;
    int          trackNumber  { 0 };
    double       durationSecs { 0.0 };

    // --- Analysis results (populated from .styl or by analysis engine) ---
    double       bpm          { 0.0 };   // 0 = not yet analysed
    juce::String musicalKey;             // e.g. "Am", "C#", "" = not analysed
    float        lufs         { 0.0f };  // integrated loudness, 0 = not analysed

    // --- Podcast ---
    bool         isPodcast    { false }; // true if this file came from a podcast folder
    juce::String podcast;               // podcast/show name (replaces artist in podcast views)

    // --- User flags ---
    bool         hidden       { false }; // excluded from library view; persisted in .styl

    // --- Play statistics ---
    int          playCount    { 0 };     // incremented each time the track starts playing

    // --- Library metadata ---
    juce::int64  dateAdded    { 0 };     // ms since epoch; set on first scan, persisted in .styl

    // Transient (not persisted): set true by StylFile::load when the .styl
    // contained an explicit trackNumber property. Lets the podcast scanner
    // skip its guessEpisodeNumber heuristic when the user has committed a
    // value to disk (including 0, meaning "no episode number").
    bool         stylHadTrackNumber { false };

    bool isValid() const { return file.existsAsFile(); }

    juce::String formattedDuration() const
    {
        if (durationSecs <= 0.0) return "--:--";
        const int totalSecs = static_cast<int>(durationSecs);
        const int mins = totalSecs / 60;
        const int secs = totalSecs % 60;
        return juce::String::formatted("%d:%02d", mins, secs);
    }

    juce::String displayTitle() const
    {
        if (title.isNotEmpty()) return title;
        return file.getFileNameWithoutExtension();
    }
};

} // namespace Stylus
