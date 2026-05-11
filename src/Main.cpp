#include <JuceHeader.h>
#include "MainComponent.h"
#include "MainWindow.h"
#include "UIConstants.h"
#include "ui/Splash.h"

class StylusApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName()    override { return "Stylus"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed()          override { return false; }

    void initialise(const juce::String& commandLine) override
    {
        // Capture any command-line paths so we can route them once the main
        // window's MainComponent exists. macOS Finder "Open With Stylus" lands
        // here on cold launch; double-clicking a file with an existing
        // association lands in anotherInstanceStarted.
        pendingOpenPaths_ = parseFilePaths(commandLine);

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
            drainPendingOpenPaths();
        });

        juce::Timer::callAfterDelay(2500, [this]() {
            splashWindow_.reset();
        });
       #else
        // No splash on Windows for now; create the main window immediately.
        mainWindow_ = std::make_unique<Stylus::MainWindow>(juce::String());
        drainPendingOpenPaths();
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

    void anotherInstanceStarted(const juce::String& commandLine) override
    {
        // OS routes "Open With Stylus" / double-click-of-associated-file to
        // here when Stylus is already running (single-instance app). The
        // commandLine string is the OS-supplied open-files list, which JUCE
        // wraps in NSApplication openFile:/openFiles: on macOS.
        auto paths = parseFilePaths(commandLine);
        if (paths.isEmpty()) return;
        if (auto* mc = mainComponent())
            mc->handleExternalPaths(paths, /*startPlayback*/ true);
        else
            pendingOpenPaths_.addArray(paths);
    }

private:
    Stylus::MainComponent* mainComponent() const
    {
        return mainWindow_ ? mainWindow_->getMainComponent() : nullptr;
    }

    // Drain any paths the OS handed us before MainComponent existed (cold
    // launch via "Open With Stylus"). Called from initialise once the window
    // is constructed.
    void drainPendingOpenPaths()
    {
        if (pendingOpenPaths_.isEmpty()) return;
        if (auto* mc = mainComponent())
        {
            const auto paths = std::move(pendingOpenPaths_);
            pendingOpenPaths_.clear();
            mc->handleExternalPaths(paths, /*startPlayback*/ true);
        }
    }

    // JUCE hands us a single command-line string. Splits on whitespace while
    // respecting quoted segments (since file paths often contain spaces).
    static juce::StringArray parseFilePaths(const juce::String& commandLine)
    {
        juce::StringArray out;
        if (commandLine.isEmpty()) return out;
        out.addTokens(commandLine, true);  // honour quotes
        out.removeEmptyStrings();
        // Anything that's not an existing path is presumably a flag/option;
        // ignore those rather than letting them reach the library scanner.
        for (int i = out.size(); --i >= 0;)
            if (! juce::File(out[i]).exists())
                out.remove(i);
        return out;
    }

    std::unique_ptr<Stylus::MainWindow>   mainWindow_;
    std::unique_ptr<Stylus::SplashWindow> splashWindow_;
    juce::StringArray                     pendingOpenPaths_;
};

START_JUCE_APPLICATION(StylusApplication)
