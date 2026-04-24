#pragma once

#include "MainComponent.h"
#include "Constants.h"
#include "ui/MacWindowHelper.h"
#include <JuceHeader.h>

namespace FoxPlayer
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
        setResizeLimits(Constants::minWindowWidth, Constants::minWindowHeight,
                        4000, 3000);
        centreWithSize(getWidth(), getHeight());
#endif
        setVisible(true);

        // macOS app menu extras: Preferences sits at the top of the FoxPlayer
        // menu, just above the automatic "Services" item and its divider.
        juce::PopupMenu appleMenuExtras;
        appleMenuExtras.addCommandItem(&mainComponent_->commandManager(),
                                        MainComponent::cmdPreferences,
                                        "Preferences...");

        // Wire up the macOS native menu bar.
        juce::MenuBarModel::setMacMainMenu(mainComponent_.get(),
                                           &appleMenuExtras,
                                           "Hide FoxPlayer");

        // Both the Window-menu command and the Dock icon click call showWindow().
        mainComponent_->onShowWindowRequested = [this]() { showWindow(); };

        // Clicking the Dock icon while all windows are hidden fires
        // NSApplicationDidBecomeActiveNotification. Re-show the window.
        FoxPlayer_setDockReopenCallback([this]() { showWindow(); });
    }

    ~MainWindow() override
    {
        FoxPlayer_setDockReopenCallback(nullptr);
        juce::MenuBarModel::setMacMainMenu(nullptr);
    }

    void closeButtonPressed() override
    {
        // Hide rather than quit. Set firstCommandTarget so the Window menu
        // stays routable even with no focused component.
        mainComponent_->commandManager().setFirstCommandTarget(mainComponent_.get());
        setVisible(false);
    }

    // "FoxPlayer" stays in the title bar at every window size for now.
    void resized() override
    {
        juce::DocumentWindow::resized();
        if (getName() != "FoxPlayer")
            setName("FoxPlayer");
    }

    MainComponent* getMainComponent() const { return mainComponent_.get(); }

private:
    std::unique_ptr<MainComponent> mainComponent_;

    void showWindow()
    {
        // Restore normal command routing now that the window is visible.
        mainComponent_->commandManager().setFirstCommandTarget(nullptr);

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
        // Space, FoxPlayer_activateExistingWindow leaves it in place so macOS
        // switches Spaces to it instead of dragging it to the current Space.
        if (auto* peer = getPeer())
        {
            if (wasVisible)
                FoxPlayer_activateExistingWindow(peer->getNativeHandle());
            else
                FoxPlayer_activateAndShowWindow(peer->getNativeHandle());
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace FoxPlayer
