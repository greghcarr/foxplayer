#include <JuceHeader.h>
#include "MainWindow.h"
#include "UIConstants.h"
#include "ui/Splash.h"

class StylusApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Stylus"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String& /*commandLine*/) override
    {
        // App-wide scrollbar styling: gray thumb, transparent track/background.
        auto& laf = juce::LookAndFeel::getDefaultLookAndFeel();
        laf.setColour(juce::ScrollBar::thumbColourId,      Stylus::UIConstants::Color::scrollbarThumb);
        laf.setColour(juce::ScrollBar::trackColourId,      juce::Colours::transparentBlack);
        laf.setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);

        // App-wide typeface: leave the default sans-serif unset so JUCE
        // falls through to the platform system font. On macOS 10.11+ that's
        // SF Pro, which is what we want for a "first-party" look. The
        // previous Helvetica Neue override was a holdover from the pre-SF
        // era; modern macOS still ships Helvetica Neue but it's no longer
        // the system font.

       #if JUCE_MAC
        // Splash: transparent borderless window showing the embedded app icon.
        // macOS-only for now: JUCE's transparent-layered-window path on
        // Windows mishandles opaque image content (only the JUCE-drawn
        // drop-shadow ring renders), and the natural fix (an opaque splash)
        // ran into the borderless-DocumentWindow combo not appearing at
        // all. Cold launch on Windows is fast enough that skipping the
        // splash is fine while we sort that out.
        splashWindow_ = std::make_unique<Stylus::SplashWindow>();

        // Defer main window creation one message loop iteration so the splash
        // actually gets a paint cycle before MainComponent's constructor runs.
        juce::Timer::callAfterDelay(60, [this]() {
            mainWindow_ = std::make_unique<Stylus::MainWindow>(juce::String());
        });

        juce::Timer::callAfterDelay(2500, [this]() {
            splashWindow_.reset();
        });
       #else
        // No splash on Windows for now; create the main window immediately.
        mainWindow_ = std::make_unique<Stylus::MainWindow>(juce::String());
       #endif
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
    std::unique_ptr<Stylus::MainWindow>   mainWindow_;
    std::unique_ptr<Stylus::SplashWindow> splashWindow_;
};

START_JUCE_APPLICATION(StylusApplication)
