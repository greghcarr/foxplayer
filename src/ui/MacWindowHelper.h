#pragma once

#include <functional>

// Shows a native NSWindow and activates the app using the current macOS API.
// Pulls the window onto the current Space (used when re-opening a hidden window).
void FoxPlayer_activateAndShowWindow(void* nsWindowHandle);

// Activates the app and brings the window to front on whichever Space it
// currently lives on, causing macOS to switch to that Space rather than moving
// the window. Used when the window is already visible and the user clicks the
// Dock icon — we want to go to the window, not bring the window to us.
void FoxPlayer_activateExistingWindow(void* nsWindowHandle);

// Registers a callback fired when the user clicks the Dock icon while no
// windows are visible (NSApplicationDidBecomeActiveNotification with no
// visible windows). Pass nullptr to unregister.
void FoxPlayer_setDockReopenCallback(std::function<void()> callback);
