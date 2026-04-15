#include "TransportBar.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int buttonH    = 32;
static constexpr int buttonW    = 40;
static constexpr int playBtnW   = 48;
static constexpr int seekH      = 12;
static constexpr int pad        = 10;
static constexpr int artSize    = 52; // album art square, fits in 60px content area

TransportBar::TransportBar(AudioEngine& engine)
    : engine_(engine)
{
    // Style buttons
    auto styleButton = [](juce::TextButton& btn) {
        btn.setColour(juce::TextButton::buttonColourId,   juce::Colour(0xff2a2a2a));
        btn.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff3a5a8a));
        btn.setColour(juce::TextButton::textColourOffId,  Color::buttonText);
        btn.setColour(juce::TextButton::textColourOnId,   Color::buttonText);
    };

    styleButton(prevButton_);
    styleButton(playPauseButton_);
    styleButton(nextButton_);
    styleButton(queueButton_);

    playPauseButton_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")));

    addAndMakeVisible(prevButton_);
    addAndMakeVisible(playPauseButton_);
    addAndMakeVisible(nextButton_);
    addAndMakeVisible(queueButton_);

    // Volume slider
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider_.setRange(0.0, 1.0, 0.0);
    volumeSlider_.setValue(1.0, juce::dontSendNotification);
    volumeSlider_.setColour(juce::Slider::trackColourId,      Color::seekBarFill);
    volumeSlider_.setColour(juce::Slider::backgroundColourId, Color::seekBarTrack);
    volumeSlider_.setColour(juce::Slider::thumbColourId,      Color::textPrimary);
    volumeSlider_.onValueChange = [this] {
        engine_.setVolume(static_cast<float>(volumeSlider_.getValue()));
    };
    addAndMakeVisible(volumeSlider_);

    volLabel_.setText("VOL", juce::dontSendNotification);
    volLabel_.setFont(juce::Font(10.0f));
    volLabel_.setColour(juce::Label::textColourId, Color::textDim);
    volLabel_.setJustificationType(juce::Justification::centredRight);
    volLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(volLabel_);

    trackLabel_.setColour(juce::Label::textColourId, Color::textPrimary);
    trackLabel_.setFont(juce::Font(13.0f, juce::Font::bold));
    trackLabel_.setJustificationType(juce::Justification::centredLeft);
    trackLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(trackLabel_);

    timeLabel_.setColour(juce::Label::textColourId, Color::textSecondary);
    timeLabel_.setFont(juce::Font(12.0f));
    timeLabel_.setJustificationType(juce::Justification::centredRight);
    timeLabel_.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(timeLabel_);

    prevButton_.onClick = [this] {
        if (onPrevClicked) onPrevClicked();
    };

    playPauseButton_.onClick = [this] {
        if (engine_.isPlaying())
            engine_.pause();
        else if (engine_.isPaused())
            engine_.resume();
        updateDisplay();
    };

    nextButton_.onClick = [this] {
        if (onNextClicked) onNextClicked();
    };

    queueButton_.onClick = [this] {
        if (onQueueToggleClicked) onQueueToggleClicked();
    };

    startTimerHz(30);
}

TransportBar::~TransportBar()
{
    stopTimer();
}

void TransportBar::setCurrentTrack(const TrackInfo& track)
{
    currentTrack_ = track;
    hasTrack_ = true;
    albumArt_ = AlbumArtExtractor::extractFromFile(track.file);
    updateDisplay();
}

