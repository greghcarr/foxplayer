#pragma once

#include "MainComponent.h"
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

    MainComponent* getMainComponent() const { return mainComponent_.get(); }

private:
    std::unique_ptr<MainComponent> mainComponent_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

} // namespace FoxPlayer
