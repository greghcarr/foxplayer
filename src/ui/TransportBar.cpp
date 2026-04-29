#include "TransportBar.h"
#include "Constants.h"
#include "Fonts.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int   modBtnD    = 28;   // hit area for shuffle/repeat floating icons
static constexpr int   skipBtnD   = 38;   // diameter of prev/next circles
static constexpr int   playBtnD   = 46;   // diameter of play/pause circle
static constexpr int   seekH      = 6;
static constexpr int   pad        = 10;
static constexpr int   artMaxFull        = 64;    // record diameter in pixels
static constexpr float cdDataInnerRatio  = 0.43f;  // CD hub ring outer radius as fraction of disc radius
static constexpr float cdHoleRatio       = 0.12f;  // CD spindle hole radius as fraction of disc radius
static constexpr float infoLetterSpacing = 0.2f;   // extra px between glyphs in info text

// Forward decl: defined later in this file, used inside TransportBar::paint().
static void drawCachedGlyphs(juce::Graphics& g,
                              const juce::GlyphArrangement& glyphs,
                              int x, int y, int w, int h);

// ----------------------------------------------------------------------------
// TransportButton
// ----------------------------------------------------------------------------

void TransportButton::paint(juce::Graphics& g)
{
    if (!isEnabled())
        g.beginTransparencyLayer(0.35f);

    const float w  = static_cast<float>(getWidth());
    const float h  = static_cast<float>(getHeight());
    const float d  = juce::jmin(w, h);
    const float cx = w * 0.5f;
    const float cy = h * 0.5f;
    const float r  = d * 0.5f;

    const bool isMod = (icon == Icon::Shuffle || icon == Icon::Repeat || icon == Icon::Pin);

    juce::Colour iconColor;
    if (isMod)
    {
        // Floating icon — no circle background.
        if (icon == Icon::Pin)
            iconColor = (toggleState == 0) ? juce::Colour(0xff606060) : juce::Colour(0xffcc3333);
        else
            iconColor = (toggleState == 0) ? juce::Colour(0xff606060) : Color::accent;
    }
    else
    {
        // Circle fill and icon color for non-mod buttons.
        juce::Colour fill;
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
            iconColor = Color::accent;
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
    }

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
        const float sizeRatio = isMod ? 1.0f : 0.52f;
        const float iconSize  = d * sizeRatio;
        svgCache_.drawable->drawWithin(
            g,
            juce::Rectangle<float>(cx - iconSize * 0.5f, cy - iconSize * 0.5f, iconSize, iconSize),
            juce::RectanglePlacement::centred | juce::RectanglePlacement::onlyReduceInSize,
            1.0f);
    }

    if (!isEnabled())
        g.endTransparencyLayer();
}

void TransportButton::mouseDown(const juce::MouseEvent&)
{
    if (!isEnabled()) return;
    pressed_ = true;
    repaint();
}

