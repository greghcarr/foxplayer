#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

class SongInfoEditor : public juce::Component
{
public:
    // Single track. Mode (music vs podcast) is derived from track.isPodcast.
    explicit SongInfoEditor(const TrackInfo& track);

    // Multi-track edit (all tracks must be the same type).
    // Mode is derived from tracks.front().isPodcast.
    explicit SongInfoEditor(const std::vector<TrackInfo>& tracks);

    // Called with all edited tracks on Save (1 element for single-track mode).
    std::function<void(std::vector<TrackInfo>)> onSave;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;
    void focusOfChildComponentChanged(juce::Component::FocusChangeType) override;

private:
    enum class Mode { SingleMusic, SinglePodcast, MultiMusic, MultiPodcast };

    void init();
    void save();
    juce::String findCommonPrefix() const;

    Mode                   mode_;
    std::vector<TrackInfo> tracks_;
    juce::String           detectedPrefix_;  // common prefix at open time (multi-mode)

    juce::Label titleLabel_       { {}, "Title" };
    juce::Label titlePrefixLabel_ { {}, "Prefix" };
    juce::Label artistLabel_      { {}, "Artist" };
    juce::Label podcastLabel_     { {}, "Podcast" };
    juce::Label albumLabel_       { {}, "Album" };
    juce::Label genreLabel_       { {}, "Genre" };
    juce::Label yearLabel_        { {}, "Year" };
    juce::Label trackNumLabel_    { {}, "Track #" };
    juce::Label episodeNumLabel_  { {}, "Episode #" };
    juce::Label bpmLabel_         { {}, "BPM" };
    juce::Label keyLabel_         { {}, "Key" };
    juce::Label fileLabel_;
    juce::Label hintLabel_;

    juce::TextEditor titleEdit_;
    juce::TextEditor artistEdit_;
    juce::TextEditor podcastEdit_;
    juce::TextEditor albumEdit_;
    juce::TextEditor genreEdit_;
    juce::TextEditor yearEdit_;
    juce::TextEditor trackNumEdit_;
    juce::TextEditor episodeNumEdit_;
    juce::TextEditor bpmEdit_;
    juce::TextEditor keyEdit_;

    juce::TextButton saveButton_   { "Save" };
    juce::TextButton cancelButton_ { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongInfoEditor)
};

} // namespace FoxPlayer
