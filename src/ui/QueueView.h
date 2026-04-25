#pragma once

#include "audio/PlayQueue.h"
#include <JuceHeader.h>
#include <functional>

namespace FoxPlayer
{

// Panel that shows the current play queue as a simple list.
// Slides in from the right edge of MainComponent.
class QueueView : public juce::Component,
                  public juce::ListBoxModel
{
public:
    QueueView();
    ~QueueView() override = default;

    void refresh(const PlayQueue& queue);

    // Scrolls so the currently playing row is vertically centred in the
    // visible area; clamped to the valid scroll range so no rows are hidden.
    void centerPlayingRow();

    // juce::Component
    void paint(juce::Graphics& g) override;
    void resized() override;

    // juce::ListBoxModel
    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int w, int h, bool selected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;

    std::function<void(int queueIndex)> onRowActivated;
    std::function<void(int queueIndex)> onRemoveTrack;

private:
    juce::ListBox list_;
    juce::Label   header_;

    std::vector<TrackInfo> items_;
    int playingIndex_ { -1 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(QueueView)
};

} // namespace FoxPlayer
