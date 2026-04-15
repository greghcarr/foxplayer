#include "SongInfoEditor.h"
#include "Constants.h"

namespace FoxPlayer
{

using namespace Constants;

static constexpr int dialogW      = 460;
static constexpr int dialogH      = 270;
static constexpr int labelW       = 68;
static constexpr int rowH         = 26;
static constexpr int rowGap       = 6;
static constexpr int pad          = 14;
static constexpr int btnW         = 80;
static constexpr int btnH         = 28;
static constexpr int halfFieldGap = 12;

SongInfoEditor::SongInfoEditor(const TrackInfo& track)
    : track_(track)
{
    setSize(dialogW, dialogH);

    auto styleLabel = [](juce::Label& lbl) {
        lbl.setColour(juce::Label::textColourId, Color::textSecondary);
        lbl.setJustificationType(juce::Justification::centredRight);
    };

    auto styleEditor = [](juce::TextEditor& ed) {
        ed.setColour(juce::TextEditor::backgroundColourId, Color::headerBackground);
        ed.setColour(juce::TextEditor::textColourId,       Color::textPrimary);
        ed.setColour(juce::TextEditor::outlineColourId,    Color::border);
        ed.setColour(juce::TextEditor::focusedOutlineColourId, Color::accent);
    };

    for (auto* lbl : { &titleLabel_, &artistLabel_, &albumLabel_,
                       &genreLabel_, &yearLabel_, &trackNumLabel_ })
    {
        styleLabel(*lbl);
        addAndMakeVisible(*lbl);
    }

    for (auto* ed : { &titleEdit_, &artistEdit_, &albumEdit_,
                      &genreEdit_, &yearEdit_, &trackNumEdit_ })
    {
        styleEditor(*ed);
        addAndMakeVisible(*ed);
    }

    titleEdit_.setText(track_.displayTitle(), false);
    artistEdit_.setText(track_.artist,        false);
    albumEdit_.setText(track_.album,          false);
    genreEdit_.setText(track_.genre,          false);
    yearEdit_.setText(track_.year,            false);
    trackNumEdit_.setText(track_.trackNumber > 0
                              ? juce::String(track_.trackNumber)
                              : juce::String(),
                          false);

    fileLabel_.setText(track_.file.getFullPathName(), juce::dontSendNotification);
    fileLabel_.setColour(juce::Label::textColourId, Color::textDim);
    fileLabel_.setMinimumHorizontalScale(0.5f);
    addAndMakeVisible(fileLabel_);

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
    track_.title       = titleEdit_.getText().trim();
    track_.artist      = artistEdit_.getText().trim();
    track_.album       = albumEdit_.getText().trim();
    track_.genre       = genreEdit_.getText().trim();
    track_.year        = yearEdit_.getText().trim();
    track_.trackNumber = trackNumEdit_.getText().trim().getIntValue();

    if (onSave) onSave(track_);

    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState(1);
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

    placeRow(titleLabel_,    titleEdit_);
    placeRow(artistLabel_,   artistEdit_);
    placeRow(albumLabel_,    albumEdit_);
    placeRow(genreLabel_,    genreEdit_);

    // Year + Track # on the same row
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

    // File path
    bounds.removeFromTop(4);
    fileLabel_.setBounds(bounds.removeFromTop(18));

    // Buttons at bottom right
    bounds.removeFromTop(6);
    auto btnRow = bounds.removeFromBottom(btnH);
    cancelButton_.setBounds(btnRow.removeFromRight(btnW));
    btnRow.removeFromRight(8);
    saveButton_.setBounds(btnRow.removeFromRight(btnW));
}

} // namespace FoxPlayer
