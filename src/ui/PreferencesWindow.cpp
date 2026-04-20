#include "PreferencesWindow.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

// ============================================================================
// AudioPreferencesPanel
// ============================================================================

static constexpr int kBufferSizes[] = { 32, 64, 128, 256, 512, 1024, 2048 };
static constexpr int kDefaultBufferSize = 512;
static constexpr const char* kBufferSizeKey = "audio.bufferSize";

AudioPreferencesPanel::AudioPreferencesPanel(juce::AudioDeviceManager& dm,
                                             juce::ApplicationProperties& appProps)
    : deviceManager_(dm), appProps_(appProps)
{
    deviceLabel_.setText("Output Device", juce::dontSendNotification);
    deviceLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    deviceLabel_.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    addAndMakeVisible(deviceLabel_);

    deviceCombo_.setColour(juce::ComboBox::backgroundColourId, Color::headerBackground);
    deviceCombo_.setColour(juce::ComboBox::textColourId,       Color::textPrimary);
    deviceCombo_.setColour(juce::ComboBox::outlineColourId,    Color::border);
    deviceCombo_.setColour(juce::ComboBox::arrowColourId,      Color::textSecondary);
    deviceCombo_.onChange = [this] { applySelectedDevice(); };
    addAndMakeVisible(deviceCombo_);

    bufferLabel_.setText("Buffer Size", juce::dontSendNotification);
    bufferLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    bufferLabel_.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
    addAndMakeVisible(bufferLabel_);

    bufferCombo_.setColour(juce::ComboBox::backgroundColourId, Color::headerBackground);
    bufferCombo_.setColour(juce::ComboBox::textColourId,       Color::textPrimary);
    bufferCombo_.setColour(juce::ComboBox::outlineColourId,    Color::border);
    bufferCombo_.setColour(juce::ComboBox::arrowColourId,      Color::textSecondary);
    bufferCombo_.onChange = [this] { applySelectedBufferSize(); };
    addAndMakeVisible(bufferCombo_);

    rebuildDeviceList();
    rebuildBufferList();
    lastDefaultName_ = currentDefaultDeviceName();
    deviceManager_.addChangeListener(this);

    // Poll every 2s so we catch OS-level default-device changes that don't
    // always come through AudioDeviceManager's change broadcaster.
    startTimer(2000);
}

AudioPreferencesPanel::~AudioPreferencesPanel()
{
    stopTimer();
    deviceManager_.removeChangeListener(this);
}

juce::String AudioPreferencesPanel::currentDefaultDeviceName() const
{
    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type == nullptr) return {};

    const int idx = type->getDefaultDeviceIndex(false);
    const auto names = type->getDeviceNames(false);
    if (juce::isPositiveAndBelow(idx, names.size()))
        return names[idx];
    return {};
}

void AudioPreferencesPanel::timerCallback()
{
    if (auto* type = deviceManager_.getCurrentDeviceTypeObject())
        type->scanForDevices();

    const auto name = currentDefaultDeviceName();
    if (name == lastDefaultName_) return;

    lastDefaultName_ = name;
    rebuildDeviceList();

    // If the user's selection is "System default", move the audio engine
    // to the new default device so output follows the OS.
    if (usingDefault_)
        applySelectedDevice();
}

void AudioPreferencesPanel::rebuildBufferList()
{
    bufferCombo_.clear(juce::dontSendNotification);
    for (int size : kBufferSizes)
        bufferCombo_.addItem(juce::String(size) + " samples", size);

    // Load persisted value, falling back to current device size, then default.
    int saved = kDefaultBufferSize;
    if (auto* s = appProps_.getUserSettings())
        saved = s->getIntValue(kBufferSizeKey, kDefaultBufferSize);

    bufferCombo_.setSelectedId(saved, juce::dontSendNotification);

    // Apply the saved size to the device on first load.
    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (setup.bufferSize != saved)
    {
        setup.bufferSize          = saved;
        deviceManager_.setAudioDeviceSetup(setup, true);
    }
}