void TransportButton::mouseUp(const juce::MouseEvent& e)
{
    pressed_ = false;
    repaint();
    if (!isEnabled()) return;
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

// Draws a single line of text with uniform extra spacing between each glyph,
// vertically centred within the given (x, y, w, h) bounds, clipped to w.
static void drawTextSpaced(juce::Graphics& g,
                            const juce::Font& font,
                            const juce::String& text,
                            float letterSpacing,
                            int x, int y, int w, int h)
{
    const float baseline = static_cast<float>(y) + static_cast<float>(h) * 0.5f
                         + (font.getAscent() - font.getDescent()) * 0.5f;

    juce::GlyphArrangement ga;
    ga.addLineOfText(font, text, static_cast<float>(x), baseline);

    for (int i = 1; i < ga.getNumGlyphs(); ++i)
        ga.moveRangeOfGlyphs(i, ga.getNumGlyphs() - i, letterSpacing, 0.0f);

    g.saveState();
    g.reduceClipRegion(x, y, w, h);
    ga.draw(g);
    g.restoreState();
}

// Draws `text` in a circle centred at `centre`, at `radius` from it.
// Characters are individually positioned and rotated so they face outward;
// the whole string is centred at the 12 o'clock position.
// Text that would consume more than 85 % of the circumference is truncated with "...".
static void drawArcText(juce::Graphics& g,
                         const juce::String& text,
                         juce::Point<float> centre,
                         float radius,
                         const juce::Font& font)
{
    if (text.isEmpty()) return;

    const float maxWidth = juce::MathConstants<float>::twoPi * radius * 0.85f;

    auto glyphsWidth = [&](const juce::String& s) -> float
    {
        juce::GlyphArrangement tmp;
        tmp.addLineOfText(font, s, 0.0f, 0.0f);
        float w = 0.0f;
        for (int i = 0; i < tmp.getNumGlyphs(); ++i)
            w += tmp.getGlyph(i).getRight() - tmp.getGlyph(i).getLeft();
        return w;
    };

    juce::String displayText = text;
    if (glyphsWidth(text) > maxWidth)
    {
        const float ellipsisW = glyphsWidth("...");
        const float budget    = maxWidth - ellipsisW;

        juce::GlyphArrangement tmp;
        tmp.addLineOfText(font, text, 0.0f, 0.0f);
        float accumulated = 0.0f;
        int cutAt = 0;
        for (int i = 0; i < tmp.getNumGlyphs(); ++i)
        {
            const float gw = tmp.getGlyph(i).getRight() - tmp.getGlyph(i).getLeft();
            if (accumulated + gw > budget) break;
            accumulated += gw;
            ++cutAt;
        }
        displayText = text.substring(0, cutAt).trimEnd() + "...";
    }

    juce::GlyphArrangement ga;
    ga.addLineOfText(font, displayText, 0.0f, 0.0f);
    const int n = ga.getNumGlyphs();
    if (n == 0) return;

    float totalWidth = 0.0f;
    for (int i = 0; i < n; ++i)
        totalWidth += ga.getGlyph(i).getRight() - ga.getGlyph(i).getLeft();

    const float totalAngle = totalWidth / radius;
    float angle = -juce::MathConstants<float>::halfPi - totalAngle * 0.5f;
    const float halfH = (font.getAscent() - font.getDescent()) * 0.5f;

    for (int i = 0; i < n; ++i)
    {
        const auto& glyph    = ga.getGlyph(i);
        const float w        = glyph.getRight() - glyph.getLeft();
        const float midAngle = angle + (w * 0.5f) / radius;

        const float px = centre.x + radius * std::cos(midAngle);
        const float py = centre.y + radius * std::sin(midAngle);

        g.saveState();
        g.addTransform(
            juce::AffineTransform::translation(-(glyph.getLeft() + w * 0.5f), halfH)
            .followedBy(juce::AffineTransform::rotation(midAngle + juce::MathConstants<float>::halfPi))
            .followedBy(juce::AffineTransform::translation(px, py)));
        glyph.draw(g);
        g.restoreState();

        angle += w / radius;
    }
}

static void drawSpinningRecord(juce::Graphics& g,
                                juce::Rectangle<int> bounds,
                                float rotation,
                                const juce::Image& art,
                                const juce::String& labelText)
{
    const auto  discF  = bounds.toFloat();
    const auto  centre = discF.getCentre();
    const float R      = discF.getWidth() * 0.5f;

    const float dataInnerR = R * cdDataInnerRatio;
    const float holeR      = R * cdHoleRatio;

    const bool hasArt = art.isValid();

    // Drop shadow is now pre-rendered by the caller (TransportBar::paint) and
    // blitted from a cached Image — drawing it from scratch every paint via
    // juce::DropShadow::drawForPath was the single largest CPU sink.

    g.saveState();
    g.addTransform(juce::AffineTransform::rotation(rotation, centre.x, centre.y));

    if (hasArt)
    {
        // Album art fills the full disc.
        juce::Path discClip;
        discClip.addEllipse(discF);
        g.saveState();
        g.reduceClipRegion(discClip);
        g.drawImage(art,
                    juce::roundToInt(discF.getX()),
                    juce::roundToInt(discF.getY()),
                    juce::roundToInt(discF.getWidth()),
                    juce::roundToInt(discF.getHeight()),
                    0, 0, art.getWidth(), art.getHeight());
        g.restoreState();
    }
    else
    {
        // Thin silver outer rim.
        g.setColour(juce::Colour(0xffaaaaaa));
        g.fillEllipse(discF);

        // White label surface (inset 2.5 px to show the rim).
        const float labelR = R - 2.5f;
        g.setColour(juce::Colour(0xfff6f6f6));
        g.fillEllipse(centre.x - labelR, centre.y - labelR, labelR * 2.0f, labelR * 2.0f);

        // Text along the outer portion of the label.
        const float textRadius = R * 0.62f;
        const auto  tf         = getFoxwhelpTypeface();
        const juce::Font labelFont(tf);
        g.setColour(juce::Colour(0xff2c2c2c));
        drawArcText(g, labelText, centre, textRadius, labelFont.withHeight(R * 0.33f));

        // Hub ring (slightly grey to distinguish from the white label).
        g.setColour(juce::Colour(0xffd8d8d8));
        g.fillEllipse(centre.x - dataInnerR, centre.y - dataInnerR,
                      dataInnerR * 2.0f, dataInnerR * 2.0f);
        g.setColour(juce::Colour(0xffbbbbbb));
        g.drawEllipse(centre.x - dataInnerR, centre.y - dataInnerR,
                      dataInnerR * 2.0f, dataInnerR * 2.0f, 0.5f);
    }

    // Spindle hole.
    g.setColour(juce::Colour(0xff141414));
    g.fillEllipse(centre.x - holeR, centre.y - holeR, holeR * 2.0f, holeR * 2.0f);

    g.restoreState();
}

static void drawPodcastArt(juce::Graphics& g,
                            juce::Rectangle<int> bounds,
                            const juce::Image& art,
                            const juce::String& fallbackText)
{
    constexpr float cornerR = 6.0f;
    const auto boundsF = bounds.toFloat();

    // Drop shadow pre-rendered by TransportBar::paint, see ensureShadowImage().

    if (art.isValid())
    {
        juce::Path clip;
        clip.addRoundedRectangle(boundsF, cornerR);
        g.saveState();
        g.reduceClipRegion(clip);
        g.drawImage(art,
                    bounds.getX(), bounds.getY(),
                    bounds.getWidth(), bounds.getHeight(),
                    0, 0, art.getWidth(), art.getHeight());
        g.restoreState();
    }
    else
    {
        g.setColour(juce::Colour(0xff1e1e1e));
        g.fillRoundedRectangle(boundsF, cornerR);
        g.setColour(juce::Colour(0xff555555));
        g.drawRoundedRectangle(boundsF.reduced(0.5f), cornerR, 1.0f);

        if (fallbackText.isNotEmpty())
        {
            const auto tf = getFoxwhelpTypeface();
            g.setColour(juce::Colour(0xffaaaaaa));
            g.setFont(juce::Font(tf).withHeight(static_cast<float>(bounds.getHeight()) * 0.18f));
            g.drawText(fallbackText, bounds.reduced(4), juce::Justification::centred, true);
        }
    }
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

int TransportBar::DraggingDotSliderLnF::getSliderThumbRadius(juce::Slider& s)
{
    return juce::LookAndFeel_V4::getSliderThumbRadius(s) + (hovered ? 2 : 0);
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

void TransportBar::updateCurrentTrackInfo(const TrackInfo& updated)
{
    if (!hasTrack_ || currentTrack_.file != updated.file) return;
    currentTrack_ = updated;
    repaint();
}

void TransportBar::refreshAlbumArt()
{
    if (!hasTrack_) return;
    albumArt_ = AlbumArtExtractor::extractFromFile(currentTrack_.file);
    repaint();
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

void TransportBar::setInitialMute(bool muted, double premuteVolume)
{
    muted_         = muted;
    premuteVolume_ = juce::jlimit(0.0, 1.0, premuteVolume);
    if (muted_)
    {
        volumeSlider_.setValue(0.0, juce::dontSendNotification);
        engine_.setVolume(0.0f);
    }
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

void TransportBar::setCanGoPrev(bool can)
{
    prevButton_.setEnabled(can);
}

void TransportBar::setCanGoNext(bool can)
{
    nextButton_.setEnabled(can);
}

void TransportBar::setPlayingFrom(const juce::String& sourceName, int sourceSidebarId)
{
    playingFromName_      = sourceName;
    playingFromSidebarId_ = sourceSidebarId;
    repaint();
}

void TransportBar::clearTrack()
{
    hasTrack_  = false;
    albumArt_  = {};
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

    // Glyph-arrangement caching: build once per track/source change instead
    // of re-shaping every paint via juce::ShapedText (was the dominant CPU
    // cost after the drop-shadow fix).
    ensureInfoTextLayout();

    // Title rendering depends on whether there's a real title tag or we're
    // falling back to the filename. Real title -> italic, tag text only.
    // Filename fallback -> regular weight, include the file extension.
    const bool         hasRealTitle  = currentTrack_.title.isNotEmpty();
    const juce::String titleText     = hasRealTitle
                                           ? currentTrack_.title
                                           : currentTrack_.file.getFileName();

    const juce::Font titleFont    = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(18.0f).withStyle("Bold"));
    const juce::Font artistFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f));
    const juce::Font noArtistFont = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f)).italicised();
    const juce::Font prefixFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f)).italicised();
    const juce::Font sourceFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f));

    int infoMaxTextW = 0;
    int prefixTextW  = infoTextLayout_.prefixWidthSpaced;
    int sourceTextW  = infoTextLayout_.sourceWidthSpaced;
    if (hasTrack_ && !infoAreaBounds_.isEmpty())
    {
        infoMaxTextW = infoTextLayout_.titleWidthUnspaced;
        const bool noArtist = currentTrack_.artist.isEmpty();
        infoMaxTextW = juce::jmax(infoMaxTextW,
            noArtist ? infoTextLayout_.noArtistWidthUnspaced
                     : infoTextLayout_.artistWidthUnspaced);
        if (playingFromName_.isNotEmpty())
            infoMaxTextW = juce::jmax(infoMaxTextW, prefixTextW + sourceTextW);
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

    // Album art / CD — podcasts get a stationary rounded square, music gets the spinning CD.
    if (!albumArtBounds_.isEmpty() && hasTrack_)
    {
        // Pre-rendered drop shadow, blitted in one cheap image draw rather
        // than re-rasterising a box-blurred shadow each frame.
        ensureShadowImage(albumArtBounds_, currentTrack_.isPodcast);
        if (shadowImage_.isValid())
            g.drawImageAt(shadowImage_,
                          albumArtBounds_.getX() - shadowMargin,
                          albumArtBounds_.getY() - shadowMargin);

        if (currentTrack_.isPodcast)
        {
            const juce::String fallback = currentTrack_.podcast.isNotEmpty()
                                              ? currentTrack_.podcast
                                              : currentTrack_.displayTitle();
            drawPodcastArt(g, albumArtBounds_, albumArt_, fallback);
        }
        else
        {
            juce::String labelText;
            if (currentTrack_.artist.isNotEmpty() || currentTrack_.displayTitle().isNotEmpty())
            {
                if (currentTrack_.artist.isNotEmpty() && currentTrack_.displayTitle().isNotEmpty())
                    labelText = currentTrack_.artist
                                + juce::String(juce::CharPointer_UTF8(" \xc2\xb7 "))
                                + currentTrack_.displayTitle();
                else
                    labelText = currentTrack_.artist.isNotEmpty() ? currentTrack_.artist
                                                                   : currentTrack_.displayTitle();
            }
            else
            {
                labelText = currentTrack_.file.getFileNameWithoutExtension();
            }
            drawSpinningRecord(g, albumArtBounds_, recordRotation_, albumArt_, labelText);
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

        // Gradient clip: link bounds are trimmed at gradStart so clicks and
        // underlines don't extend into the faded-out zone.
        const int gradStart = gradValid ? static_cast<int>(gradLeftTransparent)
                                        : (infoX + infoW);

        // Line 1: song title. Nudged up 2px so there's a touch more breathing
        // room above the artist line.
        g.setColour(Color::textPrimary.withMultipliedAlpha(infoAlpha));
        drawCachedGlyphs(g, infoTextLayout_.titleGlyphs,
                         infoX, infoY - 2, infoW, line1H);

        // Clickable title bounds (clipped to the gradient fade point).
        {
            const int titleTextW   = infoTextLayout_.titleWidthSpaced;
            const int rawRight     = infoX + juce::jmin(titleTextW, infoW);
            const int clippedRight = juce::jmin(rawRight, gradStart);
            titleLinkBounds_ = (clippedRight > infoX)
                                   ? juce::Rectangle<int>(infoX, infoY - 2, clippedRight - infoX, line1H)
                                   : juce::Rectangle<int>();
        }

        if (hoveredTitle_ && !titleLinkBounds_.isEmpty())
        {
            g.setColour(Color::textPrimary.withMultipliedAlpha(infoAlpha));
            g.drawHorizontalLine(titleLinkBounds_.getBottom() - 1,
                                 static_cast<float>(titleLinkBounds_.getX()),
                                 static_cast<float>(titleLinkBounds_.getRight()));
        }

        // Line 2: podcast name (for podcasts), artist, or "(no artist)" in italics
        const int artist2Y = infoY + line1H + lineGap;
        const juce::String line2Text = currentTrack_.isPodcast
                                           ? currentTrack_.podcast
                                           : currentTrack_.artist;
        const bool noArtist = line2Text.isEmpty();
        g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
        if (noArtist)
        {
            g.setFont(noArtistFont);
            g.drawText("(no artist)", infoX, artist2Y, infoW, line2H,
                       juce::Justification::centredLeft, true);
            artistLinkBounds_ = {};
        }
        else
        {
            drawCachedGlyphs(g, infoTextLayout_.artistGlyphs,
                             infoX, artist2Y, infoW, line2H);

            // Clickable artist bounds (clipped to the gradient fade point).
            const int artistTextW  = infoTextLayout_.artistWidthSpaced;
            const int rawRight2    = infoX + juce::jmin(artistTextW, infoW);
            const int clippedRight2 = juce::jmin(rawRight2, gradStart);
            artistLinkBounds_ = (clippedRight2 > infoX)
                                     ? juce::Rectangle<int>(infoX, artist2Y, clippedRight2 - infoX, line2H)
                                     : juce::Rectangle<int>();

            if (hoveredArtist_ && !artistLinkBounds_.isEmpty())
            {
                g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
                g.drawHorizontalLine(artistLinkBounds_.getBottom() - 1,
                                     static_cast<float>(artistLinkBounds_.getX()),
                                     static_cast<float>(artistLinkBounds_.getRight()));
            }
        }

        // Line 3: "Playing from: [source]" — both prefix and link in textDim,
        // underline appears on hover only. Nudged down 2px for separation.
        if (playingFromName_.isNotEmpty())
        {
            const int source3Y = artist2Y + line2H + lineGap + 2;

            g.setColour(Color::textDim.withMultipliedAlpha(infoAlpha));
            drawCachedGlyphs(g, infoTextLayout_.prefixGlyphs,
                             infoX, source3Y, prefixTextW + 2, line3H);

            const int linkX = infoX + prefixTextW;
            g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
            drawCachedGlyphs(g, infoTextLayout_.sourceGlyphs,
                             linkX, source3Y, infoW - prefixTextW, line3H);

            // Click hit-box: only cover the visible portion of the link.
            const int linkRight = juce::jmin(linkX + sourceTextW + 4,
                                             infoX + (infoW - prefixTextW),
                                             gradStart);
            const int clippedW  = linkRight - linkX;
            sourceLinkBounds_ = (clippedW > 0)
                                    ? juce::Rectangle<int>(linkX, source3Y, clippedW, line3H)
                                    : juce::Rectangle<int>();

            if (hoveredSource_ && !sourceLinkBounds_.isEmpty())
            {
                g.setColour(Color::textSecondary.withMultipliedAlpha(infoAlpha));
                g.drawHorizontalLine(source3Y + line3H - 1,
                                     static_cast<float>(linkX),
                                     static_cast<float>(linkX + sourceTextW));
            }
        }
        else
        {
            sourceLinkBounds_ = {};
        }
    }
    else
    {
        titleLinkBounds_  = {};
        artistLinkBounds_ = {};
        sourceLinkBounds_ = {};
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
        const float thumbD = (draggingSeek_ || hoveredSeek_) ? 16.0f : 12.0f;
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
        const juce::String line2Name = currentTrack_.isPodcast
                                          ? currentTrack_.podcast
                                          : currentTrack_.artist;
        const juce::String artistPrefix = line2Name.isNotEmpty()
                                              ? line2Name + " - "
                                              : juce::String();

        const juce::Font compactRegular(juce::FontOptions().withHeight(15.0f));
        const juce::Font compactTitleFont = compactRegular;

        // Use spaced widths for positioning so the " - " dash is never clipped
        // and the title starts immediately after the artist prefix.
        auto spacedTextWidth = [](const juce::Font& f, const juce::String& text, float sp) -> int {
            juce::GlyphArrangement ga;
            ga.addLineOfText(f, text, 0.0f, 0.0f);
            for (int i = 1; i < ga.getNumGlyphs(); ++i)
                ga.moveRangeOfGlyphs(i, ga.getNumGlyphs() - i, sp, 0.0f);
            return static_cast<int>(std::ceil(ga.getBoundingBox(0, -1, true).getWidth()));
        };

        const int prefixW = artistPrefix.isNotEmpty()
                                ? spacedTextWidth(compactRegular, artistPrefix, infoLetterSpacing) : 0;
        const int titleW  = spacedTextWidth(compactTitleFont, titleText, infoLetterSpacing);
        const int totalW  = prefixW + titleW;

        const juce::Colour compactColor = Color::textPrimary.withMultipliedAlpha(compactAlpha);

        auto drawAt = [&](int startX) {
            const int y = compactInfoBounds_.getY();
            const int h = compactInfoBounds_.getHeight();
            g.setColour(compactColor);
            if (prefixW > 0)
                drawTextSpaced(g, compactRegular, artistPrefix, infoLetterSpacing,
                               startX, y, prefixW + 4, h);
            drawTextSpaced(g, compactTitleFont, titleText, infoLetterSpacing,
                           startX + prefixW, y, titleW + 4, h);
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
    constexpr int   playbackRowYOffsetMax   = 13;
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

    const int artDim = artMaxFull;

    // Show the record whenever a track is loaded, regardless of whether it has
    // embedded album art (the label falls back to a dark placeholder).
    if (hasTrack_)
    {
        const int artY = (getHeight() - artDim) / 2;
        albumArtBounds_ = { artY, artY, artDim, artDim };  // left gap == vertical gap
    }
    else
    {
        albumArtBounds_ = {};
    }

    // Push the info-text area in slightly so the title/artist/source lines
    // don't crowd whatever sits to their left (album art, or the window edge
    // if the art is hidden). Mini mode doesn't use infoAreaBounds_.
    // Gap from CD right edge to info text equals the gap from window left to CD left,
    // so the info text appears equidistant from the CD as the CD is from the edge.
    const int infoTextLeftGap = albumArtBounds_.isEmpty() ? 13 : albumArtBounds_.getY();
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
        const int seekRowH      = 20;
        const int gapH          = 3;
        const int seekRowYNudge = static_cast<int>(playbackOffsetT * 2);
        const int seekRowY      = btnCenterY + playBtnD / 2 + gapH + seekRowYNudge;

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

    repaint();
}

// Builds a juce::GlyphArrangement for `text` in `font`, positioned with its
// baseline at y = (ascent - descent) / 2 (so a translation of y + h/2 at draw
// time produces vertically-centred text inside (x, y, w, h)). Applies the
// same letter-spacing pass as drawTextSpaced. Returns the resulting bounding
// width including the spacing offsets.
static int buildSpacedGlyphs(juce::GlyphArrangement& out,
                              const juce::Font& font,
                              const juce::String& text,
                              float letterSpacing)
{
    out.clear();
    if (text.isEmpty()) return 0;

    const float baseline = (font.getAscent() - font.getDescent()) * 0.5f;
    out.addLineOfText(font, text, 0.0f, baseline);

    if (letterSpacing != 0.0f)
        for (int i = 1; i < out.getNumGlyphs(); ++i)
            out.moveRangeOfGlyphs(i, out.getNumGlyphs() - i, letterSpacing, 0.0f);

    return static_cast<int>(std::ceil(out.getBoundingBox(0, -1, true).getWidth()));
}

// Draws a cached glyph arrangement (built by buildSpacedGlyphs) at vertical
// centre of (x, y, w, h), clipped to that rect. Avoids the expensive
// re-shape (juce::ShapedText) that every paint would otherwise do.
static void drawCachedGlyphs(juce::Graphics& g,
                              const juce::GlyphArrangement& glyphs,
                              int x, int y, int w, int h)
{
    if (glyphs.getNumGlyphs() == 0) return;

    g.saveState();
    g.reduceClipRegion(x, y, w, h);
    g.addTransform(juce::AffineTransform::translation(static_cast<float>(x),
                                                       static_cast<float>(y) + static_cast<float>(h) * 0.5f));
    glyphs.draw(g);
    g.restoreState();
}

void TransportBar::ensureInfoTextLayout()
{
    auto& L = infoTextLayout_;

    const juce::String title           = currentTrack_.title;
    const juce::String artist          = currentTrack_.artist;
    const juce::String podcast         = currentTrack_.podcast;
    const juce::String fromName        = playingFromName_;
    const bool         isPodcast       = currentTrack_.isPodcast;
    const bool         hasRealTitle    = title.isNotEmpty();

    const juce::String displayTitle    = hasRealTitle ? title : currentTrack_.file.getFileName();
    const juce::String line2Text       = isPodcast ? podcast : artist;

    if (L.valid
        && L.title           == title
        && L.artist          == artist
        && L.podcast         == podcast
        && L.playingFromName == fromName
        && L.isPodcast       == isPodcast
        && L.hasRealTitle    == hasRealTitle)
        return;  // cache hit

    L.title           = title;
    L.artist          = artist;
    L.podcast         = podcast;
    L.playingFromName = fromName;
    L.isPodcast       = isPodcast;
    L.hasRealTitle    = hasRealTitle;

    const juce::Font titleFont    = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(18.0f).withStyle("Bold"));
    const juce::Font artistFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f));
    const juce::Font noArtistFont = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f)).italicised();
    const juce::Font prefixFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f)).italicised();
    const juce::Font sourceFont   = juce::Font(juce::FontOptions().withName("Helvetica Neue").withHeight(16.0f));

    // Title: cache spaced version (used for both drawing and hit-box width).
    L.titleWidthSpaced = buildSpacedGlyphs(L.titleGlyphs, titleFont, displayTitle, infoLetterSpacing);
    {
        juce::GlyphArrangement tmp;
        L.titleWidthUnspaced = buildSpacedGlyphs(tmp, titleFont, displayTitle, 0.0f);
    }

    // Line 2: artist or podcast (or "(no artist)" italics for empty).
    if (line2Text.isEmpty())
    {
        L.artistGlyphs.clear();
        L.artistWidthSpaced   = 0;
        L.artistWidthUnspaced = 0;
        juce::GlyphArrangement tmp;
        L.noArtistWidthUnspaced = buildSpacedGlyphs(tmp, noArtistFont, "(no artist)", 0.0f);
    }
    else
    {
        L.artistWidthSpaced = buildSpacedGlyphs(L.artistGlyphs, artistFont, line2Text, infoLetterSpacing);
        juce::GlyphArrangement tmp;
        L.artistWidthUnspaced  = buildSpacedGlyphs(tmp, artistFont, line2Text, 0.0f);
        L.noArtistWidthUnspaced = 0;
    }

    // "Playing from: <source>" line.
    if (fromName.isNotEmpty())
    {
        L.prefixWidthSpaced = buildSpacedGlyphs(L.prefixGlyphs, prefixFont, "Playing from: ", infoLetterSpacing);
        L.sourceWidthSpaced = buildSpacedGlyphs(L.sourceGlyphs, sourceFont, fromName,        infoLetterSpacing);
    }
    else
    {
        L.prefixGlyphs.clear();
        L.sourceGlyphs.clear();
        L.prefixWidthSpaced = 0;
        L.sourceWidthSpaced = 0;
    }

    L.valid = true;
}

