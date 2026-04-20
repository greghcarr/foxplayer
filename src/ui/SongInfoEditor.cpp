#include "SongInfoEditor.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int dialogW      = 460;
static constexpr int labelW       = 76;
static constexpr int rowH         = 26;
static constexpr int rowGap       = 6;
static constexpr int pad          = 14;
static constexpr int btnW         = 80;
static constexpr int btnH         = 28;
static constexpr int halfFieldGap = 12;

SongInfoEditor::SongInfoEditor(const TrackInfo& track)
    : mode_(track.isPodcast ? Mode::SinglePodcast : Mode::SingleMusic)
    , tracks_({ track })
{
    init();
}

SongInfoEditor::SongInfoEditor(const std::vector<TrackInfo>& tracks)
    : mode_((!tracks.empty() && tracks.front().isPodcast) ? Mode::MultiPodcast : Mode::MultiMusic)
    , tracks_(tracks)
{
    init();
}

juce::String SongInfoEditor::findCommonPrefix() const
{
    if (tracks_.empty()) return {};
    juce::String prefix = tracks_[0].displayTitle();
    for (size_t i = 1; i < tracks_.size(); ++i)
    {
        const juce::String& t = tracks_[i].displayTitle();
        const int len = juce::jmin(prefix.length(), t.length());
        int j = 0;
        while (j < len && prefix[j] == t[j])
            ++j;
        prefix = prefix.substring(0, j);
        if (prefix.isEmpty()) return {};
    }
    return prefix;
}

