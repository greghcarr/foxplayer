#include "SyncServerBonjour.h"

// Windows stub. Apple's iOS Bonjour discovery on Windows would
// require the user to install "Bonjour for Windows" + use the dns_sd
// API. v1 ships the Mac-side advertisement only; Windows desktops
// run the SyncServer but the iPhone has to find them by manual IP +
// port (out of scope for v1). The class is kept as a no-op so the
// SyncPreferencesPanel toggle behaves the same on both platforms --
// the server starts, just without an mDNS record.

namespace Stylus
{

SyncServerBonjour::SyncServerBonjour() = default;

SyncServerBonjour::~SyncServerBonjour()
{
    stop();
}

bool SyncServerBonjour::publish(const juce::String&, int)
{
    return false;
}

void SyncServerBonjour::stop()
{
    impl_ = nullptr;
}

} // namespace Stylus
