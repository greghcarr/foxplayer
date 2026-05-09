#pragma once

// Tiny helper for OS-managed window chrome that has to be applied per top-
// level window (rather than once on the application). Currently just covers
// Windows 11's immersive dark title-bar attribute; macOS title bars follow
// the system theme automatically so the helper no-ops there.
//
// Header-only: the function is small enough that the overhead of inlining
// and pulling in windows.h / dwmapi.h is preferable to a separate compilation
// unit, and this lets every window that wants the dark title bar include
// just this one file.

#include <JuceHeader.h>

#ifdef _WIN32
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment(lib, "dwmapi.lib")
#endif

namespace Stylus
{

// Switches a top-level JUCE window's HWND to Win11's dark immersive title
// bar so it matches the dark JUCE-rendered window body instead of clashing
// as a bright stripe. Safe to call before the window has a peer (no-op
// until the peer exists), safe to call multiple times. No-op on macOS.
//
// For windows that may be hidden then re-shown, the right place to call
// this is from visibilityChanged() so the attribute is reapplied if the
// peer was rebuilt; for one-shot dialogs created and shown immediately,
// calling once after setVisible(true) is enough.
inline void applyDarkTitleBar(juce::Component& topLevelWindow)
{
   #ifdef _WIN32
    if (auto* peer = topLevelWindow.getPeer())
        if (auto* hwnd = (HWND) peer->getNativeHandle())
        {
            BOOL dark = TRUE;
            // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 on Win10 19041+ / Win11.
            // Hard-coded so we don't depend on the SDK version's headers.
            DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
        }
   #else
    juce::ignoreUnused(topLevelWindow);
   #endif
}

} // namespace Stylus
