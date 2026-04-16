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
static constexpr float fadeRadius  = 260.0f; // horizontal extent of title-fade gradient

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

    // Circle fill and icon color depend on toggle state.
    juce::Colour fill, iconColor;
    if (toggleState > 0)
    {
        fill      = pressed_ ? juce::Colour(0xff707070)
                  : hovered_ ? juce::Colour(0xffb0b0b0)
                  :             juce::Colour(0xff989898);
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

TransportBar::TransportBar(AudioEngine& engine)
    : engine_(engine)
{
    shuffleButton_.icon   = TransportButton::Icon::Shuffle;
    prevButton_.icon      = TransportButton::Icon::Prev;
    playPauseButton_.icon = TransportButton::Icon::Play;
    nextButton_.icon      = TransportButton::Icon::Next;
    repeatButton_.icon    = TransportButton::Icon::Repeat;

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
    volumeSlider_.onValueChange = [this] {
        if (muted_)
        {
            muted_ = false;
            repaint();
        }
        engine_.setVolume(static_cast<float>(volumeSlider_.getValue()));
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
        lbl->setFont(juce::Font(juce::FontOptions().withHeight(11.0f)));
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
}

void TransportBar::setCurrentTrack(const TrackInfo& track)
{
    currentTrack_ = track;
    hasTrack_ = true;
    albumArt_ = AlbumArtExtractor::extractFromFile(track.file);
    resized();
    updateDisplay();
}

void TransportBar::setShuffleOn(bool on)
{
    shuffleButton_.toggleState = on ? 1 : 0;
    shuffleButton_.repaint();
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

    if (hasTrack_)
    {
        // Seek bar track + fill
        g.setColour(Color::seekBarTrack);
        g.fillRect(seekBarBounds_);
        const double pos = draggingSeek_ ? seekDragPos_ : engine_.normalizedPosition();
        const int fillW = static_cast<int>(pos * seekBarBounds_.getWidth());

        g.setColour(Color::seekBarFill);
        g.fillRect(seekBarBounds_.withWidth(fillW));

        // Seek thumb: white circle at current position, enlarges while dragging.
        const auto  seekF  = seekBarBounds_.toFloat();
        const float thumbD = draggingSeek_ ? 16.0f : 12.0f;
        const float thumbX = seekF.getX() + static_cast<float>(pos) * seekF.getWidth();
        const float thumbY = seekF.getCentreY();
        g.setColour(juce::Colours::white);
        g.fillEllipse(thumbX - thumbD * 0.5f, thumbY - thumbD * 0.5f, thumbD, thumbD);
    }

    // Album art (or placeholder when track loaded but no art). Stays fully
    // opaque - resized() shrinks it to fit rather than fading it out.
    if (!albumArtBounds_.isEmpty())
    {
        if (albumArt_.isValid())
        {
            g.drawImageWithin(albumArt_,
                              albumArtBounds_.getX(), albumArtBounds_.getY(),
                              albumArtBounds_.getWidth(), albumArtBounds_.getHeight(),
                              juce::RectanglePlacement::centred |
                              juce::RectanglePlacement::onlyReduceInSize);
        }
        else if (hasTrack_)
        {
            const auto r = albumArtBounds_.toFloat();
            g.setColour(juce::Colour(0xff2a2a2a));
            g.fillRect(r);
            g.setColour(juce::Colour(0xff555555));
            g.drawRect(r, 1.0f);
            const float inset = r.getWidth() * 0.25f;
            g.drawLine(r.getX() + inset, r.getY() + inset,
                       r.getRight() - inset, r.getBottom() - inset, 1.5f);
            g.drawLine(r.getRight() - inset, r.getY() + inset,
                       r.getX() + inset, r.getBottom() - inset, 1.5f);
        }
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

        // Helper: measure text width with a given font.
        auto textWidth = [](const juce::Font& f, const juce::String& text) -> int {
            juce::GlyphArrangement ga;
            ga.addLineOfText(f, text, 0.0f, 0.0f);
            return static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth()));
        };

        // Line 1: song title (italic)
        const juce::Font titleFont = juce::Font(juce::FontOptions().withHeight(15.0f)).italicised();
        g.setColour(Color::textPrimary.withMultipliedAlpha(infoAlpha));
        g.setFont(titleFont);
        g.drawText(currentTrack_.displayTitle(), infoX, infoY, infoW, line1H,
                   juce::Justification::centredLeft, true);

        // Line 2: artist, or "(no artist)" in italics
        const int artist2Y = infoY + line1H + lineGap;
        const bool noArtist = currentTrack_.artist.isEmpty();
        g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
        if (noArtist)
        {
            g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)).italicised());
            g.drawText("(no artist)", infoX, artist2Y, infoW, line2H,
                       juce::Justification::centredLeft, true);
        }
        else
        {
            g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
            g.drawText(currentTrack_.artist, infoX, artist2Y, infoW, line2H,
                       juce::Justification::centredLeft, true);
        }

        // Line 3: "Playing from: [source]" where source is accent-colored and clickable
        if (playingFromName_.isNotEmpty())
        {
            const int source3Y = artist2Y + line2H + lineGap;
            const juce::Font sourceFont(juce::FontOptions().withHeight(13.0f));
            g.setFont(sourceFont);

            const juce::String prefix = "Playing from: ";
            const juce::Font prefixFont = juce::Font(juce::FontOptions().withHeight(13.0f)).italicised();
            const int prefixW = textWidth(prefixFont, prefix);

            g.setColour(Color::textDim.withMultipliedAlpha(infoAlpha));
            g.setFont(prefixFont);
            g.drawText(prefix, infoX, source3Y, prefixW + 2, line3H,
                       juce::Justification::centredLeft, false);
            g.setFont(sourceFont);

            const int linkX = infoX + prefixW;
            const int linkW = textWidth(sourceFont, playingFromName_);
            g.setColour(Color::accent.withMultipliedAlpha(infoAlpha));
            g.drawText(playingFromName_, linkX, source3Y, infoW - prefixW, line3H,
                       juce::Justification::centredLeft, false);

            // Underline the source link
            g.drawHorizontalLine(source3Y + line3H - 1,
                                 static_cast<float>(linkX),
                                 static_cast<float>(linkX + linkW));

            // Click hit-box: only cover the visible portion of the link. Once
            // the gradient starts fading the text out, the hidden tail stops
            // being clickable so we don't "fight" the visual effect.
            const int gradStart = static_cast<int>(
                playPauseButton_.getBoundsInParent().toFloat().getCentreX() - fadeRadius);
            const int linkRight = juce::jmin(linkX + linkW + 4,
                                             infoX + (infoW - prefixW),
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

    // Horizontal fade centred on the play button: opaque transport-bg at the
    // centre, falling off to fully transparent at the edges. Paints over the
    // info text above, so long titles dissolve into the background as they
    // approach the middle button cluster. Clipped vertically to the upper
    // band so it doesn't cover the seek bar or thumb beneath the buttons.
    if (hasTrack_)
    {
        const float cx = playPauseButton_.getBoundsInParent().toFloat().getCentreX();

        // Cap the radius so the gradient fully fades out before it reaches the
        // right edge of the album art (plus a small gap). Symmetric on the
        // right so the fade still looks balanced around the play button.
        float effectiveRadius = fadeRadius;
        if (!albumArtBounds_.isEmpty())
        {
            const float safeGap   = 6.0f;
            const float maxRadius = cx - (static_cast<float>(albumArtBounds_.getRight()) + safeGap);
            effectiveRadius = juce::jmin(effectiveRadius, juce::jmax(0.0f, maxRadius));
        }

        const float left       = cx - effectiveRadius;
        const float fadeBottom = seekBarBounds_.isEmpty()
                                    ? static_cast<float>(getHeight())
                                    : static_cast<float>(seekBarBounds_.getY()) - 4.0f;

        juce::ColourGradient grad(
            Color::transportBg.withAlpha(0.0f), left,                      0.0f,
            Color::transportBg.withAlpha(0.0f), cx + effectiveRadius,      0.0f,
            false);
        grad.addColour(0.5, Color::transportBg);
        g.setGradientFill(grad);
        g.fillRect(juce::Rectangle<float>(left, 0.0f, effectiveRadius * 2.0f, fadeBottom));
    }

    // Mini-mode "Artist - Title" (or filename) line above the buttons. Drawn
    // after the gradient so it stays fully legible rather than being faded out.
    if (hasTrack_ && !compactInfoBounds_.isEmpty())
    {
        const juce::String artist = currentTrack_.artist;
        const juce::String title  = currentTrack_.displayTitle();
        const juce::String label  = artist.isNotEmpty()
                                        ? artist + " - " + title
                                        : currentTrack_.file.getFileNameWithoutExtension();

        g.setColour(Color::textSecondary);
        g.setFont(juce::Font(juce::FontOptions().withHeight(13.0f)));
        g.drawText(label, compactInfoBounds_, juce::Justification::centred, true);
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
    const int btnGap     = 8;
    const int centerX    = getWidth() / 2;
    const int btnCenterY = bounds.getCentreY();
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

    // Album art: smoothly scales and repositions as the window shrinks, so
    // there's no jump at any threshold.
    //   Size  : from artMaxFull (80) at wide widths down to artMaxMini (44)
    //           at the mini-mode threshold, linearly interpolated. Further
    //           capped by the space between the left pad and shuffle button.
    //   Position: from flush-left at `pad` when the window is wide, to
    //             centred-between-the-left-edge-and-shuffle when narrow.
    constexpr int   artMaxFull = 80;
    constexpr int   artMaxMini = 44;
    constexpr int   artGap     = 6;
    constexpr float fadeFromW  = 540.0f;  // match the info-text fade range
    const float fadeToW = static_cast<float>(Constants::miniModeWidth);
    const float artT    = juce::jlimit(0.0f, 1.0f,
        (static_cast<float>(getWidth()) - fadeToW) / (fadeFromW - fadeToW));
    const int artCeiling  = artMaxMini + static_cast<int>(artT * (artMaxFull - artMaxMini));
    const int availForArt = x0 - pad - artGap;
    const int artDim      = juce::jlimit(0, artCeiling, availForArt);

    if (artDim > 0)
    {
        const int leftX     = pad;
        const int centeredX = (pad + x0 - artGap - artDim) / 2;
        const int artX      = leftX + static_cast<int>((1.0f - artT) * (centeredX - leftX));
        albumArtBounds_ = { artX, (getHeight() - artDim) / 2, artDim, artDim };
    }
    else
    {
        albumArtBounds_ = {};
    }

    // Remove the album-art column (plus gap) from the info-text working area
    // in full mode only (mini mode doesn't use infoAreaBounds_).
    if (!mini && !albumArtBounds_.isEmpty())
        bounds.removeFromLeft(albumArtBounds_.getRight() - bounds.getX() + 8);

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

    // Full-mode left-side info text vs. mini-mode centred "Artist - Title" line.
    if (mini)
    {
        infoAreaBounds_ = {};

        // Bounds are symmetric around the play button's centre so centred
        // text actually lands above it. Album art on the left and the volume
        // strip on the right each constrain how far we can extend; we take
        // whichever side is tighter and mirror it.
        const int compactH   = 20;
        const int compactY   = btnCenterY - playBtnD / 2 - compactH - 3;
        const int leftLimit  = albumArtBounds_.isEmpty() ? pad
                                                         : albumArtBounds_.getRight() + 6;
        const int rightLimit = speakerBounds_.isEmpty() ? getWidth() - pad
                                                        : speakerBounds_.getX() - 6;
        const int halfW      = juce::jmin(centerX - leftLimit, rightLimit - centerX);
        compactInfoBounds_   = { centerX - halfW, compactY, halfW * 2, compactH };
    }
    else
    {
        infoAreaBounds_    = bounds;
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
            repaint();
        }
        else if (volumeSlider_.getValue() > 0.0)
        {
            // Mute: remember the current slider value, then drop to 0.
            premuteVolume_ = volumeSlider_.getValue();
            muted_ = true;
            volumeSlider_.setValue(0.0, juce::dontSendNotification);
            engine_.setVolume(0.0f);
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

double TransportBar::seekBarNormalizedX(int x) const
{
    const int left  = seekBarBounds_.getX();
    const int width = seekBarBounds_.getWidth();
    if (width <= 0) return 0.0;
    return juce::jlimit(0.0, 1.0, static_cast<double>(x - left) / width);
}

} // namespace FoxPlayer
