#pragma once

#include "../JuceCore.h"
#include <mutex>
#include <vector>

namespace Stylus
{

// Owns the 6-digit PIN used by the iPhone to pair with the SyncServer
// and the bearer tokens issued on successful pair. Thread-safe -- the
// SyncServer touches PinManager from its worker thread when handling
// incoming requests; the SyncPreferencesPanel reads currentPin() from
// the message thread to render the UI.
//
// Lifecycle:
//   - On construction, a fresh PIN is generated.
//   - regenerate() reshuffles the PIN on demand (UI "Regenerate" button).
//   - validatePin() verifies an incoming PIN. On success, issues a
//     fresh bearer token; on failure, increments a failure counter.
//     After kMaxFailures consecutive failures the manager enters
//     lockout for kLockoutSeconds and rejects any pair attempt.
//   - isValidToken() / revokeAllTokens() drive request authorisation
//     and explicit logout (e.g. when the server toggle flips off).
class SyncPinManager
{
public:
    enum class PairResult
    {
        Ok,
        WrongPin,
        LockedOut
    };

    SyncPinManager();

    // Six-digit PIN as a string. Safe to call from any thread.
    juce::String currentPin() const;

    // Regenerates the PIN (also resets the failure counter +
    // clears any active lockout). Safe to call from any thread.
    void regenerate();

    // Validates a pair attempt. Returns Ok + a new bearer token on
    // success (caller writes the token into the HTTP response).
    // WrongPin counts toward the failure budget; LockedOut means
    // the manager is currently locked and the attempt was not
    // checked against the PIN.
    PairResult validatePin(const juce::String& pin,
                           juce::String&        outToken);

    // True if the given bearer token was issued by a recent
    // successful pair AND has not been revoked.
    bool isValidToken(const juce::String& token) const;

    // Drops every issued token. Called when the SyncServer toggle
    // flips off so a fresh enable forces every paired peer to
    // re-pair.
    void revokeAllTokens();

private:
    static juce::String generatePin();
    static juce::String generateToken();

    mutable std::mutex          mutex_;
    juce::String                pin_;
    std::vector<juce::String>   tokens_;
    int                         failureCount_  { 0 };
    juce::int64                 lockoutUntilMs_{ 0 };

    static constexpr int        kMaxFailures    = 5;
    static constexpr int        kLockoutSeconds = 60;
};

} // namespace Stylus