void AudioPreferencesPanel::applySelectedBufferSize()
{
    const int size = bufferCombo_.getSelectedId();
    if (size <= 0) return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);
    if (setup.bufferSize == size) return;

    setup.bufferSize = size;
    const auto err = deviceManager_.setAudioDeviceSetup(setup, true);
    if (err.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Audio Buffer",
            "Could not set buffer size to " + juce::String(size) + ": " + err);
        rebuildBufferList();
        return;
    }

    if (auto* s = appProps_.getUserSettings())
        s->setValue(kBufferSizeKey, size);
}

static constexpr int kSystemDefaultId = 1;

void AudioPreferencesPanel::rebuildDeviceList()
{
    deviceCombo_.clear(juce::dontSendNotification);

    auto* type = deviceManager_.getCurrentDeviceTypeObject();
    if (type == nullptr) return;

    type->scanForDevices();
    const auto devices = type->getDeviceNames(false);

    // "System default" is always the top option; we annotate it with the name
    // of whichever device macOS currently considers the default output.
    const int defaultIndex = type->getDefaultDeviceIndex(false);
    juce::String defaultLabel = "System default";
    if (juce::isPositiveAndBelow(defaultIndex, devices.size()))
        defaultLabel += " (" + devices[defaultIndex] + ")";

    deviceCombo_.addItem(defaultLabel, kSystemDefaultId);
    deviceCombo_.addSeparator();

    juce::String currentName;
    if (auto* device = deviceManager_.getCurrentAudioDevice())
        currentName = device->getName();

    int selectedId = 0;
    for (int i = 0; i < devices.size(); ++i)
    {
        const int itemId = i + 2;   // ids 2+ ; reserve 1 for "System default"
        deviceCombo_.addItem(devices[i], itemId);
        if (!usingDefault_ && devices[i] == currentName)
            selectedId = itemId;
    }

    if (usingDefault_)
        deviceCombo_.setSelectedId(kSystemDefaultId, juce::dontSendNotification);
    else if (selectedId > 0)
        deviceCombo_.setSelectedId(selectedId, juce::dontSendNotification);
}

void AudioPreferencesPanel::applySelectedDevice()
{
    const int  id           = deviceCombo_.getSelectedId();
    const bool wantsDefault = (id == kSystemDefaultId);

    // Resolve "System default" to the actual current default-device name.
    // Passing an empty outputDeviceName to AudioDeviceManager tells it to
    // close the device entirely, which is why audio went silent before.
    const juce::String desired = wantsDefault
                                     ? currentDefaultDeviceName()
                                     : deviceCombo_.getText();
    if (desired.isEmpty())
        return;

    juce::AudioDeviceManager::AudioDeviceSetup setup;
    deviceManager_.getAudioDeviceSetup(setup);

    if (setup.outputDeviceName == desired && usingDefault_ == wantsDefault)
        return;

    setup.outputDeviceName = desired;
    const auto err = deviceManager_.setAudioDeviceSetup(setup, /*treatAsChosenDevice*/ true);
    if (err.isNotEmpty())
    {
        juce::AlertWindow::showMessageBoxAsync(
            juce::MessageBoxIconType::WarningIcon,
            "Audio Device",
            "Could not switch to \"" + (wantsDefault ? juce::String("System default") : desired)
            + "\": " + err);
        rebuildDeviceList();
        return;
    }

    usingDefault_ = wantsDefault;
}

void AudioPreferencesPanel::changeListenerCallback(juce::ChangeBroadcaster*)
{
    rebuildDeviceList();
}

void AudioPreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void AudioPreferencesPanel::resized()
{
    auto bounds = getLocalBounds().reduced(24);

    auto row1 = bounds.removeFromTop(32);
    deviceLabel_.setBounds(row1.removeFromLeft(140));
    row1.removeFromLeft(8);
    deviceCombo_.setBounds(row1);

    bounds.removeFromTop(12);

    auto row2 = bounds.removeFromTop(32);
    bufferLabel_.setBounds(row2.removeFromLeft(140));
    row2.removeFromLeft(8);
    bufferCombo_.setBounds(row2);
}

