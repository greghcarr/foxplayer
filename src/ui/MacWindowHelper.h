#pragma once

#include <functional>

// Shows a native NSWindow and activates the app using the current macOS API.
// Pulls the window onto the current Space (used when re-opening a hidden window).
void Stylus_activateAndShowWindow(void* nsWindowHandle);

// Activates the app and brings the window to front on whichever Space it
// currently lives on, causing macOS to switch to that Space rather than moving
// the window. Used when the window is already visible and the user clicks the
// Dock icon, we want to go to the window, not bring the window to us.
void Stylus_activateExistingWindow(void* nsWindowHandle);

// Registers a callback fired when the user clicks the Dock icon while no
// windows are visible (NSApplicationDidBecomeActiveNotification with no
// visible windows). Pass nullptr to unregister.
void Stylus_setDockReopenCallback(std::function<void()> callback);

// Registers a callback fired on every NSApplicationDidBecomeActiveNotification,
// regardless of which Stylus window has key status. Used to surface dialog
// windows that may have ended up behind the main window after the app
// reactivates. Pass nullptr to unregister.
void Stylus_setAppActivatedCallback(std::function<void()> callback);

// Installs / removes a low-level NSEvent monitor for Option+Tab and
// Option+Shift+Tab. Cocoa's IME routes these through insertText: rather than
// keyDown:, which means JUCE-side KeyListeners and InputFilters can't see
// them reliably (the modifier flags can also be stale by the time the IME's
// callback fires). A local NSEvent monitor sees the keystroke before any
// dispatch and can return nil to swallow it entirely. shift is true when
// Option+Shift+Tab fires, false for plain Option+Tab.
// Call with non-null callback when an Edit Info dialog opens; pass nullptr
// when it closes so we don't leak the monitor.
void Stylus_setOptionTabMonitor(std::function<void(bool shift)> callback);
