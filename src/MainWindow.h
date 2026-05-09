#pragma once

#include "MainComponent.h"
#include "UIConstants.h"
#include "ui/MacWindowHelper.h"
#include "ui/PlatformChrome.h"
#include <JuceHeader.h>

namespace Stylus
{

class MainWindow : public juce::DocumentWindow
{
public:
    explicit MainWindow(const juce::String& name)
        : DocumentWindow(name,
                         juce::Desktop::getInstance().getDefaultLookAndFeel()
                             .findColour(juce::ResizableWindow::backgroundColourId),
                         DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar(true);
        mainComponent_ = std::make_unique<MainComponent>();
        setContentOwned(mainComponent_.get(), true);

#if JUCE_IOS || JUCE_ANDROID
        setFullScreen(true);
#else
        setResizable(true, true);
        // Add the menu-bar reserve to the minimum height so the transport
        // bar still gets its full 108 px in mini-player mode (the bar's
        // internal layout assumes that height; squeezing it cuts off the
        // top and bottom and offsets the spinning disc). menuBarHeight is
        // 0 on macOS, so this collapses to the original minimum there.
        //
        // The extra +16 on top of menuBarHeight is breathing room: with
        // just the bar's height added, the buffer above the transport bar
        // shrinks to whatever the minWindowHeight constant carved out
        // (currently 27 px), and visually the transport bar feels glued
        // to the menu strip. The extra 16 puts the disc and buttons in a
        // more natural position when the user shrinks all the way down.
        constexpr int extraMiniBufferPx = (UIConstants::menuBarHeight > 0) ? 16 : 0;
        setResizeLimits(UIConstants::minWindowWidth,
                        UIConstants::minWindowHeight
                            + UIConstants::menuBarHeight
                            + extraMiniBufferPx,
                        4000, 3000);
        centreWithSize(getWidth(), getHeight());
#endif
        setVisible(true);

       #if JUCE_MAC
        // macOS app menu extras: Preferences sits at the top of the Stylus
        // menu, just above the automatic "Services" item and its divider.
        juce::PopupMenu appleMenuExtras;
        appleMenuExtras.addCommandItem(&mainComponent_->commandManager(),
                                        MainComponent::cmdPreferences,
                                        "Preferences...");

        // Wire up the macOS native menu bar.
        juce::MenuBarModel::setMacMainMenu(mainComponent_.get(),
                                           &appleMenuExtras,
                                           "Hide Stylus");
       #endif

        // Win11 dark title bar (no-op on macOS; see PlatformChrome.h).
        // setVisible(true) above has already created the peer, so this
        // takes effect immediately on the first paint.
        applyDarkTitleBar(*this);

       #if JUCE_WINDOWS
        // Windows withholds foreground rights from newly-launched
        // processes that didn't get them via a user click — launching
        // from PowerShell, Explorer's "Run", IDE debug, etc. all hit
        // this. Without explicit handling, the window appears only in
        // the taskbar and waits for the user to click the icon. Pair
        // toFront with the standard AttachThreadInput trick so
        // SetForegroundWindow is actually allowed through.
        bringWindowToForegroundOnLaunch();
       #endif

        // Both the Window-menu command and the Dock icon click call showWindow().
        mainComponent_->onShowWindowRequested = [this]() { showWindow(); };

        // macOS only: clicking the Dock icon while all windows are hidden
        // fires NSApplicationDidBecomeActiveNotification. Re-show the window.
        // The Windows stub no-ops, so this is a safe call on every platform.
        Stylus_setDockReopenCallback([this]() { showWindow(); });
    }

    ~MainWindow() override
    {
        Stylus_setDockReopenCallback(nullptr);
       #if JUCE_MAC
        juce::MenuBarModel::setMacMainMenu(nullptr);
       #endif
    }

    void closeButtonPressed() override
    {
       #if JUCE_MAC
        // macOS convention: closing the window leaves the app running so
        // the Dock icon stays available for re-opening, and music keeps
        // playing in the background. firstCommandTarget is permanently set
        // to MainComponent in its constructor, so menus stay routable here.
        setVisible(false);
       #else
        // Windows / Linux convention: closing the main window quits the
        // app. Route through systemRequestedQuit so the same confirmation
        // flow as File > Quit and Alt-F4 runs (preference for "Ask before
        // quitting", chance to flush state, etc.).
        if (auto* app = juce::JUCEApplication::getInstance())
            app->systemRequestedQuit();
       #endif
    }

    // "Stylus" stays in the title bar at every window size for now.
    void resized() override
    {
        juce::DocumentWindow::resized();
        if (getName() != "Stylus")
            setName("Stylus");
    }

    MainComponent* getMainComponent() const { return mainComponent_.get(); }

private:
    std::unique_ptr<MainComponent> mainComponent_;

   #if JUCE_WINDOWS
    void bringWindowToForegroundOnLaunch()
    {
        auto* peer = getPeer();
        if (peer == nullptr) return;
        auto* hwnd = (HWND) peer->getNativeHandle();
        if (hwnd == nullptr) return;

        // The AttachThreadInput dance: temporarily pretend our thread
        // shares the input queue of whoever currently holds focus, so
        // Windows allows SetForegroundWindow to actually transfer
        // foreground rights. Without it, anti-focus-stealing logic
        // silently demotes our request to a taskbar flash.
        const DWORD foregroundThreadId =
            GetWindowThreadProcessId(GetForegroundWindow(), nullptr);
        const DWORD currentThreadId = GetCurrentThreadId();
        const bool attached = (foregroundThreadId != 0
                            && foregroundThreadId != currentThreadId
                            && AttachThreadInput(foregroundThreadId,
                                                 currentThreadId, TRUE) != 0);

        ShowWindow(hwnd, SW_SHOW);
        BringWindowToTop(hwnd);
        SetForegroundWindow(hwnd);
        SetActiveWindow(hwnd);

        if (attached)
            AttachThreadInput(foregroundThreadId, currentThreadId, FALSE);

        // toFront also issues a Z-order bump and sets keyboard focus on
        // the JUCE side; harmless after the Win32 calls above.
        toFront(true);
        grabKeyboardFocus();
    }
   #endif

    void showWindow()
    {
        const bool wasVisible = isVisible();
        if (! wasVisible)
        {
            // Window had been closed (hidden). Re-centre on the display under
            // the mouse so it re-appears where the user is working.
            auto& displays = juce::Desktop::getInstance().getDisplays();
            auto mousePos  = juce::Desktop::getMousePosition();
            auto* display  = displays.getDisplayForPoint(mousePos, false);
            if (display == nullptr) display = displays.getPrimaryDisplay();
            if (display != nullptr)
            {
                auto area = display->userArea;
                setBounds(area.getCentreX() - getWidth()  / 2,
                          area.getCentreY() - getHeight() / 2,
                          getWidth(), getHeight());
            }
            setVisible(true);
        }

        // Native activation. When the window was already visible on another
        // Space, Stylus_activateExistingWindow leaves it in place so macOS
        // switches Spaces to it instead of dragging it to the current Space.
        // The Windows stub no-ops; toFront() below handles activation there.
        if (auto* peer = getPeer())
        {
            if (wasVisible)
                Stylus_activateExistingWindow(peer->getNativeHandle());
            else
                Stylus_activateAndShowWindow(peer->getNativeHandle());
        }

       #if ! JUCE_MAC
        // On Windows the macOS helpers above are no-ops; this is what actually
        // brings the window to the foreground and gives it focus.
        toFront(true);
       #endif
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace Stylus
