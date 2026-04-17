#include "TransportBar.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int modBtnD    = 32;   // diameter of shuffle/repeat circles
static constexpr int skipBtnD   = 38;   // diameter of prev/next circles
static constexpr int playBtnD   = 46;   // diameter of play/pause circle
static constexpr int seekH      = 6;
static constexpr int pad        = 10;

// ----------------------------------------------------------------------------
// TransportButton
// ----------------------------------------------------------------------------

void TransportButton::paint(juce::Graphics& g)
{
    const float w  = static_cast<float>(getWidth());
    const float h  = static_cast<float>(getHeight());
    const float d  = juce::jmin(w, h);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float r  = d * 0.5f;

    // Circle fill and icon color. Toggleable buttons (shuffle/repeat/pin) are
    // dim grey when off and light up chrome + red when on. Non-toggle buttons
    // (play/pause/prev/next) always show the chrome look.
    juce::Colour fill, iconColor;
    if (toggleStyle && toggleState == 0)
    {
        fill      = pressed_ ? juce::Colour(0xff3a3a3a)
                  : hovered_ ? juce::Colour(0xff666666)
                  :             juce::Colour(0xff505050);
        iconColor = juce::Colour(0xff909090);
    }
    else if (toggleStyle)
    {
        fill      = pressed_ ? juce::Colour(0xff989898)
                  : hovered_ ? juce::Colour(0xffe2e2e2)
                  :             juce::Colour(0xffc4c4c4);
        iconColor = juce::Colour(0xffdd3333);
    }
    else
    {
        fill      = pressed_ ? juce::Colour(0xff989898)
                  : hovered_ ? juce::Colour(0xffe2e2e2)
                  :             juce::Colour(0xffc4c4c4);
        iconColor = juce::Colours::black;
    }
    g.setColour(fill);
    g.fillEllipse(cx - r, cy - r, d, d);

    // Select the SVG source for this icon/state combination.
    const char* svgData = nullptr;
    int         svgSize = 0;

    switch (icon)
    {
    case Icon::Play:
        svgData = BinaryData::playfill_svg;         svgSize = BinaryData::playfill_svgSize;         break;
    case Icon::Pause:
        svgData = BinaryData::pausefill_svg;        svgSize = BinaryData::pausefill_svgSize;        break;
    case Icon::Prev:
        svgData = BinaryData::skipbackfill_svg;     svgSize = BinaryData::skipbackfill_svgSize;     break;
    case Icon::Next:
        svgData = BinaryData::skipforwardfill_svg;  svgSize = BinaryData::skipforwardfill_svgSize;  break;
    case Icon::Shuffle:
        svgData = BinaryData::shufflefill_svg;      svgSize = BinaryData::shufflefill_svgSize;      break;
    case Icon::Repeat:
        if (toggleState == 2)
            { svgData = BinaryData::repeatoncefill_svg; svgSize = BinaryData::repeatoncefill_svgSize; }
        else
            { svgData = BinaryData::repeatfill_svg;     svgSize = BinaryData::repeatfill_svgSize;     }
        break;
    case Icon::Pin:
        svgData = BinaryData::pushpinsimplefill_svg;    svgSize = BinaryData::pushpinsimplefill_svgSize; break;
    }

    if (svgData == nullptr) return;

    // Rebuild the cached drawable only when icon, toggleState, or color changes.
    if (svgCache_.icon != icon || svgCache_.toggleState != toggleState || svgCache_.color != iconColor)
    {
        svgCache_.icon        = icon;
        svgCache_.toggleState = toggleState;
        svgCache_.color       = iconColor;
        svgCache_.drawable.reset();

        const auto xmlStr = juce::String::createStringFromData(svgData, svgSize);
        if (auto xml = juce::XmlDocument::parse(xmlStr))
            if (auto drawable = juce::Drawable::createFromSVG(*xml))
            {
                drawable->replaceColour(juce::Colours::black, iconColor);
                svgCache_.drawable = std::move(drawable);
            }
    }

    if (svgCache_.drawable)
    {
        const float iconSize = d * 0.52f;
        svgCache_.drawable->drawWithin(
            g,
            juce::Rectangle<float>(cx - iconSize * 0.5f, cy - iconSize * 0.5f, iconSize, iconSize),
            juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
            1.0f);
    }
}

void TransportButton::mouseDown(const juce::MouseEvent&)
{
    pressed_ = true;
    repaint();
}

void TransportButton::mouseUp(const juce::MouseEvent& e)
{
    pressed_ = false;
    repaint();
    if (getLocalBounds().contains(e.getPosition()) && onClick)
        onClick();
}