void TransportBar::ensureShadowImage(juce::Rectangle<int> artBounds, bool isPodcast)
{
    const int w   = artBounds.getWidth();
    const int h   = artBounds.getHeight();
    const bool cd = ! isPodcast;

    if (shadowImage_.isValid()
        && shadowImageW_ == w
        && shadowImageH_ == h
        && shadowImageIsCD_ == cd)
        return;

    shadowImageW_    = w;
    shadowImageH_    = h;
    shadowImageIsCD_ = cd;

    if (w <= 0 || h <= 0)
    {
        shadowImage_ = {};
        return;
    }

    // Image has shadowMargin px of padding on every side so the blurred edges
    // have room to fall off without being clipped.
    const int imgW = w + shadowMargin * 2;
    const int imgH = h + shadowMargin * 2;
    juce::Image img(juce::Image::ARGB, imgW, imgH, true);

    juce::Graphics g(img);

    juce::Path shape;
    const auto shapeBounds = juce::Rectangle<float>(
        static_cast<float>(shadowMargin),
        static_cast<float>(shadowMargin),
        static_cast<float>(w),
        static_cast<float>(h));
    if (cd)
        shape.addEllipse(shapeBounds);
    else
        shape.addRoundedRectangle(shapeBounds, 6.0f);

    juce::DropShadow(juce::Colours::black.withAlpha(0.85f),
                     shadowRadius, {}).drawForPath(g, shape);

    shadowImage_ = std::move(img);
}

