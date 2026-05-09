#pragma once

#include "MainComponent.h"
#include "UIConstants.h"
#include "ui/MacWindowHelper.h"
#include <JuceHeader.h>

#if JUCE_WINDOWS
 #include <windows.h>
 #include <dwmapi.h>
 #pragma comment(lib, "dwmapi.lib")
#endif

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
        setResizeLimits(UIConstants::minWindowWidth, UIConstants::minWindowHeight,
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

       #if JUCE_WINDOWS
        // Switch the OS title bar to its Win11 dark variant so it stops
        // clashing with the dark JUCE-rendered window body. setVisible(true)
        // above already created the peer, so the HWND is available.
        if (auto* peer = getPeer())
        {
            if (auto* hwnd = (HWND) peer->getNativeHandle())
            {
                BOOL dark = TRUE;
                // DWMWA_USE_IMMERSIVE_DARK_MODE = 20 on Win10 19041+ / Win11.
                DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark));
            }
        }
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
