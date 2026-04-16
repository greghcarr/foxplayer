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

        // Wire up the macOS native menu bar.
        juce::MenuBarModel::setMacMainMenu(mainComponent_.get(),
                                           nullptr,
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

    // Title bar is blank at normal sizes (the app already shows "FoxPlayer" in
    // the Dock + app menu), but when the window shrinks to its compact "mini
    // player" dimensions the library disappears, so we put the name back into
    // the title bar to keep the window identifiable.
    void resized() override
    {
        juce::DocumentWindow::resized();

        const bool compact = getWidth()  < Constants::miniModeWidth
                          || getHeight() < Constants::compactHeight;
        const juce::String want = compact ? "FoxPlayer" : juce::String();
        if (getName() != want)
            setName(want);
    }

    MainComponent* getMainComponent() const { return mainComponent_.get(); }

private:
    std::unique_ptr<MainComponent> mainComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace FoxPlayer