// ============================================================================
// LibraryPreferencesPanel
// ============================================================================

static void styleFolderListBox(juce::ListBox& lb)
{
    lb.setColour(juce::ListBox::backgroundColourId, Color::headerBackground);
    lb.setColour(juce::ListBox::outlineColourId,    Color::border);
    lb.setOutlineThickness(1);
    lb.setRowHeight(32);
    lb.setMultipleSelectionEnabled(true);
}

static void stylePrefButton(juce::TextButton& b)
{
    b.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
    b.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
}

LibraryPreferencesPanel::LibraryPreferencesPanel()
{
    podcastListModel_.owner = this;
    addMouseListener(this, true);

    heading_.setText("Music Folders", juce::dontSendNotification);
    heading_.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)).boldened());
    heading_.setColour(juce::Label::textColourId, Color::textPrimary);
    addAndMakeVisible(heading_);

    list_.setModel(this);
    styleFolderListBox(list_);
    addAndMakeVisible(list_);

    stylePrefButton(addButton_);
    stylePrefButton(removeButton_);
    addButton_.onClick    = [this] { addFolder(); };
    removeButton_.onClick = [this] { removeSelectedFolders(); };
    addAndMakeVisible(addButton_);
    addAndMakeVisible(removeButton_);

    podcastHeading_.setText("Podcast Folders", juce::dontSendNotification);
    podcastHeading_.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)).boldened());
    podcastHeading_.setColour(juce::Label::textColourId, Color::textPrimary);
    addAndMakeVisible(podcastHeading_);

    podcastList_.setModel(&podcastListModel_);
    styleFolderListBox(podcastList_);
    addAndMakeVisible(podcastList_);

    stylePrefButton(podcastAddButton_);
    stylePrefButton(podcastRemoveButton_);
    podcastAddButton_.onClick    = [this] { addPodcastFolder(); };
    podcastRemoveButton_.onClick = [this] { removeSelectedPodcastFolders(); };
    addAndMakeVisible(podcastAddButton_);
    addAndMakeVisible(podcastRemoveButton_);
}

void LibraryPreferencesPanel::setFolders(std::vector<juce::File> folders)
{
    folders_ = std::move(folders);
    list_.updateContent();
    list_.repaint();
}

void LibraryPreferencesPanel::setPodcastFolders(std::vector<juce::File> folders)
{
    podcastFolders_ = std::move(folders);
    podcastList_.updateContent();
    podcastList_.repaint();
}

void LibraryPreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void LibraryPreferencesPanel::mouseDown(const juce::MouseEvent& e)
{
    if (dynamic_cast<juce::Button*>(e.eventComponent) != nullptr)
        return;

    const auto pos = e.getEventRelativeTo(this).getPosition();
    if (!list_.getBounds().contains(pos))
        list_.deselectAllRows();
    if (!podcastList_.getBounds().contains(pos))
        podcastList_.deselectAllRows();
}

void LibraryPreferencesPanel::resized()
{
    constexpr int pad      = 16;
    constexpr int headingH = 24;
    constexpr int gap      = 10;
    constexpr int sectionGap = 20;
    constexpr int btnH     = 28;
    constexpr int listRowH = 32;
    constexpr int listRows = 3;
    constexpr int listH    = listRowH * listRows + 2;

    auto area = getLocalBounds().reduced(pad, pad);

    // Music folders section
    heading_.setBounds(area.removeFromTop(headingH));
    area.removeFromTop(gap);
    list_.setBounds(area.removeFromTop(listH));
    area.removeFromTop(gap);
    auto btnRow = area.removeFromTop(btnH);
    addButton_.setBounds(btnRow.removeFromLeft(120));
    btnRow.removeFromLeft(8);
    removeButton_.setBounds(btnRow.removeFromLeft(140));

    // Podcast folders section
    area.removeFromTop(sectionGap);
    podcastHeading_.setBounds(area.removeFromTop(headingH));
    area.removeFromTop(gap);
    podcastList_.setBounds(area.removeFromTop(listH));
    area.removeFromTop(gap);
    auto podBtnRow = area.removeFromTop(btnH);
    podcastAddButton_.setBounds(podBtnRow.removeFromLeft(120));
    podBtnRow.removeFromLeft(8);
    podcastRemoveButton_.setBounds(podBtnRow.removeFromLeft(140));
}