void TransportButton::mouseEnter(const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void TransportButton::mouseExit(const juce::MouseEvent&)
{
    hovered_ = false;
    pressed_ = false;
    repaint();
}

// Parses an SVG binary resource and returns a tinted Drawable.
static std::unique_ptr<juce::Drawable> loadSvgTinted(const char* data, int size, juce::Colour color)
{
    const auto xmlStr = juce::String::createStringFromData(data, size);
    if (auto xml = juce::XmlDocument::parse(xmlStr))
        if (auto d = juce::Drawable::createFromSVG(*xml))
        {
            d->replaceColour(juce::Colours::black, color);
            return d;
        }
    return nullptr;
}

void TransportBar::DraggingDotSliderLnF::drawLinearSlider(juce::Graphics& g,
                                                           int x, int y, int width, int height,
                                                           float sliderPos, float minPos, float maxPos,
                                                           juce::Slider::SliderStyle style,
                                                           juce::Slider& slider)
{
    juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height,
                                            sliderPos, minPos, maxPos, style, slider);

    if (slider.isMouseButtonDown())
    {
        const float thumbR = static_cast<float>(getSliderThumbRadius(slider));
        // V4's thumb is visibly smaller than its hit-radius, so use a tighter
        // ratio than the seek bar's 0.5 so the dot stays well inside the thumb.
        const float dotD   = thumbR * 0.6f;

        float thumbX, thumbY;
        if (style == juce::Slider::LinearVertical)
        {
            thumbX = x + width * 0.5f;
            thumbY = sliderPos;
        }
        else
        {
            thumbX = sliderPos;
            thumbY = y + height * 0.5f;
        }

        g.setColour(juce::Colour(0xff404040));
        g.fillEllipse(thumbX - dotD * 0.5f, thumbY - dotD * 0.5f, dotD, dotD);
    }
}

TransportBar::TransportBar(AudioEngine& engine)
    : engine_(engine)
{
    volumeSlider_.setLookAndFeel(&volumeSliderLnF_);
    shuffleButton_.icon   = TransportButton::Icon::Shuffle;
    prevButton_.icon      = TransportButton::Icon::Prev;
    playPauseButton_.icon = TransportButton::Icon::Play;
    nextButton_.icon      = TransportButton::Icon::Next;
    repeatButton_.icon    = TransportButton::Icon::Repeat;

    shuffleButton_.toggleStyle = true;
    repeatButton_.toggleStyle  = true;

    addAndMakeVisible(shuffleButton_);
    addAndMakeVisible(prevButton_);
    addAndMakeVisible(playPauseButton_);
    addAndMakeVisible(nextButton_);
    addAndMakeVisible(repeatButton_);

    // Volume slider (vertical)
    volumeSlider_.setSliderStyle(juce::Slider::LinearVertical);
    volumeSlider_.setTextBoxStyle(juce::Slider::NoTextBox, true, 0, 0);
    volumeSlider_.setRange(0.0, 1.0, 0.0);
    volumeSlider_.setValue(1.0, juce::dontSendNotification);
    volumeSlider_.setColour(juce::Slider::trackColourId,      Color::seekBarFill);
    volumeSlider_.setColour(juce::Slider::backgroundColourId, Color::seekBarTrack);
    volumeSlider_.setColour(juce::Slider::thumbColourId,      Color::textPrimary);
    volumeSlider_.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    volumeSlider_.onValueChange = [this] {
        if (muted_)
        {
            muted_ = false;
            repaint();
        }
        const double v = volumeSlider_.getValue();
        engine_.setVolume(static_cast<float>(v));
        if (onVolumeChanged) onVolumeChanged(v);
        refreshVolumeAlpha();
        repaint();  // update speaker icon
    };
    addAndMakeVisible(volumeSlider_);

    // Pre-load speaker SVG drawables with appropriate tint colors.
    const juce::Colour speakerColor = Color::textDim;
    const juce::Colour mutedColor   = juce::Colour(0xffdd3333);
    speakerNoneDrawable_  = loadSvgTinted(BinaryData::speakernonefill_svg,  BinaryData::speakernonefill_svgSize,  speakerColor);
    speakerLowDrawable_   = loadSvgTinted(BinaryData::speakerlowfill_svg,   BinaryData::speakerlowfill_svgSize,   speakerColor);
    speakerHighDrawable_  = loadSvgTinted(BinaryData::speakerhighfill_svg,  BinaryData::speakerhighfill_svgSize,  speakerColor);
    speakerMutedDrawable_ = loadSvgTinted(BinaryData::speakerxfill_svg,     BinaryData::speakerxfill_svgSize,     mutedColor);

    for (auto* lbl : { &elapsedLabel_, &totalLabel_ })
    {
        lbl->setColour(juce::Label::textColourId, Color::textSecondary);
        lbl->setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        lbl->setInterceptsMouseClicks(false, false);
        addAndMakeVisible(lbl);
    }
    elapsedLabel_.setJustificationType(juce::Justification::centredRight);
    totalLabel_.setJustificationType(juce::Justification::centredLeft);

    shuffleButton_.onClick = [this] {
        shuffleButton_.toggleState = shuffleButton_.toggleState ? 0 : 1;
        shuffleButton_.repaint();
        if (onShuffleToggled) onShuffleToggled(shuffleButton_.toggleState > 0);
    };

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

    repeatButton_.onClick = [this] {
        repeatButton_.toggleState = (repeatButton_.toggleState + 1) % 3;
        repeatButton_.repaint();
        if (onRepeatToggled) onRepeatToggled(repeatButton_.toggleState);
    };

    startTimerHz(30);
}