void SongInfoEditor::init()
{
    int h = 302;
    switch (mode_)
    {
        case Mode::SingleMusic:   h = 302; break;
        case Mode::SinglePodcast: h = 222; break;
        case Mode::MultiMusic:    h = 254; break;
        case Mode::MultiPodcast:  h = 222; break;
    }
    setSize(dialogW, h);

    auto styleLabel = [](juce::Label& lbl) {
        lbl.setColour(juce::Label::textColourId, Color::textSecondary);
        lbl.setJustificationType(juce::Justification::centredRight);
    };

    auto styleEditor = [](juce::TextEditor& ed) {
        ed.setColour(juce::TextEditor::backgroundColourId, Color::headerBackground);
        ed.setColour(juce::TextEditor::textColourId,       Color::textPrimary);
        ed.setColour(juce::TextEditor::outlineColourId,    Color::border);
        ed.setColour(juce::TextEditor::focusedOutlineColourId, Color::accent);
        ed.setJustification(juce::Justification::centredLeft);
    };

    auto commonStr = [this](std::function<juce::String(const TrackInfo&)> fn) -> juce::String {
        if (tracks_.empty()) return {};
        juce::String first = fn(tracks_[0]);
        for (size_t i = 1; i < tracks_.size(); ++i)
            if (fn(tracks_[i]) != first) return {};
        return first;
    };

    if (mode_ == Mode::SingleMusic)
    {
        for (auto* lbl : { &titleLabel_, &artistLabel_, &albumLabel_,
                           &genreLabel_, &yearLabel_, &trackNumLabel_,
                           &bpmLabel_,   &keyLabel_ })
        {
            styleLabel(*lbl);
            addAndMakeVisible(*lbl);
        }
        for (auto* ed : { &titleEdit_, &artistEdit_, &albumEdit_,
                          &genreEdit_, &yearEdit_, &trackNumEdit_,
                          &bpmEdit_,   &keyEdit_ })
        {
            styleEditor(*ed);
            addAndMakeVisible(*ed);
        }

        titleEdit_.setText(tracks_[0].displayTitle(), false);
        artistEdit_.setText(tracks_[0].artist,        false);
        albumEdit_.setText(tracks_[0].album,          false);
        genreEdit_.setText(tracks_[0].genre,          false);
        yearEdit_.setText(tracks_[0].year,            false);
        trackNumEdit_.setText(tracks_[0].trackNumber > 0
                                  ? juce::String(tracks_[0].trackNumber)
                                  : juce::String(), false);
        bpmEdit_.setText(tracks_[0].bpm > 0.0
                             ? juce::String(tracks_[0].bpm, 1)
                             : juce::String(), false);
        keyEdit_.setText(tracks_[0].musicalKey, false);

        fileLabel_.setText(tracks_[0].file.getFullPathName(), juce::dontSendNotification);
        fileLabel_.setColour(juce::Label::textColourId, Color::textDim);
        fileLabel_.setMinimumHorizontalScale(0.5f);
        addAndMakeVisible(fileLabel_);
    }
    else if (mode_ == Mode::SinglePodcast)
    {
        for (auto* lbl : { &titleLabel_, &podcastLabel_, &genreLabel_,
                           &yearLabel_,  &episodeNumLabel_ })
        {
            styleLabel(*lbl);
            addAndMakeVisible(*lbl);
        }
        for (auto* ed : { &titleEdit_, &podcastEdit_, &genreEdit_,
                          &yearEdit_,  &episodeNumEdit_ })
        {
            styleEditor(*ed);
            addAndMakeVisible(*ed);
        }

        titleEdit_.setText(tracks_[0].displayTitle(), false);
        podcastEdit_.setText(tracks_[0].podcast,      false);
        genreEdit_.setText(tracks_[0].genre,          false);
        yearEdit_.setText(tracks_[0].year,            false);
        episodeNumEdit_.setText(tracks_[0].trackNumber > 0
                                    ? juce::String(tracks_[0].trackNumber)
                                    : juce::String(), false);

        fileLabel_.setText(tracks_[0].file.getFullPathName(), juce::dontSendNotification);
        fileLabel_.setColour(juce::Label::textColourId, Color::textDim);
        fileLabel_.setMinimumHorizontalScale(0.5f);
        addAndMakeVisible(fileLabel_);
    }
    else if (mode_ == Mode::MultiMusic)
    {
        detectedPrefix_ = findCommonPrefix();

        for (auto* lbl : { &titlePrefixLabel_, &artistLabel_, &albumLabel_,
                           &genreLabel_, &yearLabel_ })
        {
            styleLabel(*lbl);
            addAndMakeVisible(*lbl);
        }
        for (auto* ed : { &titleEdit_, &artistEdit_, &albumEdit_,
                          &genreEdit_, &yearEdit_ })
        {
            styleEditor(*ed);
            addAndMakeVisible(*ed);
        }

        titleEdit_.setText(detectedPrefix_, false);
        artistEdit_.setText(commonStr([](const TrackInfo& t) { return t.artist; }), false);
        albumEdit_.setText(commonStr([](const TrackInfo& t)  { return t.album;  }), false);
        genreEdit_.setText(commonStr([](const TrackInfo& t)  { return t.genre;  }), false);
        yearEdit_.setText(commonStr([](const TrackInfo& t)   { return t.year;   }), false);

        hintLabel_.setText("Leave a field blank to keep each track's existing value.",
                           juce::dontSendNotification);
        hintLabel_.setColour(juce::Label::textColourId, Color::textDim);
        hintLabel_.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(hintLabel_);
    }
    else // MultiPodcast
    {
        detectedPrefix_ = findCommonPrefix();

        for (auto* lbl : { &titlePrefixLabel_, &podcastLabel_,
                           &genreLabel_, &yearLabel_ })
        {
            styleLabel(*lbl);
            addAndMakeVisible(*lbl);
        }
        for (auto* ed : { &titleEdit_, &podcastEdit_,
                          &genreEdit_, &yearEdit_ })
        {
            styleEditor(*ed);
            addAndMakeVisible(*ed);
        }

        titleEdit_.setText(detectedPrefix_, false);
        podcastEdit_.setText(commonStr([](const TrackInfo& t) { return t.podcast; }), false);
        genreEdit_.setText(commonStr([](const TrackInfo& t)   { return t.genre;   }), false);
        yearEdit_.setText(commonStr([](const TrackInfo& t)    { return t.year;    }), false);

        hintLabel_.setText("Leave a field blank to keep each episode's existing value.",
                           juce::dontSendNotification);
        hintLabel_.setColour(juce::Label::textColourId, Color::textDim);
        hintLabel_.setFont(juce::Font(juce::FontOptions().withHeight(12.0f)));
        addAndMakeVisible(hintLabel_);
    }

    // Enter in any field commits the dialog.
    for (auto* comp : getChildren())
        if (auto* ed = dynamic_cast<juce::TextEditor*>(comp))
            ed->onReturnKey = [this] { save(); };

    // Focus the title field once the dialog is on screen.
    // Single modes: select all so the first keystroke replaces it.
    // Multi modes: cursor at end, nothing selected (prefix may be partial).
    const bool doSelectAll = (mode_ == Mode::SingleMusic || mode_ == Mode::SinglePodcast);
    juce::Component::SafePointer<SongInfoEditor> safeThis(this);
    juce::MessageManager::callAsync([safeThis, doSelectAll] {
        if (auto* self = safeThis.getComponent())
        {
            self->titleEdit_.grabKeyboardFocus();
            if (doSelectAll)
                self->titleEdit_.selectAll();
            else
                self->titleEdit_.moveCaretToEnd(false);
        }
    });

    saveButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    saveButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    saveButton_.onClick = [this] { save(); };
    addAndMakeVisible(saveButton_);

    cancelButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
    cancelButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    cancelButton_.onClick = [this] {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState(0);
    };
    addAndMakeVisible(cancelButton_);
}