void TransportBar::clearTrack()
{
    hasTrack_ = false;
    albumArt_ = {};
    trackLabel_.setText("", juce::dontSendNotification);
    timeLabel_.setText("", juce::dontSendNotification);
    playPauseButton_.setButtonText(juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6")));
    repaint();
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(Color::transportBg);

    // Top border
    g.setColour(Color::border);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

    // Seek bar track
    g.setColour(Color::seekBarTrack);
    g.fillRect(seekBarBounds_);

    if (hasTrack_)
    {
        const double pos = draggingSeek_ ? seekDragPos_ : engine_.normalizedPosition();
        const int fillW = static_cast<int>(pos * seekBarBounds_.getWidth());

        g.setColour(Color::seekBarFill);
        g.fillRect(seekBarBounds_.withWidth(fillW));
    }

    // Album art
    if (albumArt_.isValid())
        g.drawImageWithin(albumArt_,
                          albumArtBounds_.getX(), albumArtBounds_.getY(),
                          albumArtBounds_.getWidth(), albumArtBounds_.getHeight(),
                          juce::RectanglePlacement::centred |
                          juce::RectanglePlacement::onlyReduceInSize);
}

void TransportBar::resized()
{
    auto bounds = getLocalBounds();

    // Seek bar spans the full width at the very top, no padding or rounding
    seekBarBounds_ = bounds.removeFromTop(seekH);

    bounds = bounds.reduced(pad, 0);

    // Album art slot on the far left (always reserved; only painted when valid)
    albumArtBounds_ = bounds.removeFromLeft(artSize).withSizeKeepingCentre(artSize, artSize);
    bounds.removeFromLeft(8);

    // Transport buttons centered horizontally
    const int totalBtnW = buttonW + 4 + playBtnW + 4 + buttonW;
    const int btnGroupX = bounds.getCentreX() - totalBtnW / 2;
    prevButton_.setBounds    (btnGroupX,                            bounds.getCentreY() - buttonH / 2, buttonW,  buttonH);
    playPauseButton_.setBounds(btnGroupX + buttonW + 4,             bounds.getCentreY() - buttonH / 2, playBtnW, buttonH);
    nextButton_.setBounds    (btnGroupX + buttonW + 4 + playBtnW + 4, bounds.getCentreY() - buttonH / 2, buttonW,  buttonH);

    // Right side (from right to left): Queue | VOL label + slider | time
    queueButton_.setBounds(bounds.removeFromRight(64).withSizeKeepingCentre(64, buttonH));
    bounds.removeFromRight(4);
    volumeSlider_.setBounds(bounds.removeFromRight(volumeSliderWidth).withSizeKeepingCentre(volumeSliderWidth, buttonH));
    const int volLabelW = 28;
    volLabel_.setBounds(bounds.removeFromRight(volLabelW).withSizeKeepingCentre(volLabelW, buttonH));
    bounds.removeFromRight(4);
    timeLabel_.setBounds(bounds.removeFromRight(90).withSizeKeepingCentre(90, buttonH));

    // Track label fills the remaining centre-left space
    trackLabel_.setBounds(bounds);
}

void TransportBar::timerCallback()
{
    updateDisplay();
}

void TransportBar::updateDisplay()
{
    if (!hasTrack_) return;

    playPauseButton_.setButtonText(engine_.isPlaying()
        ? juce::String(juce::CharPointer_UTF8("\xe2\x8f\xb8"))   // ⏸
        : juce::String(juce::CharPointer_UTF8("\xe2\x96\xb6"))); // ▶

    const double elapsed = engine_.elapsedSeconds();
    const double total   = engine_.durationSeconds();
    timeLabel_.setText(formatSeconds(elapsed) + " / " + formatSeconds(total),
                       juce::dontSendNotification);

    if (!draggingSeek_)
        repaint();
}

juce::String TransportBar::formatSeconds(double secs) const
{
    if (secs < 0.0) return "--:--";
    const int total = static_cast<int>(secs);
    return juce::String::formatted("%d:%02d", total / 60, total % 60);
}

void TransportBar::mouseDown(const juce::MouseEvent& e)
{
    if (!hasTrack_) return;
    if (seekBarBounds_.contains(e.x, e.y))
    {
        draggingSeek_ = true;
        seekDragPos_  = seekBarNormalizedX(e.x);
        repaint();
    }
}

void TransportBar::mouseDrag(const juce::MouseEvent& e)
{
    if (!draggingSeek_) return;
    seekDragPos_ = seekBarNormalizedX(e.x);
    repaint();
}

void TransportBar::mouseUp(const juce::MouseEvent& e)
{
    if (!draggingSeek_) return;
    draggingSeek_ = false;
    engine_.seekToNormalized(seekBarNormalizedX(e.x));
    repaint();
}

double TransportBar::seekBarNormalizedX(int x) const
{
    const int left  = seekBarBounds_.getX();
    const int width = seekBarBounds_.getWidth();
    if (width <= 0) return 0.0;
    return juce::jlimit(0.0, 1.0, static_cast<double>(x - left) / width);
}

} // namespace FoxPlayer