TransportBar::~TransportBar()
{
    stopTimer();
    volumeSlider_.setLookAndFeel(nullptr);
}

void TransportBar::setCurrentTrack(const TrackInfo& track)
{
    currentTrack_ = track;
    hasTrack_ = true;
    albumArt_ = AlbumArtExtractor::extractFromFile(track.file);
    compactScrollStartMs_ = juce::Time::getMillisecondCounter();
    resized();
    updateDisplay();
}

void TransportBar::setInitialVolume(double value)
{
    value = juce::jlimit(0.0, 1.0, value);
    volumeSlider_.setValue(value, juce::dontSendNotification);
    engine_.setVolume(static_cast<float>(value));
    muted_ = false;
    refreshVolumeAlpha();
    repaint();
}

void TransportBar::refreshVolumeAlpha()
{
    const bool silent = muted_ || volumeSlider_.getValue() <= 0.0;
    volumeSlider_.setAlpha(silent ? 0.25f : 1.0f);
}

void TransportBar::setShuffleOn(bool on)
{
    shuffleButton_.toggleState = on ? 1 : 0;
    shuffleButton_.repaint();
}

void TransportBar::setRepeatMode(int mode)
{
    repeatButton_.toggleState = juce::jlimit(0, 2, mode);
    repeatButton_.repaint();
}

void TransportBar::setPlayingFrom(const juce::String& sourceName, int sourceSidebarId)
{
    playingFromName_      = sourceName;
    playingFromSidebarId_ = sourceSidebarId;
    repaint();
}

void TransportBar::clearTrack()
{
    hasTrack_        = false;
    albumArt_        = {};
    playingFromName_ = {};
    sourceLinkBounds_ = {};
    elapsedLabel_.setText("", juce::dontSendNotification);
    totalLabel_.setText("", juce::dontSendNotification);
    playPauseButton_.icon = TransportButton::Icon::Play;
    resized();
    repaint();
}

