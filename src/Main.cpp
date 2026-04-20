#include <JuceHeader.h>
#include "MainWindow.h"
#include "Constants.h"
#include "ui/Splash.h"

class FoxPlayerApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "FoxPlayer"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        // App-wide scrollbar styling: gray thumb, transparent track/background.
        auto& laf = juce::LookAndFeel::getDefaultLookAndFeel();
        laf.setColour(juce::ScrollBar::thumbColourId,      FoxPlayer::Constants::Color::scrollbarThumb);
        laf.setColour(juce::ScrollBar::trackColourId,      juce::Colours::transparentBlack);
        laf.setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);

        // App-wide typeface: use the native macOS system font (SF Pro) for the
        // most "first-party" look. Every juce::Font() created without an
        // explicit typeface falls through to this default.
        laf.setDefaultSansSerifTypefaceName("Helvetica Neue");

        // Splash: transparent window, white Foxwhelp title with thick black outline.
        splashWindow_ = std::make_unique<FoxPlayer::SplashWindow>();

        // Defer main window creation one message loop iteration so the splash
        // actually gets a paint cycle before MainComponent's constructor runs.
        juce::Timer::callAfterDelay(60, [this]() {
            mainWindow_ = std::make_unique<FoxPlayer::MainWindow>(juce::String());
        });

        juce::Timer::callAfterDelay(2500, [this]() {
            splashWindow_.reset();
        });
    }

    void shutdown() override
    {
        mainWindow_.reset();
    }

    void systemRequestedQuit() override
    {
        if (mainWindow_ != nullptr)
        {
            if (auto* mc = mainWindow_->getMainComponent())
            {
                mc->requestQuit([] { juce::JUCEApplication::getInstance()->quit(); });
                return;
            }
        }
        quit();
    }

    void anotherInstanceStarted(const juce::String& /*commandLine*/) override {}

private:
    std::unique_ptr<FoxPlayer::MainWindow>   mainWindow_;
    std::unique_ptr<FoxPlayer::SplashWindow> splashWindow_;
};

START_JUCE_APPLICATION(FoxPlayerApplication)
