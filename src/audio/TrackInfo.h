#pragma once

#include <JuceHeader.h>

namespace FoxPlayer
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

    // --- Analysis results (populated from .foxp or by analysis engine) ---
    double       bpm          { 0.0 };   // 0 = not yet analysed
    juce::String musicalKey;             // e.g. "Am", "C#", "" = not analysed
    float        lufs         { 0.0f };  // integrated loudness, 0 = not analysed

    // --- User flags ---
    bool         hidden       { false }; // excluded from library view; persisted in .foxp

    // --- Play statistics ---
    int          playCount    { 0 };     // incremented each time the track starts playing

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

} // namespace FoxPlayer
