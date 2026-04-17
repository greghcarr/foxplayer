#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <vector>

namespace FoxPlayer
{

// Table showing each enqueued/running/finished analysis with BPM + key results.
class AnalysisLogComponent : public juce::Component,
                              public juce::TableListBoxModel
{
public:
    AnalysisLogComponent();

    void trackQueued  (const TrackInfo& track);
    void trackStarted (const TrackInfo& track);
    void trackAnalysed(const TrackInfo& track);

    // Apple Music lookup entries share the same table; the status column shows
    // the lookup state and the BPM/Key columns are blank for lookup rows.
    void lookupQueued   (const TrackInfo& track);
    void lookupStarted  (const TrackInfo& track);
    void lookupCompleted(const TrackInfo& track, const juce::String& summary);

    // juce::Component
    void resized() override;
    void paint(juce::Graphics&) override;

    // juce::TableListBoxModel
    int  getNumRows() override;
    void paintRowBackground(juce::Graphics&, int row, int w, int h, bool selected) override;
    void paintCell(juce::Graphics&, int row, int col, int w, int h, bool selected) override;

private:
    enum class Type   { Analysis, Lookup };
    enum class Status { Queued, Running, Done };

    struct Entry
    {
        juce::File   file;
        juce::String title;
        juce::String artist;
        Type         type   { Type::Analysis };
        Status       status { Status::Queued };
        double       bpm    { 0.0 };
        juce::String key;
        juce::String lookupSummary;   // populated for completed lookup rows
    };

    int  findEntryIndex(const juce::File& file, Type type) const;
    void scrollToRow(int row);

    std::vector<Entry>    entries_;
    juce::TableListBox    table_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisLogComponent)
};

// Separate window hosting the log. Closing hides instead of destroys.
class AnalysisLogWindow : public juce::DocumentWindow
{
public:
    AnalysisLogWindow();

    void closeButtonPressed() override;

    AnalysisLogComponent& log();

private:
    AnalysisLogComponent* logComponent_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisLogWindow)
};

} // namespace FoxPlayer
