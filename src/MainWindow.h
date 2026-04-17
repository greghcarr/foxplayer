#pragma once

#include "MainComponent.h"
#include "Constants.h"
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
    }

    ~MainWindow() override
    {
        juce::MenuBarModel::setMacMainMenu(nullptr);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace FoxPlayer