int LibraryPreferencesPanel::getNumRows()
{
    return static_cast<int>(folders_.size());
}

void LibraryPreferencesPanel::paintListBoxItem(int row, juce::Graphics& g,
                                                int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int>(folders_.size())) return;

    if (selected)
        g.fillAll(Color::tableSelected);
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);

    g.setColour(Color::textPrimary);
    g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    g.drawText(folders_[static_cast<size_t>(row)].getFullPathName(),
               12, 0, width - 24, height,
               juce::Justification::centredLeft, true);
}

int LibraryPreferencesPanel::PodcastListModel::getNumRows()
{
    return static_cast<int>(owner->podcastFolders_.size());
}

void LibraryPreferencesPanel::PodcastListModel::paintListBoxItem(int row, juce::Graphics& g,
                                                                   int width, int height,
                                                                   bool selected)
{
    const auto& folders = owner->podcastFolders_;
    if (row < 0 || row >= static_cast<int>(folders.size())) return;

    if (selected)
        g.fillAll(Color::tableSelected);
    else
        g.fillAll(row % 2 == 0 ? Color::tableBackground : Color::tableRowAlt);

    g.setColour(Color::textPrimary);
    g.setFont(juce::Font(juce::FontOptions().withHeight(14.0f)));
    g.drawText(folders[static_cast<size_t>(row)].getFullPathName(),
               12, 0, width - 24, height,
               juce::Justification::centredLeft, true);
}

void LibraryPreferencesPanel::addFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Add a music folder",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory));

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;
            const auto folder = results[0];
            for (const auto& f : folders_)
                if (f == folder) return;

            auto updated = folders_;
            updated.push_back(folder);
            if (onFoldersChanged) onFoldersChanged(std::move(updated));
        });
}

void LibraryPreferencesPanel::removeSelectedFolders()
{
    auto selected = list_.getSelectedRows();
    if (selected.isEmpty()) return;

    auto updated = folders_;
    std::vector<int> rowsDesc;
    for (int i = 0; i < selected.size(); ++i)
        rowsDesc.push_back(selected[i]);
    std::sort(rowsDesc.begin(), rowsDesc.end(), std::greater<int>());
    for (int row : rowsDesc)
        if (row >= 0 && row < static_cast<int>(updated.size()))
            updated.erase(updated.begin() + row);

    list_.deselectAllRows();
    if (onFoldersChanged) onFoldersChanged(std::move(updated));
}

void LibraryPreferencesPanel::addPodcastFolder()
{
    auto chooser = std::make_shared<juce::FileChooser>(
        "Add a podcast folder",
        juce::File::getSpecialLocation(juce::File::userHomeDirectory));

    chooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [this, chooser](const juce::FileChooser& fc) {
            const auto results = fc.getResults();
            if (results.isEmpty()) return;
            const auto folder = results[0];
            for (const auto& f : podcastFolders_)
                if (f == folder) return;

            auto updated = podcastFolders_;
            updated.push_back(folder);
            if (onPodcastFoldersChanged) onPodcastFoldersChanged(std::move(updated));
        });
}

