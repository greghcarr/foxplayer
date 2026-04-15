#include <JuceHeader.h>
#include "MainWindow.h"

class FoxPlayerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "FoxPlayer"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        mainWindow_ = std::make_unique<FoxPlayer::MainWindow>(getApplicationName());
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override {}

private:
    std::unique_ptr<FoxPlayer::MainWindow> mainWindow_;
};

START_JUCE_APPLICATION(FoxPlayerApplication)
