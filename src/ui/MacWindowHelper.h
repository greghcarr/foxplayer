#pragma once

#include <functional>

// Shows a native NSWindow and activates the app using the current macOS API.
void FoxPlayer_activateAndShowWindow(void* nsWindowHandle);

// Registers a callback fired when the user clicks the Dock icon while no
// windows are visible (NSApplicationDidBecomeActiveNotification with no
// visible windows). Pass nullptr to unregister.
void FoxPlayer_setDockReopenCallback(std::function<void()> callback);
