#pragma once

#include <JuceHeader.h>

namespace FoxPlayer::Constants
{
    // Window
    static constexpr int defaultWindowWidth  = 1100;
    static constexpr int defaultWindowHeight = 700;
    static constexpr int minWindowWidth      = 430;   // record left-gap equals record-to-shuffle-button gap at this width
    static constexpr int minWindowHeight     = 135;   // transport bar + small buffer; library above stays invisible
    static constexpr int miniModeWidth       = 460;   // transport bar switches to mini layout below this width
    static constexpr int compactHeight       = 150;   // at-or-below this height the library/sidebar are effectively hidden

    // Transport bar
    static constexpr int transportBarHeight  = 108;

    // Volume slider
    static constexpr int volumeSliderWidth = 80;

    // Sidebar
    static constexpr int sidebarWidth        = 160;

    // Queue panel
    static constexpr int queuePanelWidth     = 300;

    // Library table columns (IDs must be > 0)
    static constexpr int colIdRow    = 1;
    static constexpr int colIdTitle  = 2;
    static constexpr int colIdArtist = 3;
    static constexpr int colIdAlbum  = 4;
    static constexpr int colIdTime   = 5;
    static constexpr int colIdBpm    = 6;
    static constexpr int colIdKey    = 7;
    static constexpr int colIdPlays  = 8;
    static constexpr int colIdFormat = 9;
    static constexpr int colIdGenre  = 10;

    static constexpr int colWidthRow    = 40;
    static constexpr int colWidthTitle  = 280;
    static constexpr int colWidthArtist = 180;
    static constexpr int colWidthAlbum  = 180;
    static constexpr int colWidthTime   = 60;
    static constexpr int colWidthBpm    = 55;
    static constexpr int colWidthKey    = 50;
    static constexpr int colWidthPlays  = 55;
    static constexpr int colWidthFormat = 60;
    static constexpr int colWidthGenre  = 120;

    static constexpr int rowHeight = 24;

    // Sidebar dynamic ID ranges
    static constexpr int noGenreId = 5999;  // reserved; real genres use 5000..5998

    // Scanner
    static constexpr int scannerBatchSize = 50;

    // Supported audio file extensions (lowercase)
    static const juce::StringArray supportedExtensions {
        "mp3", "flac", "wav", "aiff", "aif", "m4a", "aac", "alac", "ogg", "opus"
    };

    // Colors
    namespace Color
    {
        static const juce::Colour background       { 0xff1a1a1a };
        static const juce::Colour headerBackground { 0xff111111 };
        static const juce::Colour tableBackground  { 0xff1e1e1e };
        static const juce::Colour tableRowAlt      { 0xff232323 };
        static const juce::Colour tableSelected    { 0xff2a5a8a };
        static const juce::Colour tableHighlight   { 0xff3a7abf };
        static const juce::Colour transportBg      { 0xff0f0f0f };
        static const juce::Colour textPrimary      { 0xffe8e8e8 };
        static const juce::Colour textSecondary    { 0xff909090 };
        static const juce::Colour textDim          { 0xff555555 };
        static const juce::Colour accent           { 0xff4a9eff };
        static const juce::Colour playingHighlight { 0xff4a9eff };
        static const juce::Colour seekBarTrack     { 0xff333333 };
        static const juce::Colour seekBarFill      { 0xff4a9eff };
        static const juce::Colour buttonText       { 0xffe8e8e8 };
        static const juce::Colour border           { 0xff2a2a2a };
        static const juce::Colour scrollbarThumb   { 0xff707070 };
    }
}
