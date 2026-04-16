#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <deque>

namespace FoxPlayer
{

// Accepts TrackInfo items into a queue and analyses them one at a time on a
// background thread (BPM + key). Results are saved to .foxp and reported back
// to the message thread via onTrackAnalysed.
class AnalysisEngine : private juce::Thread
{
public:
    AnalysisEngine();
    ~AnalysisEngine() override;

    // Enqueues a track for analysis. Skips if already analysed (.foxp exists with results).
    void enqueue(const TrackInfo& track);

    // Enqueues a whole library; tracks with existing .foxp data are skipped.
    void enqueueAll(const std::vector<TrackInfo>& tracks);

    void cancelAll();
    bool isAnalysing() const;

    // All callbacks fire on the message thread.
    std::function<void(TrackInfo)> onTrackQueued;     // added to queue
    std::function<void(TrackInfo)> onTrackStarted;    // analysis begun
    std::function<void(TrackInfo)> onTrackAnalysed;   // analysis finished

private:
    void run() override;
    void analyseOne(TrackInfo track);

    juce::AudioFormatManager     formatManager_;
    std::deque<TrackInfo>        queue_;
    juce::CriticalSection        queueLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisEngine)
};

} // namespace FoxPlayer