void TransportBar::paint(juce::Graphics& g)
{
    g.fillAll(Color::transportBg);

    // Top border
    g.setColour(Color::border);
    g.drawHorizontalLine(0, 0.0f, static_cast<float>(getWidth()));

    // Album art and info text fade out smoothly as the window shrinks past
    // ~540px, reaching alpha 0 just before the mini-mode threshold flips the
    // layout. This way the layout collapse is visually invisible (the content
    // was already faded out) instead of an abrupt pop.
    constexpr float fadeFromW = 540.0f;
    const float     fadeToW   = static_cast<float>(miniModeWidth);
    const float     infoAlpha = juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(getWidth()) - fadeToW) / (fadeFromW - fadeToW));

    // Measure the widest info-text line so the gradient can anchor its
    // transparent-left edge just past the info text's right edge. That way
    // the fade only starts to obscure the title/artist/source once the text
    // is about to collide with the current-time label.
    auto textWidth = [](const juce::Font& f, const juce::String& text) -> int {
        juce::GlyphArrangement ga;
        ga.addLineOfText(f, text, 0.0f, 0.0f);
        return static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth()));
    };

    // Title rendering depends on whether there's a real title tag or we're
    // falling back to the filename. Real title -> italic, tag text only.
    // Filename fallback -> regular weight, include the file extension.
    const bool         hasRealTitle  = currentTrack_.title.isNotEmpty();
    const juce::String titleText     = hasRealTitle
                                           ? currentTrack_.title
                                           : currentTrack_.file.getFileName();

    const juce::Font titleFontBase   = juce::Font(juce::FontOptions().withHeight(17.0f));
    const juce::Font titleFont       = titleFontBase;
    const juce::Font artistFont      = juce::Font(juce::FontOptions().withHeight(15.0f));
    const juce::Font noArtistFont    = juce::Font(juce::FontOptions().withHeight(15.0f)).italicised();
    const juce::Font prefixFont      = juce::Font(juce::FontOptions().withHeight(15.0f)).italicised();
    const juce::Font sourceFont(juce::FontOptions().withHeight(15.0f));

    int infoMaxTextW = 0;
    int prefixTextW  = 0;
    int sourceTextW  = 0;
    if (hasTrack_ && !infoAreaBounds_.isEmpty())
    {
        infoMaxTextW = textWidth(titleFont, titleText);
        const bool noArtist = currentTrack_.artist.isEmpty();
        infoMaxTextW = juce::jmax(infoMaxTextW,
            noArtist ? textWidth(noArtistFont, juce::String("(no artist)"))
                     : textWidth(artistFont,   currentTrack_.artist));
        if (playingFromName_.isNotEmpty())
        {
            prefixTextW = textWidth(prefixFont, juce::String("Playing from: "));
            sourceTextW = textWidth(sourceFont, playingFromName_);
            infoMaxTextW = juce::jmax(infoMaxTextW, prefixTextW + sourceTextW);
        }
    }

    // Gradient boundaries. Solid region spans current-time -> total-time. The
    // left transparent edge is anchored to the info text's right edge (plus a
    // small gap) so the fade only impinges on the info text when it's close to
    // colliding with the current-time label. A minimum fade width keeps the
    // transition smooth even when the text overruns.
    float gradLeftTransparent = 0.0f, gradRightTransparent = 0.0f;
    float gradLeftSolidX = 0.0f,      gradRightSolidX      = 0.0f;
    bool  gradValid = false;
    if (hasTrack_)
    {
        constexpr float safeGap      = 6.0f;
        constexpr float minFadeWidth = 24.0f;
        constexpr float maxFadeWidth = 60.0f;

        const float pbCx = playPauseButton_.getBoundsInParent().toFloat().getCentreX();
        gradLeftSolidX  = !seekBarBounds_.isEmpty()
                              ? static_cast<float>(elapsedLabel_.getBounds().getX())
                              : pbCx;
        gradRightSolidX = !seekBarBounds_.isEmpty()
                              ? static_cast<float>(totalLabel_.getBounds().getRight())
                              : pbCx;

        // Where we'd *like* the fade to start from the left: right after the
        // info text (or album art if there's no text).
        float desiredStart = gradLeftSolidX;
        if (infoMaxTextW > 0)
            desiredStart = static_cast<float>(infoAreaBounds_.getX() + infoMaxTextW) + safeGap;
        else if (!albumArtBounds_.isEmpty())
            desiredStart = static_cast<float>(albumArtBounds_.getRight()) + safeGap;

        // Preferred range: fade stays within [minFadeWidth, maxFadeWidth] of
        // the solid region so the gradient hugs the current-time label rather
        // than reaching back to the info text unless they're close to colliding.
        const float earliestStart = gradLeftSolidX - maxFadeWidth;
        const float latestStart   = gradLeftSolidX - minFadeWidth;
        gradLeftTransparent = juce::jlimit(earliestStart, latestStart, desiredStart);

        // Hard floor: never start the fade to the left of the album art. The
        // gradient must never paint over the album art at any window size.
        if (!albumArtBounds_.isEmpty())
        {
            const float albumFloor = static_cast<float>(albumArtBounds_.getRight()) + safeGap;
            gradLeftTransparent = juce::jmax(gradLeftTransparent, albumFloor);
        }
        // Hard ceiling: fade must have non-negative width.
        gradLeftTransparent = juce::jmin(gradLeftTransparent, gradLeftSolidX);

        const float leftFadeWidth = gradLeftSolidX - gradLeftTransparent;
        gradRightTransparent = gradRightSolidX + leftFadeWidth;
        gradValid = leftFadeWidth > 0.0f;
    }

    // Album art (or placeholder when track loaded but no art). Stays fully
    // opaque - resized() shrinks it to fit rather than fading it out. A soft
    // black drop shadow sits behind it to lift it off the transport bar.
    if (!albumArtBounds_.isEmpty() && hasTrack_)
    {
        juce::Path artPath;
        artPath.addRectangle(albumArtBounds_.toFloat());
        juce::DropShadow(juce::Colours::black.withAlpha(0.85f), 12, {}).drawForPath(g, artPath);
    }
    if (!albumArtBounds_.isEmpty() && albumArt_.isValid())
    {
        g.drawImageWithin(albumArt_,
                          albumArtBounds_.getX(), albumArtBounds_.getY(),
                          albumArtBounds_.getWidth(), albumArtBounds_.getHeight(),
                          juce::RectanglePlacement::centred |
                          juce::RectanglePlacement::onlyReduceInSize);
    }

    // Three-line track info - faded by infoAlpha so it dissolves smoothly into
    // the background as the window approaches mini mode.
    if (hasTrack_ && !infoAreaBounds_.isEmpty() && infoAlpha > 0.0f)
    {
        constexpr int line1H  = 18;
        constexpr int line2H  = 16;
        constexpr int line3H  = 16;
        constexpr int lineGap = 5;
        constexpr int totalH  = line1H + lineGap + line2H + lineGap + line3H;

        const int infoY = infoAreaBounds_.getY() + (infoAreaBounds_.getHeight() - totalH) / 2;
        const int infoX = infoAreaBounds_.getX();
        const int infoW = infoAreaBounds_.getWidth();

        // Line 1: song title. Nudged up 2px so there's a touch more breathing
        // room above the artist line.
        g.setColour(Color::textPrimary.withMultipliedAlpha(infoAlpha));
        g.setFont(titleFont);
        g.drawText(titleText, infoX, infoY - 2, infoW, line1H,
                   juce::Justification::centredLeft, true);

        // Line 2: artist, or "(no artist)" in italics
        const int artist2Y = infoY + line1H + lineGap;
        const bool noArtist = currentTrack_.artist.isEmpty();
        g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
        if (noArtist)
        {
            g.setFont(noArtistFont);
            g.drawText("(no artist)", infoX, artist2Y, infoW, line2H,
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setFont(artistFont);
            g.drawText(currentTrack_.artist, infoX, artist2Y, infoW, line2H,
                       juce::Justification::centredLeft, true);
        }

        // Line 3: "Playing from: [source]" where source is accent-colored and
        // clickable. Nudged down 2px for a bit more separation from the artist.
        if (playingFromName_.isNotEmpty())
        {
            const int source3Y = artist2Y + line2H + lineGap + 2;

            g.setColour(Color::textDim.withMultipliedAlpha(infoAlpha));
            g.setFont(prefixFont);
            g.drawText("Playing from: ", infoX, source3Y, prefixTextW + 2, line3H,
                       juce::Justification::centredLeft, false);

            const int linkX = infoX + prefixTextW;
            g.setFont(sourceFont);
            g.setColour(Color::accent.withMultipliedAlpha(infoAlpha));
            g.drawText(playingFromName_, linkX, source3Y, infoW - prefixTextW, line3H,
                       juce::Justification::centredLeft, false);

            // Underline the source link
            g.drawHorizontalLine(source3Y + line3H - 1,
                                 static_cast<float>(linkX),
                                 static_cast<float>(linkX + sourceTextW));

            // Click hit-box: only cover the visible portion of the link. Once
            // the gradient starts fading the text out, the hidden tail stops
            // being clickable so we don't "fight" the visual effect.
            const int gradStart = gradValid ? static_cast<int>(gradLeftTransparent)
                                            : (infoX + infoW);
            const int linkRight = juce::jmin(linkX + sourceTextW + 4,
                                             infoX + (infoW - prefixTextW),
                                             gradStart);
            const int clippedW  = linkRight - linkX;
            sourceLinkBounds_ = (clippedW > 0)
                                    ? juce::Rectangle<int>(linkX, source3Y, clippedW, line3H)
                                    : juce::Rectangle<int>();
        }
        else
        {
            sourceLinkBounds_ = {};
        }
    }

    // Horizontal fade: solid transport-bg from the current-time label through
    // to the total-time label, falling off to fully transparent past the album
    // art (on the left) and past a symmetric point (on the right). Fills the
    // full height so the seek bar, drawn next, sits on top of it and long info
    // text behind it dissolves into the background as the window narrows.
    if (gradValid)
    {
        const float width          = gradRightTransparent - gradLeftTransparent;
        const float leftSolidStop  = (gradLeftSolidX  - gradLeftTransparent) / width;
        const float rightSolidStop = (gradRightSolidX - gradLeftTransparent) / width;

        juce::ColourGradient grad(
            Color::transportBg.withAlpha(0.0f), gradLeftTransparent,  0.0f,
            Color::transportBg.withAlpha(0.0f), gradRightTransparent, 0.0f,
            false);
        grad.addColour(leftSolidStop,  Color::transportBg);
        grad.addColour(rightSolidStop, Color::transportBg);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(gradLeftTransparent, 0.0f,
                                          width, static_cast<float>(getHeight())));
    }

    // Seek bar track + fill + thumb - drawn after the gradient so they stay
    // visible on top of the solid-black band behind the buttons.
    if (hasTrack_ && !seekBarBounds_.isEmpty())
    {
        g.setColour(Color::seekBarTrack);
        g.fillRect(seekBarBounds_);
        const double pos = draggingSeek_ ? seekDragPos_ : engine_.normalizedPosition();
        const int fillW = static_cast<int>(pos * seekBarBounds_.getWidth());

        g.setColour(Color::seekBarFill);
        g.fillRect(seekBarBounds_.withWidth(fillW));

        const auto  seekF  = seekBarBounds_.toFloat();
        const float thumbD = draggingSeek_ ? 16.0f : 12.0f;
        const float thumbX = seekF.getX() + static_cast<float>(pos) * seekF.getWidth();
        const float thumbY = seekF.getCentreY();
        g.setColour(juce::Colours::white);
        g.fillEllipse(thumbX - thumbD * 0.5f, thumbY - thumbD * 0.5f, thumbD, thumbD);

        // While dragging, render a dark-grey "pressed" dot in the centre of the
        // thumb. Its diameter is half the thumb's diameter.
        if (draggingSeek_)
        {
            const float dotD = thumbD * 0.5f;
            g.setColour(juce::Colour(0xff404040));
            g.fillEllipse(thumbX - dotD * 0.5f, thumbY - dotD * 0.5f, dotD, dotD);
        }
    }

    // Mini-mode "Artist - Title" (or filename) line above the buttons. Drawn
    // after the gradient so it stays fully legible rather than being faded out.
    // Left-aligned with the shuffle button; clipped at the repeat button. If
    // the text is wider than that span, marquee-scrolls iTunes-style: pause
    // at the start, scroll until the tail is visible, pause, then jump back.
    // Fades in across a small window just above the mini-mode threshold so
    // the appearance isn't abrupt.
    constexpr float compactFadeStartW = static_cast<float>(miniModeWidth) + 25.0f;
    constexpr float compactFadeEndW   = static_cast<float>(miniModeWidth);
    const float compactAlpha = juce::jlimit(0.0f, 1.0f,
        (compactFadeStartW - static_cast<float>(getWidth()))
            / (compactFadeStartW - compactFadeEndW));

    if (hasTrack_ && !compactInfoBounds_.isEmpty() && compactAlpha > 0.0f)
    {
        // "Artist - Title" where the title portion follows the same rule as
        // the full-mode title line: italic when it's a real tag, plain with
        // extension when we're falling back to the filename.
        const juce::String artistPrefix = currentTrack_.artist.isNotEmpty()
                                              ? currentTrack_.artist + " - "
                                              : juce::String();

        const juce::Font compactRegular(juce::FontOptions().withHeight(15.0f));
        const juce::Font compactTitleFont = hasRealTitle
                                                ? compactRegular.italicised()
                                                : compactRegular;

        const int prefixW = artistPrefix.isNotEmpty()
                                ? textWidth(compactRegular, artistPrefix) : 0;
        const int titleW  = textWidth(compactTitleFont, titleText);
        const int totalW  = prefixW + titleW;

        const juce::Colour compactColor = Color::textPrimary.withMultipliedAlpha(compactAlpha);

        auto drawAt = [&](int startX) {
            const int y = compactInfoBounds_.getY();
            const int h = compactInfoBounds_.getHeight();
            g.setColour(compactColor);
            if (prefixW > 0)
            {
                g.setFont(compactRegular);
                g.drawText(artistPrefix, startX, y, prefixW + 4, h,
                           juce::Justification::centredLeft, false);
            }
            g.setFont(compactTitleFont);
            g.drawText(titleText, startX + prefixW, y, titleW + 4, h,
                       juce::Justification::centredLeft, false);
        };

        if (totalW <= compactInfoBounds_.getWidth())
        {
            // Fits: centre above the play button.
            const int startX = compactInfoBounds_.getX()
                             + (compactInfoBounds_.getWidth() - totalW) / 2;
            drawAt(startX);
        }
        else
        {
            // Doesn't fit: left-align with the shuffle button and marquee-scroll.
            const int overflow = totalW - compactInfoBounds_.getWidth();
            constexpr juce::uint32 pauseMs        = 3000;
            constexpr float        scrollPxPerSec = 18.0f;
            const juce::uint32 scrollMs = juce::jmax<juce::uint32>(
                100, static_cast<juce::uint32>((static_cast<float>(overflow) / scrollPxPerSec) * 1000.0f));
            // Ping-pong cycle: pause, scroll out, pause, scroll back.
            const juce::uint32 cycleMs = 2 * (pauseMs + scrollMs);

            const juce::uint32 nowMs = juce::Time::getMillisecondCounter();
            const juce::uint32 phase = (nowMs - compactScrollStartMs_) % cycleMs;

            int offsetX;
            if (phase < pauseMs)
            {
                offsetX = 0;
            }
            else if (phase < pauseMs + scrollMs)
            {
                const float t = static_cast<float>(phase - pauseMs) / static_cast<float>(scrollMs);
                offsetX = -static_cast<int>(t * static_cast<float>(overflow));
            }
            else if (phase < 2 * pauseMs + scrollMs)
            {
                offsetX = -overflow;
            }
            else
            {
                const float t = static_cast<float>(phase - (2 * pauseMs + scrollMs)) / static_cast<float>(scrollMs);
                offsetX = -overflow + static_cast<int>(t * static_cast<float>(overflow));
            }

            g.saveState();
            g.reduceClipRegion(compactInfoBounds_);
            drawAt(compactInfoBounds_.getX() + offsetX);
            g.restoreState();
        }
    }

    // Speaker icon - red X whenever output is silent (muted or slider at zero).
    if (!speakerBounds_.isEmpty())
    {
        const double vol = volumeSlider_.getValue();
        juce::Drawable* d = nullptr;
        if (muted_ || vol <= 0.0)   d = speakerMutedDrawable_.get();
        else if (vol <= 0.45)       d = speakerLowDrawable_.get();
        else                        d = speakerHighDrawable_.get();

        if (d)
            d->drawWithin(g, speakerBounds_.toFloat(),
                          juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
                          1.0f);
    }
}

