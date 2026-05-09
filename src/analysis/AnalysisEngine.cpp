#include "AnalysisEngine.h"
#include "BpmDetector.h"
#include "KeyDetector.h"
#include "LufsDetector.h"
#include "audio/StylFile.h"

namespace Stylus
{

AnalysisEngine::AnalysisEngine()
    : juce::Thread("Stylus.AnalysisEngine")
{
    formatManager_.registerBasicFormats();
}

AnalysisEngine::~AnalysisEngine()
{
    cancelAll();
}

void AnalysisEngine::enqueue(const TrackInfo& track)
{
    // Skip if the sidecar already has BPM, key, AND loudness - all three
    // outputs of a Full analysis. A track missing only one of them still
    // gets requeued; the per-step gates inside analyseOne will skip the
    // already-filled measurements.
    if (track.bpm > 0.0 && track.musicalKey.isNotEmpty() && track.lufs != 0.0f)
        return;

    {
        juce::ScopedLock sl(queueLock_);
        queue_.push_back({ track, Mode::Full });
    }

    if (onTrackQueued) onTrackQueued(track);

    if (!isThreadRunning())
        startThread(juce::Thread::Priority::background);
}

void AnalysisEngine::enqueueAll(const std::vector<TrackInfo>& tracks)
{
    std::vector<TrackInfo> added;
    {
        juce::ScopedLock sl(queueLock_);
        for (const auto& t : tracks)
        {
            if (t.bpm > 0.0 && t.musicalKey.isNotEmpty() && t.lufs != 0.0f)
                continue;
            queue_.push_back({ t, Mode::Full });
            added.push_back(t);
        }
    }

    if (onTrackQueued)
        for (auto& t : added)
            onTrackQueued(t);

    if (!queue_.empty() && !isThreadRunning())
        startThread(juce::Thread::Priority::background);
}

void AnalysisEngine::enqueueLufsOnly(const TrackInfo& track)
{
    // No-op when LUFS is already measured.
    if (track.lufs != 0.0f) return;

    {
        juce::ScopedLock sl(queueLock_);
        queue_.push_back({ track, Mode::LufsOnly });
    }

    // The lazy-on-first-play hook is silent in the Analysis Log: not firing
    // onTrackQueued / onTrackStarted keeps it out of the log so a casual
    // play-through doesn't spam the developer-facing window. Only the
    // explicit user-driven enqueue / enqueueAll paths announce themselves.

    if (!isThreadRunning())
        startThread(juce::Thread::Priority::background);
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
        QueueEntry entry;
        {
            juce::ScopedLock sl(queueLock_);
            if (queue_.empty()) break;
            entry = queue_.front();
            queue_.pop_front();
        }

        analyseOne(entry.track, entry.mode);
    }
}

void AnalysisEngine::analyseOne(TrackInfo track, Mode mode)
{
    DBG("AnalysisEngine - analysing: " + track.file.getFileName()
        + (mode == Mode::LufsOnly ? " (lufs-only)" : ""));

    // Only the user-driven Full path announces itself in the log. The
    // lazy LufsOnly path runs silently.
    if (mode == Mode::Full)
    {
        juce::MessageManager::callAsync([this, t = track]() mutable {
            if (onTrackStarted) onTrackStarted(std::move(t));
        });
    }

    bool changed = false;

    if (mode == Mode::Full && track.bpm <= 0.0)
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

    if (mode == Mode::Full && track.musicalKey.isEmpty())
    {
        const juce::String key = KeyDetector::detect(track.file, formatManager_, {});
        if (key.isNotEmpty())
        {
            track.musicalKey = key;
            changed = true;
            DBG("AnalysisEngine - Key: " + key);
        }
    }

    if (threadShouldExit()) return;

    if (track.lufs == 0.0f)
    {
        const float lufs = LufsDetector::detect(track.file, formatManager_,
                                                [this] { return threadShouldExit(); });
        if (lufs != 0.0f)
        {
            track.lufs = lufs;
            changed = true;
            DBG("AnalysisEngine - LUFS: " + juce::String(lufs, 2));
        }
    }

    if (changed)
    {
        // Refresh non-analysis fields from the on-disk .styl before saving so
        // we don't clobber user edits made while analysis was running.
        // Analysis owns bpm / musicalKey / lufs; everything else stays as the
        // user (or scanner) last left it.
        const double      bpmOut  = track.bpm;
        const juce::String keyOut = track.musicalKey;
        const float       lufsOut = track.lufs;
        StylFile::load(track);
        track.bpm        = bpmOut;
        track.musicalKey = keyOut;
        track.lufs       = lufsOut;
        StylFile::save(track);
    }

    juce::MessageManager::callAsync([this, t = track]() mutable {
        if (onTrackAnalysed) onTrackAnalysed(std::move(t));
    });
}

} // namespace Stylus
