#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>
#include <vector>

namespace FoxPlayer
{

// ---- Audio panel -------------------------------------------------------------
// Output-device dropdown backed by AudioDeviceManager. First of many future
// audio settings (sample rate, buffer size, etc.).
class AudioPreferencesPanel : public juce::Component,
                               private juce::ChangeListener,
                               private juce::Timer
{
public:
    explicit AudioPreferencesPanel(juce::AudioDeviceManager& deviceManager);
    ~AudioPreferencesPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    void rebuildDeviceList();
    void applySelectedDevice();
    juce::String currentDefaultDeviceName() const;

    // juce::ChangeListener (deviceManager broadcasts when devices appear/disappear).
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // juce::Timer - polls for OS-default changes that may not broadcast through
    // AudioDeviceManager; updates the label and follows the new default if the
    // user has "System default" selected.
    void timerCallback() override;

    juce::AudioDeviceManager& deviceManager_;
    bool                      usingDefault_     { false };
    juce::String              lastDefaultName_;

    juce::Label    deviceLabel_;
    juce::ComboBox deviceCombo_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioPreferencesPanel)
};

// ---- Library panel -----------------------------------------------------------
// Scrollable list of music-library root folders plus Add/Remove buttons. All
// changes fire through onFoldersChanged; MainComponent pushes the live folder
// list back via setFolders (which the panel mirrors in its display).
class LibraryPreferencesPanel : public juce::Component,
                                 public juce::ListBoxModel
{
public:
    LibraryPreferencesPanel();

    // Fires after the user adds or removes a folder.
    std::function<void(std::vector<juce::File>)> onFoldersChanged;

    void setFolders(std::vector<juce::File> folders);
    const std::vector<juce::File>& folders() const { return folders_; }

    void paint(juce::Graphics& g) override;
    void resized() override;

    int  getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected) override;

private:
    void addFolder();
    void removeSelectedFolders();

    juce::Label        heading_;
    juce::ListBox      list_;
    juce::TextButton   addButton_    { "Add Folder" };
    juce::TextButton   removeButton_ { "Remove Folder" };

    std::vector<juce::File> folders_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LibraryPreferencesPanel)
};

// ---- Sidebar + panel host ----------------------------------------------------
// Left-hand category list + right-hand content area. Add a new category by
// extending the Category enum and wiring a new panel in PreferencesComponent.
class PreferencesComponent : public juce::Component
{
public:
    explicit PreferencesComponent(juce::AudioDeviceManager& deviceManager);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

private:
    enum class Category { Audio, Library };

    struct SidebarItem
    {
        Category              category;
        juce::String          label;
        juce::Rectangle<int>  bounds;
    };

    void showPanel(Category c);
    void layoutSidebar();

    juce::AudioDeviceManager&              deviceManager_;
    std::vector<SidebarItem>               items_;
    Category                               current_ { Category::Audio };

    std::unique_ptr<AudioPreferencesPanel>   audioPanel_;
    std::unique_ptr<LibraryPreferencesPanel> libraryPanel_;

public:
    LibraryPreferencesPanel& libraryPanel() { return *libraryPanel_; }

private:

    static constexpr int sidebarWidth = 160;
    static constexpr int itemHeight   = 34;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesComponent)
};

// ---- Window wrapper ----------------------------------------------------------
// Closing the window hides it, so the user's scroll/category state is
// preserved next time Preferences is opened.
class PreferencesWindow : public juce::DocumentWindow
{
public:
    explicit PreferencesWindow(juce::AudioDeviceManager& deviceManager);

    void closeButtonPressed() override;

    // Direct access to the library panel so MainComponent can wire up the
    // add/remove callback and push the current folder list into the view.
    LibraryPreferencesPanel* libraryPanel();

    // Fires when the user closes the preferences window (so MainComponent
    // can refresh menu item state that depends on visibility).
    std::function<void()> onClosed;

private:
    PreferencesComponent* prefsComponent_ { nullptr };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PreferencesWindow)
};

} // namespace FoxPlayer