void LibraryPreferencesPanel::removeSelectedPodcastFolders()
{
    auto selected = podcastList_.getSelectedRows();
    if (selected.isEmpty()) return;

    auto updated = podcastFolders_;
    std::vector<int> rowsDesc;
    for (int i = 0; i < selected.size(); ++i)
        rowsDesc.push_back(selected[i]);
    std::sort(rowsDesc.begin(), rowsDesc.end(), std::greater<int>());
    for (int row : rowsDesc)
        if (row >= 0 && row < static_cast<int>(updated.size()))
            updated.erase(updated.begin() + row);

    podcastList_.deselectAllRows();
    if (onPodcastFoldersChanged) onPodcastFoldersChanged(std::move(updated));
}

// ============================================================================
// MiscPreferencesPanel
// ============================================================================

MiscPreferencesPanel::MiscPreferencesPanel(juce::ApplicationProperties& props)
    : props_(props)
{
    const bool current = [&] {
        if (auto* s = props_.getUserSettings())
            return s->getBoolValue(kAskBeforeQuittingKey, true);
        return true;
    }();

    askBeforeQuittingToggle_.setButtonText("Ask before quitting");
    askBeforeQuittingToggle_.setToggleState(current, juce::dontSendNotification);
    askBeforeQuittingToggle_.setColour(juce::ToggleButton::textColourId,        Color::textPrimary);
    askBeforeQuittingToggle_.setColour(juce::ToggleButton::tickColourId,        Color::accent);
    askBeforeQuittingToggle_.setColour(juce::ToggleButton::tickDisabledColourId, Color::textSecondary);
    askBeforeQuittingToggle_.onClick = [this] {
        if (auto* s = props_.getUserSettings())
            s->setValue(kAskBeforeQuittingKey, askBeforeQuittingToggle_.getToggleState());
    };
    addAndMakeVisible(askBeforeQuittingToggle_);
}

void MiscPreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void MiscPreferencesPanel::resized()
{
    askBeforeQuittingToggle_.setBounds(getLocalBounds().reduced(24).removeFromTop(32));
}

// ============================================================================
// DebugPreferencesPanel
// ============================================================================

static constexpr const char* kDeleteFoxpKey = "debug.deleteFoxpOnShutdown";

DebugPreferencesPanel::DebugPreferencesPanel(juce::ApplicationProperties& props)
    : props_(props)
{
    const bool current = [&] {
        if (auto* s = props_.getUserSettings())
            return s->getBoolValue(kDeleteFoxpKey, false);
        return false;
    }();

    deleteFoxpToggle_.setButtonText("Delete all .foxp files in Library on shutdown");
    deleteFoxpToggle_.setToggleState(current, juce::dontSendNotification);
    deleteFoxpToggle_.setColour(juce::ToggleButton::textColourId,       Color::textPrimary);
    deleteFoxpToggle_.setColour(juce::ToggleButton::tickColourId,       Color::accent);
    deleteFoxpToggle_.setColour(juce::ToggleButton::tickDisabledColourId, Color::textSecondary);
    deleteFoxpToggle_.onClick = [this] {
        if (auto* s = props_.getUserSettings())
            s->setValue(kDeleteFoxpKey, deleteFoxpToggle_.getToggleState());
    };
    addAndMakeVisible(deleteFoxpToggle_);
}

void DebugPreferencesPanel::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void DebugPreferencesPanel::resized()
{
    deleteFoxpToggle_.setBounds(getLocalBounds().reduced(24).removeFromTop(32));
}

// ============================================================================
// PreferencesComponent
// ============================================================================

PreferencesComponent::PreferencesComponent(juce::AudioDeviceManager& dm,
                                           juce::ApplicationProperties& appProperties)
    : deviceManager_(dm), appProperties_(appProperties)
{
    items_.push_back({ Category::Audio,   "Audio",   {} });
    items_.push_back({ Category::Library, "Library", {} });
    items_.push_back({ Category::Misc,    "Misc",    {} });
    items_.push_back({ Category::Debug,   "Debug",   {} });

    audioPanel_   = std::make_unique<AudioPreferencesPanel>(deviceManager_, appProperties_);
    libraryPanel_ = std::make_unique<LibraryPreferencesPanel>();
    miscPanel_    = std::make_unique<MiscPreferencesPanel>(appProperties);
    debugPanel_   = std::make_unique<DebugPreferencesPanel>(appProperties);
    addChildComponent(*libraryPanel_);
    addChildComponent(*audioPanel_);
    addChildComponent(*miscPanel_);
    addChildComponent(*debugPanel_);

    showPanel(current_);
    setSize(640, 480);
}