void TransportBar::timerCallback()
{
    // Poll hover state for clickable info text. Runs at 30 Hz which is
    // responsive enough and avoids mouse-listener plumbing for child components.
    const auto mousePos = getMouseXYRelative();
    const bool newHoveredTitle  = !titleLinkBounds_.isEmpty()  && titleLinkBounds_.contains(mousePos);
    const bool newHoveredArtist = !artistLinkBounds_.isEmpty() && artistLinkBounds_.contains(mousePos);
    const bool newHoveredSource = !sourceLinkBounds_.isEmpty() && sourceLinkBounds_.contains(mousePos);
    const bool newHoveredSeek   = !draggingSeek_ && !seekBarBounds_.isEmpty()
                                    && seekBarBounds_.expanded(0, 8).contains(mousePos);
    const bool newHoveredVolume = volumeSlider_.getBounds().contains(mousePos);
    if (newHoveredTitle  != hoveredTitle_  ||
        newHoveredArtist != hoveredArtist_ ||
        newHoveredSource != hoveredSource_ ||
        newHoveredSeek   != hoveredSeek_   ||
        newHoveredVolume != hoveredVolume_)
    {
        hoveredTitle_  = newHoveredTitle;
        hoveredArtist_ = newHoveredArtist;
        hoveredSource_ = newHoveredSource;
        hoveredSeek_   = newHoveredSeek;
        hoveredVolume_ = newHoveredVolume;
        if (volumeSliderLnF_.hovered != newHoveredVolume)
        {
            volumeSliderLnF_.hovered = newHoveredVolume;
            volumeSlider_.repaint();
        }
        repaint();
    }
    updateDisplay();
}