void TransportBar::resized()
{
    // Mini mode: when the bar is narrower than this, hide the album art,
    // info text, seek bar, and shuffle/repeat buttons - leaving just the
    // three core transport buttons and the volume strip. Lets the user shrink
    // the window into a compact controller.
    const bool mini = getWidth() < Constants::miniModeWidth;

    auto bounds = getLocalBounds().reduced(pad, 0);

    // Volume area (always visible): speaker icon + vertical slider, on the right.
    const int speakerSz = 22;
    const int sliderW   = 20;
    const int horizGap  = 6;
    const int volAreaW  = speakerSz + horizGap + sliderW;
    const int sliderH   = getHeight() - 2 * pad;
    const int volAreaX  = bounds.getRight() - volAreaW;

    speakerBounds_ = { volAreaX, (getHeight() - speakerSz) / 2, speakerSz, speakerSz };
    volumeSlider_.setBounds(volAreaX + speakerSz + horizGap, pad, sliderW, sliderH);
    bounds.removeFromRight(volAreaW + 4);

    // Buttons are centered on the window, independent of any left-side layout.
    // A small upward offset lifts the whole playback row + seek row off the
    // bottom of the bar at normal widths. As the window narrows toward mini
    // mode, the offset eases to zero so the row drops back to its original
    // vertical position (matching the pre-offset look in mini player territory).
    // With no track loaded the seek row is hidden, so the buttons sit dead-
    // centre vertically in the bar.
    constexpr int   playbackRowYOffsetMax   = 11;
    constexpr float playbackOffsetFadeStart = 510.0f;
    constexpr float playbackOffsetFadeEnd   = static_cast<float>(miniModeWidth);
    const float playbackOffsetT = juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(getWidth()) - playbackOffsetFadeEnd)
            / (playbackOffsetFadeStart - playbackOffsetFadeEnd));
    const int playbackRowYOffset = hasTrack_
                                       ? static_cast<int>(playbackOffsetT * playbackRowYOffsetMax)
                                       : 0;

    const int btnGap     = 8;
    const int centerX    = getWidth() / 2;
    const int btnCenterY = bounds.getCentreY() - playbackRowYOffset;
    const int skipOffY   = (playBtnD - skipBtnD) / 2;
    const int modOffY    = (playBtnD - modBtnD) / 2;

    // All five buttons remain visible in both modes; mini mode only sheds the
    // album art / info text / seek bar.
    const int totalBtnW = modBtnD + btnGap + skipBtnD + btnGap + playBtnD + btnGap + skipBtnD + btnGap + modBtnD;
    const int btnGroupX = centerX - totalBtnW / 2;

    const int x0 = btnGroupX;
    const int x1 = x0 + modBtnD  + btnGap;
    const int x2 = x1 + skipBtnD + btnGap;
    const int x3 = x2 + playBtnD + btnGap;
    const int x4 = x3 + skipBtnD + btnGap;

    shuffleButton_.setBounds  (x0, btnCenterY - playBtnD / 2 + modOffY,  modBtnD,  modBtnD);
    prevButton_.setBounds     (x1, btnCenterY - playBtnD / 2 + skipOffY, skipBtnD, skipBtnD);
    playPauseButton_.setBounds(x2, btnCenterY - playBtnD / 2,            playBtnD, playBtnD);
    nextButton_.setBounds     (x3, btnCenterY - playBtnD / 2 + skipOffY, skipBtnD, skipBtnD);
    repeatButton_.setBounds   (x4, btnCenterY - playBtnD / 2 + modOffY,  modBtnD,  modBtnD);

    // Album art stays at full size (80px) at every window width. In normal
    // mode it sits flush-left at `pad`. As the window narrows, instead of
    // shrinking it slides leftward, ending partially off-screen at minimum
    // width so its right portion can tuck behind the shuffle button.
    constexpr int   artMaxFull = 80;
    constexpr float fadeFromW  = 490.0f;
    const float fadeToW = static_cast<float>(Constants::minWindowWidth);
    const float artT = juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(getWidth()) - fadeToW) / (fadeFromW - fadeToW));

    const int artDim = artMaxFull;

    // Only reserve space for the album art when we actually have artwork to
    // draw. Tracks without embedded art (and the no-track state) collapse the
    // art column entirely so the info text can sit near the left edge.
    if (hasTrack_ && albumArt_.isValid())
    {
        const int leftX        = pad;
        constexpr int slidLeftX = -20; // left edge at min window width (partially off-screen)
        const int artX = leftX + static_cast<int>((1.0f - artT) * (slidLeftX - leftX));
        albumArtBounds_ = { artX, (getHeight() - artDim) / 2, artDim, artDim };
    }
    else
    {
        albumArtBounds_ = {};
    }

    // Push the info-text area in slightly so the title/artist/source lines
    // don't crowd whatever sits to their left (album art, or the window edge
    // if the art is hidden). Mini mode doesn't use infoAreaBounds_.
    constexpr int infoTextLeftGap = 13;
    if (!mini)
    {
        if (!albumArtBounds_.isEmpty())
            bounds.removeFromLeft(albumArtBounds_.getRight() - bounds.getX() + infoTextLeftGap);
        else
            bounds.removeFromLeft(infoTextLeftGap);
    }

    // Seek row stays visible in both modes so the user can still scrub in
    // mini mode. In mini mode the bar itself widens a bit since we no longer
    // share the row with the other layout elements.
    if (hasTrack_)
    {
        const int seekRowH = 20;
        const int gapH     = 3;
        const int seekRowY = btnCenterY + playBtnD / 2 + gapH;

        const int timeLW = 48;
        // Seek bar width scales smoothly with the window across the full range:
        // ~1/3 of window width, clamped between 80 and 400 so it never gets
        // tiny in narrow windows or absurdly wide in huge ones. No mini-mode
        // discontinuity.
        const int barW   = juce::jlimit(80, 400, getWidth() / 3);
        const int groupW = timeLW + 6 + barW + 6 + timeLW;
        const int groupX = centerX - groupW / 2;
        elapsedLabel_.setVisible(true);
        totalLabel_.setVisible(true);
        elapsedLabel_.setBounds(groupX, seekRowY, timeLW, seekRowH);
        seekBarBounds_ = { groupX + timeLW + 6, seekRowY + (seekRowH - seekH) / 2, barW, seekH };
        totalLabel_.setBounds(groupX + timeLW + 6 + barW + 6, seekRowY, timeLW, seekRowH);
    }
    else
    {
        seekBarBounds_ = {};
        elapsedLabel_.setVisible(false);
        totalLabel_.setVisible(false);
    }

    // Full-mode left-side info area.
    infoAreaBounds_ = mini ? juce::Rectangle<int>() : bounds;

    // Compact "Artist - Title" line above the play button. Computed whenever
    // the window is at or below the compact fade-in range (slightly above
    // mini-mode), so the paint-time alpha can crossfade it in/out smoothly.
    constexpr int compactFadeInMargin = 25;
    if (getWidth() < Constants::miniModeWidth + compactFadeInMargin)
    {
        constexpr int compactH = 20;
        const int compactY = btnCenterY - playBtnD / 2 - compactH - 3;
        compactInfoBounds_ = {
            shuffleButton_.getX(), compactY,
            repeatButton_.getRight() - shuffleButton_.getX(), compactH
        };
    }
    else
    {
        compactInfoBounds_ = {};
    }
}

