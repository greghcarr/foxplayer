#pragma once

#include "MainComponent.h"
#include "UIConstants.h"
#include "ui/MacWindowHelper.h"
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
        // Hide rather than quit. firstCommandTarget is permanently set to the
        // MainComponent in its constructor, so menus stay routable here too.
        setVisible(false);
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