void SongInfoEditor::save()
{
    if (mode_ == Mode::SingleMusic)
    {
        auto& t       = tracks_[0];
        t.title       = titleEdit_.getText().trim();
        t.artist      = artistEdit_.getText().trim();
        t.album       = albumEdit_.getText().trim();
        t.genre       = genreEdit_.getText().trim();
        t.year        = yearEdit_.getText().trim();
        t.trackNumber = trackNumEdit_.getText().trim().getIntValue();
        t.bpm         = bpmEdit_.getText().trim().getDoubleValue();
        t.musicalKey  = keyEdit_.getText().trim();
    }
    else if (mode_ == Mode::SinglePodcast)
    {
        auto& t       = tracks_[0];
        t.title       = titleEdit_.getText().trim();
        t.podcast     = podcastEdit_.getText().trim();
        t.genre       = genreEdit_.getText().trim();
        t.year        = yearEdit_.getText().trim();
        t.trackNumber = episodeNumEdit_.getText().trim().getIntValue();
    }
    else // multi-edit
    {
        const juce::String newPrefix  = titleEdit_.getText().trim();
        const bool         prefixChanged = (newPrefix != detectedPrefix_);

        const juce::String newArtist  = (mode_ == Mode::MultiMusic) ? artistEdit_.getText().trim() : juce::String();
        const juce::String newAlbum   = (mode_ == Mode::MultiMusic) ? albumEdit_.getText().trim()  : juce::String();
        const juce::String newPodcast = (mode_ == Mode::MultiPodcast) ? podcastEdit_.getText().trim() : juce::String();
        const juce::String newGenre   = genreEdit_.getText().trim();
        const juce::String newYear    = yearEdit_.getText().trim();

        for (auto& t : tracks_)
        {
            if (prefixChanged)
            {
                juce::String title = t.displayTitle();
                if (detectedPrefix_.isNotEmpty() && title.startsWith(detectedPrefix_))
                    title = newPrefix + title.substring(detectedPrefix_.length());
                else if (newPrefix.isNotEmpty())
                    title = newPrefix + title;
                t.title = title;
            }

            if (mode_ == Mode::MultiMusic)
            {
                if (newArtist.isNotEmpty()) t.artist = newArtist;
                if (newAlbum.isNotEmpty())  t.album  = newAlbum;
            }
            else
            {
                if (newPodcast.isNotEmpty()) t.podcast = newPodcast;
            }
            if (newGenre.isNotEmpty()) t.genre = newGenre;
            if (newYear.isNotEmpty())  t.year  = newYear;
        }
    }

    if (onSave) onSave(tracks_);

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
}

