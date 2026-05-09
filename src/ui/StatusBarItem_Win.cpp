// Windows implementation of StatusBarItem. Mirrors the macOS NSStatusItem
// surface: an icon in the system tray with a "Show Stylus" / "Quit Stylus"
// context menu. State changes (Stopped / Playing / Paused) update the icon
// so the tray reflects the player at a glance.
//
// Built on Shell_NotifyIcon, which requires a real HWND to deliver tray
// callbacks to. We register a minimal window class and create a
// message-only window (HWND_MESSAGE parent) for that purpose; nothing is
// drawn to it.

#include "StatusBarItem.h"

#include <JuceHeader.h>

#include <windows.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "user32.lib")

namespace Stylus
{

namespace
{
constexpr UINT     kTrayCallbackMsg = WM_APP + 1;
constexpr UINT     kTrayId          = 1;
constexpr wchar_t  kClassName[]     = L"StylusTrayWindow";

// Pulls the first icon out of the running .exe. JUCE's juceaide bakes the
// app icon (resources/app-icon.png) into the binary at build time, and
// ExtractIconEx returns it without us having to know its resource ID.
HICON loadAppIcon()
{
    wchar_t exePath[MAX_PATH] {};
    if (GetModuleFileNameW(nullptr, exePath, MAX_PATH) == 0) return nullptr;
    // Note: don't name the second variable `large` — older Windows headers
    // still #define it as part of legacy 16-bit memory-model attributes,
    // which causes the compiler to choke on `HICON large = nullptr`.
    HICON smallIcon = nullptr, largeIcon = nullptr;
    if (ExtractIconExW(exePath, 0, &largeIcon, &smallIcon, 1) > 0)
    {
        if (largeIcon && largeIcon != (HICON) 1) DestroyIcon(largeIcon);
        if (smallIcon && smallIcon != (HICON) 1) return smallIcon;
        if (largeIcon && largeIcon != (HICON) 1) return largeIcon;
    }
    // Fallback to a generic system icon if extraction failed.
    return LoadIconW(nullptr, (LPCWSTR) IDI_APPLICATION);
}
} // namespace

struct StatusBarItem::Impl
{
    StatusBarItem*       owner    { nullptr };
    HWND                 hwnd     { nullptr };
    HICON                icon     { nullptr };
    NOTIFYICONDATAW      nid      {};
    bool                 added    { false };

    explicit Impl(StatusBarItem* o) : owner(o)
    {
        // RegisterClass is idempotent in practice: a second call with the
        // same name returns 0 and sets ERROR_CLASS_ALREADY_EXISTS, which we
        // treat as success. Class lifetime is process-wide, so we never
        // unregister.
        WNDCLASSW wc {};
        wc.lpfnWndProc   = &Impl::wndProc;
        wc.hInstance     = GetModuleHandleW(nullptr);
        wc.lpszClassName = kClassName;
        RegisterClassW(&wc);

        hwnd = CreateWindowExW(0, kClassName, L"StylusTray",
                               0, 0, 0, 0, 0,
                               HWND_MESSAGE, nullptr, wc.hInstance, this);
        if (! hwnd) return;

        // Stash `this` on the window so the static WndProc can dispatch.
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR) this);

        icon = loadAppIcon();

        nid.cbSize           = sizeof(nid);
        nid.hWnd             = hwnd;
        nid.uID              = kTrayId;
        nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = kTrayCallbackMsg;
        nid.hIcon            = icon;
        wcsncpy_s(nid.szTip, L"Stylus", _TRUNCATE);

        if (Shell_NotifyIconW(NIM_ADD, &nid))
            added = true;
    }

    ~Impl()
    {
        if (added)
        {
            Shell_NotifyIconW(NIM_DELETE, &nid);
            added = false;
        }
        if (hwnd)   { DestroyWindow(hwnd); hwnd = nullptr; }
        if (icon)   { DestroyIcon(icon);   icon = nullptr; }
    }

    void setIcon(HICON newIcon)
    {
        if (! added || ! newIcon) return;
        // Replace the icon handle and re-modify so Explorer redraws the tray.
        nid.uFlags = NIF_ICON;
        nid.hIcon  = newIcon;
        Shell_NotifyIconW(NIM_MODIFY, &nid);
        // The active icon must remain valid until Explorer has consumed it,
        // and there's no good signal for "consumed", so we keep both the new
        // and the previous icon alive until the next swap.
        if (icon && icon != newIcon) DestroyIcon(icon);
        icon = newIcon;
    }

    void showContextMenu(POINT screenPos)
    {
        HMENU menu = CreatePopupMenu();
        if (! menu) return;
        AppendMenuW(menu, MF_STRING,    1, L"Show Stylus");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING,    2, L"Quit Stylus");

        // SetForegroundWindow before TrackPopupMenu is the documented dance
        // that makes the menu auto-dismiss when the user clicks elsewhere.
        SetForegroundWindow(hwnd);
        const int cmd = TrackPopupMenu(menu,
                                       TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY,
                                       screenPos.x, screenPos.y,
                                       0, hwnd, nullptr);
        DestroyMenu(menu);

        // Match the Mac controller: callbacks fire on the JUCE message thread.
        if (cmd == 1)
            juce::MessageManager::callAsync([owner = owner]
                { if (owner && owner->onShowApp) owner->onShowApp(); });
        else if (cmd == 2)
            juce::MessageManager::callAsync([owner = owner]
                { if (owner && owner->onQuit) owner->onQuit(); });
    }

    static LRESULT CALLBACK wndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp)
    {
        if (msg == kTrayCallbackMsg)
        {
            auto* self = (Impl*) GetWindowLongPtrW(h, GWLP_USERDATA);
            if (! self) return 0;

            const UINT event = LOWORD(lp);
            if (event == WM_LBUTTONUP)
            {
                StatusBarItem* owner = self->owner;
                juce::MessageManager::callAsync([owner]
                    { if (owner && owner->onShowApp) owner->onShowApp(); });
            }
            else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU)
            {
                POINT p {};
                GetCursorPos(&p);
                self->showContextMenu(p);
            }
            return 0;
        }
        return DefWindowProcW(h, msg, wp, lp);
    }
};

StatusBarItem::StatusBarItem()
    : impl_(std::make_unique<Impl>(this)) {}

StatusBarItem::~StatusBarItem() = default;

void StatusBarItem::setState(State /*state*/)
{
    // The macOS controller swaps in play / pause / square glyphs here. The
    // initial Windows version keeps the app icon constant; if/when we add
    // state-aware tray icons, render them once at startup and call
    // impl_->setIcon(...) from this method.
}

} // namespace Stylus
