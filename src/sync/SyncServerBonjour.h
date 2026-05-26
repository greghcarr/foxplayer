#pragma once

#include "../JuceCore.h"

namespace Stylus
{

// Thin Objective-C++ wrapper around NSNetService. Publishes a
// _stylus-sync._tcp Bonjour record so the iOS SyncBonjourBrowser
// (NWBrowser) can find this Mac on the local network.
//
// Kept in its own translation unit (a .mm file) so the JUCE / C++
// world doesn't drag NSNetService into every consumer. The header
// stays pure C++.
class SyncServerBonjour
{
public:
    SyncServerBonjour();
    ~SyncServerBonjour();

    // Start advertising on the given port with the given service
    // name (typically the computer's user-visible hostname).
    // Replaces any previous publication. Returns false if the
    // underlying NSNetService refused to publish.
    bool publish(const juce::String& serviceName, int port);
    void stop();

private:
    void* impl_ = nullptr;   // erased NSNetService + delegate
};

} // namespace Stylus
