// Windows stub for NowPlayingBridge. Once SMTC (Windows.Media
// SystemMediaTransportControls) is wired up, this is where the C++/WinRT
// glue will live. For now everything is a no-op so the rest of the app can
// build and run on Windows; transport controls remain reachable through
// on-screen buttons and standard hotkeys (F8 etc. via JUCE accelerators if
// configured) but lock-screen / overlay metadata is not published.

#include "NowPlayingBridge.h"

namespace Stylus
{

NowPlayingBridge::NowPlayingBridge()  : impl_(nullptr) {}
NowPlayingBridge::~NowPlayingBridge() = default;

void NowPlayingBridge::setTrackInfo(const std::string& /*title*/,
                                    const std::string& /*artist*/,
                                    double             /*durationSeconds*/) {}

void NowPlayingBridge::setPlaybackState(bool /*isPlaying*/, double /*posSeconds*/) {}

void NowPlayingBridge::clearNowPlaying() {}

} // namespace Stylus