void TransportBar::timerCallback()
{
    updateDisplay();
}

void TransportBar::updateDisplay()
{
    if (!hasTrack_) return;

    playPauseButton_.icon = engine_.isPlaying() ? TransportButton::Icon::Pause
                                                : TransportButton::Icon::Play;
    playPauseButton_.repaint();

    const double elapsed = engine_.elapsedSeconds();
    const double total   = engine_.durationSeconds();
    elapsedLabel_.setText(formatSeconds(elapsed), juce::dontSendNotification);
    totalLabel_.setText(formatSeconds(total),     juce::dontSendNotification);

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
    // Speaker/mute click works regardless of whether a track is loaded.
    if (!speakerBounds_.isEmpty() && speakerBounds_.contains(e.x, e.y))
    {
        if (muted_)
        {
            // Unmute: restore the pre-mute volume.
            muted_ = false;
            volumeSlider_.setValue(premuteVolume_, juce::dontSendNotification);
            engine_.setVolume(static_cast<float>(premuteVolume_));
            refreshVolumeAlpha();
            repaint();
        }
        else if (volumeSlider_.getValue() > 0.0)
        {
            // Mute: remember the current slider value, then drop to 0.
            premuteVolume_ = volumeSlider_.getValue();
            muted_ = true;
            volumeSlider_.setValue(0.0, juce::dontSendNotification);
            engine_.setVolume(0.0f);
            refreshVolumeAlpha();
            repaint();
        }
        // else: slider is already at 0 manually - clicking speaker is a no-op.
        return;
    }

    if (!hasTrack_) return;

    // Expand the seek bar hit area vertically so the thumb (which extends
    // above and below the 6px track) is easy to grab.
    constexpr int seekHitPadY = 8;
    const auto seekHit = seekBarBounds_.expanded(0, seekHitPadY);
    if (seekHit.contains(e.x, e.y))
    {
        draggingSeek_ = true;
        seekDragPos_  = seekBarNormalizedX(e.x);
        repaint();
        return;
    }

    if (!sourceLinkBounds_.isEmpty() && sourceLinkBounds_.contains(e.x, e.y))
    {
        if (onPlayingFromClicked)
            onPlayingFromClicked(playingFromSidebarId_);
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

juce::MouseCursor TransportBar::getMouseCursor()
{
    if (draggingSeek_)
        return juce::MouseCursor::PointingHandCursor;

    const auto pos = getMouseXYRelative();

    // Seek-bar hit area: matches the expanded hit region used in mouseDown.
    if (! seekBarBounds_.isEmpty()
        && seekBarBounds_.expanded(0, 8).contains(pos))
        return juce::MouseCursor::PointingHandCursor;

    // Speaker icon.
    if (! speakerBounds_.isEmpty() && speakerBounds_.contains(pos))
        return juce::MouseCursor::PointingHandCursor;

    // "Playing from: <source>" clickable link.
    if (! sourceLinkBounds_.isEmpty() && sourceLinkBounds_.contains(pos))
        return juce::MouseCursor::PointingHandCursor;

    return juce::Component::getMouseCursor();
}

double TransportBar::seekBarNormalizedX(int x) const
{
    const int left  = seekBarBounds_.getX();
    const int width = seekBarBounds_.getWidth();
    if (width <= 0) return 0.0;
    return juce::jlimit(0.0, 1.0, static_cast<double>(x - left) / width);
}

} // namespace FoxPlayer
