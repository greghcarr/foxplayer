#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>

namespace FoxPlayer
{

class SongInfoEditor : public juce::Component
{
public:
    explicit SongInfoEditor(const TrackInfo& track);

    // Called with the edited track when the user clicks Save.
    std::function<void(TrackInfo)> onSave;

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void save();

    TrackInfo track_;

    juce::Label titleLabel_    { {}, "Title" };
    juce::Label artistLabel_   { {}, "Artist" };
    juce::Label albumLabel_    { {}, "Album" };
    juce::Label genreLabel_    { {}, "Genre" };
    juce::Label yearLabel_     { {}, "Year" };
    juce::Label trackNumLabel_ { {}, "Track #" };
    juce::Label fileLabel_;

    juce::TextEditor titleEdit_;
    juce::TextEditor artistEdit_;
    juce::TextEditor albumEdit_;
    juce::TextEditor genreEdit_;
    juce::TextEditor yearEdit_;
    juce::TextEditor trackNumEdit_;

    juce::TextButton saveButton_   { "Save" };
    juce::TextButton cancelButton_ { "Cancel" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SongInfoEditor)
};

} // namespace FoxPlayer
