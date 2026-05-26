#include "SyncPinManager.h"

namespace Stylus
{

SyncPinManager::SyncPinManager()
    : pin_(generatePin())
{
}

juce::String SyncPinManager::currentPin() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return pin_;
}

void SyncPinManager::regenerate()
{
    std::lock_guard<std::mutex> lock(mutex_);
    pin_             = generatePin();
    failureCount_    = 0;
    lockoutUntilMs_  = 0;
}

SyncPinManager::PairResult SyncPinManager::validatePin(
    const juce::String& pin,
    juce::String&        outToken)
{
    std::lock_guard<std::mutex> lock(mutex_);

    const auto now = juce::Time::currentTimeMillis();
    if (now < lockoutUntilMs_)
        return PairResult::LockedOut;

    if (pin != pin_)
    {
        ++failureCount_;
        if (failureCount_ >= kMaxFailures)
        {
            // Brute-force defence: hold off for kLockoutSeconds AND
            // rotate the PIN so the previously-guessed prefix space
            // is invalidated when the lockout expires.
            lockoutUntilMs_ = now + (kLockoutSeconds * 1000);
            pin_            = generatePin();
            failureCount_   = 0;
        }
        return PairResult::WrongPin;
    }

    // Success: clear failure budget, issue a new token.
    failureCount_   = 0;
    lockoutUntilMs_ = 0;
    outToken        = generateToken();
    tokens_.push_back(outToken);
    return PairResult::Ok;
}

bool SyncPinManager::isValidToken(const juce::String& token) const
{
    if (token.isEmpty()) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& t : tokens_)
        if (t == token) return true;
    return false;
}

void SyncPinManager::revokeAllTokens()
{
    std::lock_guard<std::mutex> lock(mutex_);
    tokens_.clear();
}

juce::String SyncPinManager::generatePin()
{
    // Six digits, leading zeros preserved. juce::Random seeds from
    // the system clock + a per-call mix-in, fine for non-crypto use
    // -- the protocol itself is local-network-only and the PIN is
    // short-lived.
    auto& rng = juce::Random::getSystemRandom();
    const int n = rng.nextInt(1000000);  // 0..999999
    return juce::String::formatted("%06d", n);
}

juce::String SyncPinManager::generateToken()
{
    // 16-byte random UUID-style token, hex-encoded -- plenty of
    // entropy to survive an attacker on the LAN attempting to
    // guess one in a single Mac-process lifetime.
    auto& rng = juce::Random::getSystemRandom();
    juce::String s;
    for (int i = 0; i < 16; ++i)
        s += juce::String::toHexString(rng.nextInt(256)).paddedLeft('0', 2);
    return s;
}

} // namespace Stylus