void SongInfoEditor::focusOfChildComponentChanged(juce::Component::FocusChangeType)
{
    for (auto* comp : getChildren())
        if (auto* ed = dynamic_cast<juce::TextEditor*>(comp))
            if (ed->hasKeyboardFocus(false))
                { ed->selectAll(); break; }
}

void SongInfoEditor::paint(juce::Graphics& g)
{
    g.fillAll(Color::background);
}

void SongInfoEditor::resized()
{
    auto bounds = getLocalBounds().reduced(pad, pad);

    auto nextRow = [&]() {
        auto row = bounds.removeFromTop(rowH);
        bounds.removeFromTop(rowGap);
        return row;
    };

    auto placeRow = [&](juce::Label& lbl, juce::TextEditor& ed) {
        auto row = nextRow();
        lbl.setBounds(row.removeFromLeft(labelW));
        row.removeFromLeft(8);
        ed.setBounds(row);
    };

    if (mode_ == Mode::SingleMusic)
    {
        placeRow(titleLabel_,  titleEdit_);
        placeRow(artistLabel_, artistEdit_);
        placeRow(albumLabel_,  albumEdit_);
        placeRow(genreLabel_,  genreEdit_);
        {
            auto row = nextRow();
            yearLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            yearEdit_.setBounds(row.removeFromLeft(80));
            row.removeFromLeft(halfFieldGap);
            trackNumLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            trackNumEdit_.setBounds(row.removeFromLeft(60));
        }
        {
            auto row = nextRow();
            bpmLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            bpmEdit_.setBounds(row.removeFromLeft(80));
            row.removeFromLeft(halfFieldGap);
            keyLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            keyEdit_.setBounds(row.removeFromLeft(60));
        }
        bounds.removeFromTop(4);
        fileLabel_.setBounds(bounds.removeFromTop(18));
    }
    else if (mode_ == Mode::SinglePodcast)
    {
        placeRow(titleLabel_,   titleEdit_);
        placeRow(podcastLabel_, podcastEdit_);
        placeRow(genreLabel_,   genreEdit_);
        {
            auto row = nextRow();
            yearLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            yearEdit_.setBounds(row.removeFromLeft(80));
            row.removeFromLeft(halfFieldGap);
            episodeNumLabel_.setBounds(row.removeFromLeft(labelW));
            row.removeFromLeft(8);
            episodeNumEdit_.setBounds(row.removeFromLeft(60));
        }
        bounds.removeFromTop(4);
        fileLabel_.setBounds(bounds.removeFromTop(18));
    }
    else if (mode_ == Mode::MultiMusic)
    {
        placeRow(titlePrefixLabel_, titleEdit_);
        placeRow(artistLabel_,      artistEdit_);
        placeRow(albumLabel_,       albumEdit_);
        placeRow(genreLabel_,       genreEdit_);
        placeRow(yearLabel_,        yearEdit_);
        bounds.removeFromTop(4);
        hintLabel_.setBounds(bounds.removeFromTop(16));
    }
    else // MultiPodcast
    {
        placeRow(titlePrefixLabel_, titleEdit_);
        placeRow(podcastLabel_,     podcastEdit_);
        placeRow(genreLabel_,       genreEdit_);
        placeRow(yearLabel_,        yearEdit_);
        bounds.removeFromTop(4);
        hintLabel_.setBounds(bounds.removeFromTop(16));
    }

    auto btnRow = bounds.removeFromBottom(btnH);
    cancelButton_.setBounds(btnRow.removeFromRight(btnW));
    btnRow.removeFromRight(8);
    saveButton_.setBounds(btnRow.removeFromRight(btnW));
}

} // namespace FoxPlayer
