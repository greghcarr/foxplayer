#pragma once

#include <functional>
#include <string>

namespace FoxPlayer
{

// Registers with macOS MPRemoteCommandCenter so that the system media keys
// (Fn+F7 previous, Fn+F8 play/pause, Fn+F9 next) control FoxPlayer.
// Also keeps MPNowPlayingInfoCenter updated so Control Center shows the
// correct track and position.
class NowPlayingBridge
{
public:
    NowPlayingBridge();
    ~NowPlayingBridge();

    // Call when a new track starts or metadata changes.
    void setTrackInfo(const std::string& title,
                      const std::string& artist,
                      double             durationSeconds);

    // Call on state changes and from the timer tick (for seek bar accuracy).
    void setPlaybackState(bool isPlaying, double posSeconds);

    // Call when playback stops with no track loaded.
    void clearNowPlaying();

    // Callbacks — all fire on the main thread.
    std::function<void()> onPlayPause;
    std::function<void()> onPrevious;
    std::function<void()> onNext;

private:
    void* impl_;

    NowPlayingBridge(const NowPlayingBridge&)            = delete;
    NowPlayingBridge& operator=(const NowPlayingBridge&) = delete;
};

} // namespace FoxPlayer