void PreferencesComponent::showPanel(Category c)
{
    current_ = c;
    if (audioPanel_)   audioPanel_->setVisible(c == Category::Audio);
    if (libraryPanel_) libraryPanel_->setVisible(c == Category::Library);
    if (miscPanel_)    miscPanel_->setVisible(c == Category::Misc);
    if (debugPanel_)   debugPanel_->setVisible(c == Category::Debug);
    repaint();
}

void PreferencesComponent::layoutSidebar()
{
    int y = 16;
    for (auto& item : items_)
    {
        item.bounds = juce::Rectangle<int>(0, y, sidebarWidth, itemHeight);
        y += itemHeight;
    }
}

void PreferencesComponent::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);

    // Sidebar background
    g.setColour(Color::headerBackground);
    g.fillRect(0, 0, sidebarWidth, getHeight());

    // Right border of sidebar
    g.setColour(Color::border);
    g.drawVerticalLine(sidebarWidth - 1, 0.0f, static_cast<float>(getHeight()));

    // Items
    for (const auto& item : items_)
    {
        const bool selected = (item.category == current_);

        if (selected)
        {
            g.setColour(Color::background);
            g.fillRect(item.bounds);
            g.setColour(Color::accent);
            g.fillRect(item.bounds.withWidth(3));
        }

        g.setColour(selected ? Color::textPrimary : Color::textSecondary);
        g.setFont(juce::Font(juce::FontOptions().withHeight(15.0f)));
        g.drawText(item.label,
                   item.bounds.reduced(16, 0),
                   juce::Justification::centredLeft,
                   false);
    }

}

void PreferencesComponent::resized()
{
    layoutSidebar();

    const auto content = juce::Rectangle<int>(sidebarWidth, 0,
                                              getWidth() - sidebarWidth, getHeight());
    if (audioPanel_)   audioPanel_->setBounds(content);
    if (libraryPanel_) libraryPanel_->setBounds(content);
    if (miscPanel_)    miscPanel_->setBounds(content);
    if (debugPanel_)   debugPanel_->setBounds(content);
}

void PreferencesComponent::mouseDown(const juce::MouseEvent& e)
{
    for (const auto& item : items_)
    {
        if (item.bounds.contains(e.getPosition()))
        {
            if (item.category != current_)
                showPanel(item.category);
            return;
        }
    }
}

// ============================================================================
// PreferencesWindow
// ============================================================================

PreferencesWindow::PreferencesWindow(juce::AudioDeviceManager& dm,
                                     juce::ApplicationProperties& appProperties)
    : juce::DocumentWindow("Preferences",
                           Color::background,
                           juce::DocumentWindow::minimiseButton | juce::DocumentWindow::closeButton)
{
    setUsingNativeTitleBar(true);
    setResizable(true, false);
    setResizeLimits(420, 320, 1200, 900);

    auto* content = new PreferencesComponent(dm, appProperties);
    prefsComponent_ = content;
    setContentOwned(content, true);
    centreWithSize(640, 480);
    setVisible(false);
}

void PreferencesWindow::closeButtonPressed()
{
    setVisible(false);
    if (onClosed) onClosed();
}

LibraryPreferencesPanel* PreferencesWindow::libraryPanel()
{
    return prefsComponent_ != nullptr ? &prefsComponent_->libraryPanel() : nullptr;
}

void PreferencesWindow::showLibraryCategory()
{
    if (prefsComponent_ != nullptr)
        prefsComponent_->showLibraryCategory();
}


} // namespace FoxPlayer
