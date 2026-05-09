#pragma once

#include "audio/TrackInfo.h"
#include "JuceCore.h"
#include <functional>
#include <deque>

namespace Stylus
{

// Accepts TrackInfo items into a queue and analyses them one at a time on a
// background thread (BPM + key). Results are saved to .styl and reported back
// to the message thread via onTrackAnalysed.
class AnalysisEngine : private juce::Thread
{
public:
    AnalysisEngine();
    ~AnalysisEngine() override;

    // Enqueues a track for analysis. Skips if already analysed (.styl exists with results).
    void enqueue(const TrackInfo& track);

    // Enqueues a whole library; tracks with existing .styl data are skipped.
    void enqueueAll(const std::vector<TrackInfo>& tracks);

    // Fast loudness-only path: measure LUFS and skip BPM / key detection.
    // Used for the lazy-on-first-play hook so an unanalysed track gets a
    // gain offset within ~a second or two, without paying for the heavier
    // BPM / key passes the user hasn't asked for yet.
    void enqueueLufsOnly(const TrackInfo& track);

    void cancelAll();
    bool isAnalysing() const;

    // All callbacks fire on the message thread.
    std::function<void(TrackInfo)> onTrackQueued;     // added to queue
    std::function<void(TrackInfo)> onTrackStarted;    // analysis begun
    std::function<void(TrackInfo)> onTrackAnalysed;   // analysis finished

private:
    enum class Mode { Full, LufsOnly };
    struct QueueEntry
    {
        TrackInfo track;
        Mode      mode;
    };

    void run() override;
    void analyseOne(TrackInfo track, Mode mode);

    juce::AudioFormatManager     formatManager_;
    std::deque<QueueEntry>       queue_;
    juce::CriticalSection        queueLock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnalysisEngine)
};

} // namespace Stylus