void TransportBar::updateDisplay()
{
    if (!hasTrack_) return;

    playPauseButton_.icon = engine_.isPlaying() ? TransportButton::Icon::Pause
                                                : TransportButton::Icon::Play;
    playPauseButton_.repaint();

    // Advance rotation by actual elapsed wall-clock time since the last tick.
    // Using real time instead of a fixed per-frame amount means paint() always
    // has an accurate sync point, so the CD keeps spinning even during window
    // resize (when the timer stops firing but resized() still triggers repaints).
    const double nowMs = juce::Time::getMillisecondCounterHiRes();
    if (engine_.isPlaying() && lastUpdateMs_ > 0.0 && !currentTrack_.isPodcast)
    {
        const float deltaRads = static_cast<float>((nowMs - lastUpdateMs_) / 1000.0)
                                * (33.333f / 60.0f) * juce::MathConstants<float>::twoPi;
        recordRotation_ = std::fmod(recordRotation_ + deltaRads,
                                    juce::MathConstants<float>::twoPi);
    }
    lastUpdateMs_ = nowMs;

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
            if (onMuteChanged) onMuteChanged(false, premuteVolume_);
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
            if (onMuteChanged) onMuteChanged(true, premuteVolume_);
        }
        // else: slider is already at 0 manually - clicking speaker is a no-op.
        return;
    }

    if (!hasTrack_) return;

    if (!titleLinkBounds_.isEmpty() && titleLinkBounds_.contains(e.x, e.y))
    {
        if (onTitleClicked) onTitleClicked(playingFromSidebarId_);
        return;
    }

    if (!artistLinkBounds_.isEmpty() && artistLinkBounds_.contains(e.x, e.y))
    {
        if (onArtistClicked) onArtistClicked(currentTrack_);
        return;
    }

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

    // Clickable title and artist text.
    if (! titleLinkBounds_.isEmpty()  && titleLinkBounds_.contains(pos))
        return juce::MouseCursor::PointingHandCursor;
    if (! artistLinkBounds_.isEmpty() && artistLinkBounds_.contains(pos))
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
