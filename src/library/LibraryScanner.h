#pragma once

#include "audio/TrackInfo.h"
#include <JuceHeader.h>
#include <functional>
#include <vector>

namespace FoxPlayer
{

// Scans a folder recursively on a background thread, emitting batches of TrackInfo
// to the message thread via the onBatchReady and onScanComplete callbacks.
class LibraryScanner : private juce::Thread
{
public:
    LibraryScanner();
    ~LibraryScanner() override;

    void scanFolder(const juce::File& folder);
    void cancelScan();
    bool isScanning() const;

    // Called on the message thread with each completed batch of tracks.
    std::function<void(std::vector<TrackInfo>)> onBatchReady;

    // Called on the message thread when the entire scan is done.
    std::function<void(int totalTracksFound)> onScanComplete;

private:
    void run() override;

    TrackInfo buildTrackInfo(const juce::File& file,
                             juce::AudioFormatManager& fmgr) const;

    juce::File scanRoot_;
    juce::CriticalSection lock_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryScanner)
};

} // namespace FoxPlayer
