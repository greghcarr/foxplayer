#include "AnalysisEngine.h"
#include "BpmDetector.h"
#include "KeyDetector.h"
#include "audio/FoxpFile.h"

namespace FoxPlayer
{

AnalysisEngine::AnalysisEngine()
    : juce::Thread("FoxPlayer.AnalysisEngine")
{
    formatManager_.registerBasicFormats();
}

AnalysisEngine::~AnalysisEngine()
{
    cancelAll();
}

void AnalysisEngine::enqueue(const TrackInfo& track)
{
    // Skip if the sidecar already has both BPM and key.
    if (track.bpm > 0.0 && track.musicalKey.isNotEmpty())
        return;

    {
        juce::ScopedLock sl(queueLock_);
        queue_.push_back(track);
    }

    if (onTrackQueued) onTrackQueued(track);

    if (!isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AnalysisEngine::enqueueAll(const std::vector<TrackInfo>& tracks)
{
    std::vector<TrackInfo> added;
    {
        juce::ScopedLock sl(queueLock_);
        for (const auto& t : tracks)
        {
            if (t.bpm > 0.0 && t.musicalKey.isNotEmpty())
                continue;
            queue_.push_back(t);
            added.push_back(t);
        }
    }

    if (onTrackQueued)
        for (auto& t : added)
            onTrackQueued(t);

    if (!queue_.empty() && !isThreadRunning())
        startThread(juce::Thread::Priority::low);
}

void AnalysisEngine::cancelAll()
{
    {
        juce::ScopedLock sl(queueLock_);
        queue_.clear();
    }
    signalThreadShouldExit();
    stopThread(4000);
}

bool AnalysisEngine::isAnalysing() const
{
    return isThreadRunning();
}

void AnalysisEngine::run()
{
    while (!threadShouldExit())
    {
        TrackInfo track;
        {
            juce::ScopedLock sl(queueLock_);
            if (queue_.empty()) break;
            track = queue_.front();
            queue_.pop_front();
        }

        analyseOne(track);
    }
}

void AnalysisEngine::analyseOne(TrackInfo track)
{
    DBG("AnalysisEngine - analysing: " + track.file.getFileName());

    juce::MessageManager::callAsync([this, t = track]() mutable {
        if (onTrackStarted) onTrackStarted(std::move(t));
    });

    bool changed = false;

    if (track.bpm <= 0.0)
    {
        const double bpm = BpmDetector::detect(track.file, formatManager_, {});
        if (bpm > 0.0)
        {
            track.bpm = bpm;
            changed = true;
            DBG("AnalysisEngine - BPM: " + juce::String(bpm, 1));
        }
    }

    if (threadShouldExit()) return;

    if (track.musicalKey.isEmpty())
    {
        const juce::String key = KeyDetector::detect(track.file, formatManager_, {});
        if (key.isNotEmpty())
        {
            track.musicalKey = key;
            changed = true;
            DBG("AnalysisEngine - Key: " + key);
        }
    }

    if (changed)
    {
        // Refresh non-analysis fields from the on-disk .foxp before saving so
        // we don't clobber user edits made while analysis was running.
        // Analysis owns bpm / musicalKey / lufs; everything else stays as the
        // user (or scanner) last left it.
        const double      bpmOut  = track.bpm;
        const juce::String keyOut = track.musicalKey;
        const float       lufsOut = track.lufs;
        FoxpFile::load(track);
        track.bpm        = bpmOut;
        track.musicalKey = keyOut;
        track.lufs       = lufsOut;
        FoxpFile::save(track);
    }

    juce::MessageManager::callAsync([this, t = track]() mutable {
        if (onTrackAnalysed) onTrackAnalysed(std::move(t));
    });
}

} // namespace FoxPlayer
