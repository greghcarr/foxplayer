#include "SongInfoEditor.h"
#include "UIConstants.h"
#include "ui/MacWindowHelper.h"

namespace Stylus
{

using namespace UIConstants;

namespace
{
    // KeyListener attached to every Edit Info text field. Intercepts
    // Cmd+Backspace and turns it into "clear the whole field", matching
    // the macOS convention for single-line inputs (browser address bar,
    // Spotlight, system dialog text fields, etc.). JUCE's TextEditor
    // otherwise treats Cmd+Backspace as a plain single-character delete.
    // KeyListener::keyPressed runs before Component::keyPressed (see
    // ComponentPeer::handleKeyPress), so returning true here suppresses
    // TextEditor's default handling for this key combo only.
    // Walks up from a child component to find the enclosing SongInfoEditor.
    // Returns null if the editor isn't somewhere in the ancestor chain,
    // which would mean the listener/filter got attached to a field outside
    // its intended scope.
    SongInfoEditor* findEnclosingEditor(juce::Component* origin)
    {
        for (auto* c = origin; c != nullptr; c = c->getParentComponent())
            if (auto* se = dynamic_cast<SongInfoEditor*>(c))
                return se;
        return nullptr;
    }

    struct EditFieldKeyListener : public juce::KeyListener
    {
        bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override
        {
            const auto mods = key.getModifiers();

            // Cmd+Backspace: clear the focused field entirely. JUCE's
            // default treats this as a single-character delete; this
            // matches the macOS single-line-input convention instead.
            if (key.isKeyCode(juce::KeyPress::backspaceKey) && mods.isCommandDown())
            {
                if (auto* ed = dynamic_cast<juce::TextEditor*>(origin))
                {
                    ed->setText({}, juce::sendNotificationSync);
                    return true;
                }
            }

            // Esc dismisses the dialog. TextEditor would otherwise consume
            // Esc (via consumeEscAndReturnKeys) and the keystroke would
            // never reach the surrounding dialog window's escape handler,
            // so we have to catch it here and route it through dismiss().
            if (key.isKeyCode(juce::KeyPress::escapeKey)
                && ! mods.isAnyModifierKeyDown())
            {
                if (auto* editor = findEnclosingEditor(origin))
                {
                    editor->dismiss();
                    return true;
                }
            }

            // Option+Tab / Option+Shift+Tab: same as clicking the Next /
            // Previous buttons. Lets the user save-and-advance without
            // moving their hand off the keyboard. Plain Tab continues to
            // traverse fields.
            //
            // On macOS Option+Tab is *also* caught at the IME layer via
            // TabNavInputFilter below — that path fires when Cocoa routes
            // the keystroke through insertText: instead of keyDown:, which
            // it does specifically with Option held. We need both because
            // Cocoa's choice between the two paths isn't predictable for
            // every keyboard layout.
            if (key.isKeyCode(juce::KeyPress::tabKey) && mods.isAltDown())
            {
                if (auto* editor = findEnclosingEditor(origin))
                {
                    // Mods captured from the KeyPress are reliable for this
                    // path; the IME-routed path in TabNavInputFilter has to
                    // re-poll the OS for live state instead.
                    editor->requestNavigatePeer(mods.isShiftDown() ? -1 : +1);
                    return true;
                }
            }

            return false;
        }
    };

    // macOS NSTextInputContext (the IME) intercepts Option+Tab before JUCE's
    // keyPressed dispatch can see it, and converts the keystroke into a
    // plain character insertion via insertText: -> insertTextAtCaret. The
    // result is that nothing happens at the KeyListener layer; instead a
    // tab character (which JUCE TextEditor renders as a space-ish glyph
    // because tabKeyUsedAsCharacter is off) gets inserted. We catch the
    // hijacked insertion here, in TextEditor's InputFilter chain, by
    // looking for whitespace-y inserts while Option is currently held.
    // Legitimate Option+letter combos (like Option+E for combining accents)
    // produce non-whitespace text so they pass through unchanged.
    struct TabNavInputFilter : public juce::TextEditor::InputFilter
    {
        juce::String filterNewText(juce::TextEditor& ed,
                                    const juce::String& newInput) override
        {
            // Pull the LIVE OS-level modifier state, not the cached
            // ModifierKeys::currentModifiers. The cached value can be
            // stale by the time filterNewText fires for the second
            // navigation in a row: closing/reopening the dialog appears to
            // disturb JUCE's modifier-tracking even though the user never
            // released Option. getNativeRealtimeModifiers re-reads from
            // [NSEvent modifierFlags] on macOS, which is authoritative.
            const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
            if (! mods.isAltDown() || newInput.isEmpty())
                return newInput;

            // Only swallow whitespace-y single-char insertions. Tab (0x09),
            // ASCII space (0x20), and non-breaking space (0xA0) cover the
            // observed macOS variants for Option+Tab and Option+Shift+Tab.
            if (newInput.length() != 1) return newInput;
            const auto c = newInput[0];
            const bool isHijackedTab = (c == 0x09 || c == 0x20 || c == 0xA0);
            if (! isHijackedTab) return newInput;

            if (auto* editor = findEnclosingEditor(&ed))
                editor->requestNavigatePeer(mods.isShiftDown() ? -1 : +1);

            return {};   // drop the would-be insertion
        }
    };

    EditFieldKeyListener& sharedEditFieldKeys()
    {
        static EditFieldKeyListener instance;
        return instance;
    }

    TabNavInputFilter& sharedTabNavFilter()
    {
        static TabNavInputFilter instance;
        return instance;
    }
}

static const juce::String kLookupLabel =
    juce::String("Apple Music ") + juce::String(juce::CharPointer_UTF8("\xf0\x9f\x94\x8d"));

static constexpr int dialogW      = 460;
static constexpr int singleW      = 490; // wider to fit Prev/Next buttons
static constexpr int navBtnW      = 70;
static constexpr int labelW       = 76;
static constexpr int rowH         = 26;
static constexpr int rowGap       = 6;
static constexpr int pad          = 14;
static constexpr int btnW         = 80;
static constexpr int btnH         = 28;
static constexpr int halfFieldGap = 12;

SongInfoEditor::FilePathLink::FilePathLink()
{
    setInterceptsMouseClicks(true, false);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void SongInfoEditor::FilePathLink::setFile(const juce::File& f)
{
    file_ = f;
    repaint();
}

void SongInfoEditor::FilePathLink::paint(juce::Graphics& g)
{
    if (file_ == juce::File{}) return;

    const auto path = file_.getFullPathName();

    juce::Font font(juce::FontOptions().withHeight(13.0f));
    if (hovered_)
        font.setUnderline(true);
    g.setFont(font);
    g.setColour(hovered_ ? Color::textPrimary : Color::textDim);

    const auto bounds = getLocalBounds().reduced(2, 0).toFloat();
    g.drawText(path, bounds, juce::Justification::centredLeft, true);
}

void SongInfoEditor::FilePathLink::mouseEnter(const juce::MouseEvent&)
{
    hovered_ = true;
    repaint();
}

void SongInfoEditor::FilePathLink::mouseExit(const juce::MouseEvent&)
{
    hovered_ = false;
    repaint();
}

void SongInfoEditor::FilePathLink::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && file_ != juce::File{})
    {
        juce::PopupMenu m;
        m.addItem(1, "Copy Path");
        m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this),
            [path = file_.getFullPathName()](int r) {
                if (r == 1) juce::SystemClipboard::copyTextToClipboard(path);
            });
    }
}

void SongInfoEditor::FilePathLink::mouseUp(const juce::MouseEvent& e)
{
    // Reveal on left-click release inside the component. Using mouseUp (not
    // mouseDown) avoids triggering Finder during a click-and-drag and matches
    // the "I committed to clicking this" affordance.
    if (e.mods.isPopupMenu()) return;
    if (file_ == juce::File{}) return;
    if (! getLocalBounds().contains(e.getPosition())) return;
    file_.revealToUser();
}

// Tracks the most-recently-constructed SongInfoEditor so the global
// Option+Tab NSEvent monitor knows which instance to invoke. Only one Edit
// Info dialog is open at a time, but the Next/Previous flow creates a new
// editor before the previous one is asynchronously deleted, so the dtor
// must check that it's still the current_ before clearing it.
namespace { SongInfoEditor* currentEditor = nullptr; }

SongInfoEditor::SongInfoEditor(const TrackInfo& track)
    : mode_(track.isPodcast ? Mode::SinglePodcast : Mode::SingleMusic)
    , tracks_({ track })
{
    currentEditor = this;
    Stylus_setOptionTabMonitor([](bool shift) {
        if (currentEditor != nullptr)
            currentEditor->requestNavigatePeer(shift ? -1 : +1);
    });
    init();
}

SongInfoEditor::SongInfoEditor(const std::vector<TrackInfo>& tracks)
    : mode_((!tracks.empty() && tracks.front().isPodcast) ? Mode::MultiPodcast : Mode::MultiMusic)
    , tracks_(tracks)
{
    currentEditor = this;
    Stylus_setOptionTabMonitor([](bool shift) {
        if (currentEditor != nullptr)
            currentEditor->requestNavigatePeer(shift ? -1 : +1);
    });
    init();
}

SongInfoEditor::~SongInfoEditor()
{
    // The next-track recursion in MainComponent creates the replacement
    // editor before the old one is asynchronously destroyed: ctor runs
    // first, dtor later. Only clear current_ if it still points at us;
    // otherwise the new editor has already claimed it and we'd otherwise
    // wipe out an active monitor target.
    if (currentEditor == this)
    {
        currentEditor = nullptr;
        Stylus_setOptionTabMonitor(nullptr);
    }
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
    int w = dialogW, h = 302;
    switch (mode_)
    {
        case Mode::SingleMusic:   w = singleW; h = 302; break;
        case Mode::SinglePodcast: w = singleW; h = 222; break;
        case Mode::MultiMusic:                 h = 254; break;
        case Mode::MultiPodcast:               h = 222; break;
    }
    setSize(w, h);

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
        // Tab-into / click-into a field selects the whole content, so the
        // user can immediately type to replace it. The init() block below
        // intentionally moves the caret to the end for the initial focus
        // in Multi mode (where the user is extending a common prefix) -
        // that runs after this and overrides the selection.
        ed.setSelectAllWhenFocused(true);
        // Cmd+Backspace clears the field rather than deleting one char.
        ed.addKeyListener(&sharedEditFieldKeys());
        // Catches Option+Tab when macOS routes it through the IME path
        // instead of keyPressed (see TabNavInputFilter for details).
        ed.setInputFilter(&sharedTabNavFilter(), /*takeOwnership*/ false);
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

        fileLabel_.setFile(tracks_[0].file);
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

        fileLabel_.setFile(tracks_[0].file);
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

    if (mode_ == Mode::SingleMusic)
    {
        lookupButton_.setButtonText(kLookupLabel);
        lookupButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
        lookupButton_.setColour(juce::TextButton::textColourOffId, Color::textSecondary);
        lookupButton_.onClick = [this] {
            if (lookupSucceeded_)
            {
                titleEdit_.setText(snapTitle_,  false);
                artistEdit_.setText(snapArtist_, false);
                albumEdit_.setText(snapAlbum_,   false);
                genreEdit_.setText(snapGenre_,   false);
                yearEdit_.setText(snapYear_,     false);
                lookupSucceeded_ = false;
                lookupButton_.setButtonText(kLookupLabel);
                return;
            }
            if (!onLookupRequested) return;
            snapTitle_  = titleEdit_.getText();
            snapArtist_ = artistEdit_.getText();
            snapAlbum_  = albumEdit_.getText();
            snapGenre_  = genreEdit_.getText();
            snapYear_   = yearEdit_.getText();
            lookupButton_.setButtonText("Looking up...");
            lookupButton_.setEnabled(false);
            juce::Component::SafePointer<SongInfoEditor> safeLookup(this);
            onLookupRequested(tracks_[0], [safeLookup](bool success, TrackInfo result) {
                if (auto* self = safeLookup.getComponent())
                {
                    if (success)
                        self->fillFromLookup(result);
                    else
                        self->lookupFailed();
                }
            });
        };
        addAndMakeVisible(lookupButton_);
    }

    if (mode_ == Mode::SingleMusic || mode_ == Mode::SinglePodcast)
    {
        for (auto* btn : { &prevButton_, &nextButton_ })
        {
            btn->setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
            btn->setColour(juce::TextButton::textColourOffId, Color::textPrimary);
            btn->setEnabled(false);
            addAndMakeVisible(*btn);
        }
        prevButton_.onClick = [this] {
            collectEditsIntoTracks();
            if (onSave) onSave(tracks_);
            if (onSaveAndNavigate) onSaveAndNavigate(-1);
        };
        nextButton_.onClick = [this] {
            collectEditsIntoTracks();
            if (onSave) onSave(tracks_);
            if (onSaveAndNavigate) onSaveAndNavigate(+1);
        };
    }

    saveButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a5a8a));
    saveButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    saveButton_.onClick = [this] { save(); };
    addAndMakeVisible(saveButton_);

    cancelButton_.setColour(juce::TextButton::buttonColourId,  juce::Colour(0xff2a2a2a));
    cancelButton_.setColour(juce::TextButton::textColourOffId, Color::textPrimary);
    cancelButton_.onClick = [this] {
        if (onDismiss) onDismiss();
    };
    addAndMakeVisible(cancelButton_);
}

void SongInfoEditor::fillFromLookup(const TrackInfo& result)
{
    if (result.title.isNotEmpty())  titleEdit_.setText(result.title,  false);
    if (result.artist.isNotEmpty()) artistEdit_.setText(result.artist, false);
    if (result.album.isNotEmpty())  albumEdit_.setText(result.album,   false);
    if (result.genre.isNotEmpty())  genreEdit_.setText(result.genre,   false);
    if (result.year.isNotEmpty())   yearEdit_.setText(result.year,     false);

    lookupSucceeded_ = true;
    lookupButton_.setButtonText("Undo");
    lookupButton_.setEnabled(true);
}

void SongInfoEditor::lookupFailed()
{
    lookupButton_.setButtonText("Failed");
    lookupButton_.setEnabled(false);
    juce::Component::SafePointer<SongInfoEditor> safeThis(this);
    juce::Timer::callAfterDelay(2000, [safeThis] {
        if (auto* self = safeThis.getComponent())
        {
            self->lookupButton_.setButtonText(kLookupLabel);
            self->lookupButton_.setEnabled(true);
        }
    });
}

void SongInfoEditor::collectEditsIntoTracks()
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
}

void SongInfoEditor::dismiss()
{
    if (onDismiss) onDismiss();
}

void SongInfoEditor::requestNavigatePeer(int delta)
{
    auto& btn = (delta > 0) ? nextButton_ : prevButton_;
    // Hidden in Multi mode, disabled at the ends of a Single-mode peer
    // list. Either way, the keyboard shortcut should silently do nothing
    // rather than save + dismiss with no follow-up dialog.
    if (! btn.isShowing() || ! btn.isEnabled()) return;
    // Pull the latest field values out of the text editors into tracks_
    // before save fires - matches what the on-click handlers above do.
    // Without this, the keyboard-shortcut path would save the original
    // (pre-edit) values and the user's changes would be silently lost.
    collectEditsIntoTracks();
    if (onSave) onSave(tracks_);
    if (onSaveAndNavigate) onSaveAndNavigate(delta);
}

void SongInfoEditor::setPeerNavigation(int index, int total)
{
    prevButton_.setEnabled(index > 0);
    nextButton_.setEnabled(index < total - 1);
}

void SongInfoEditor::save()
{
    if (mode_ == Mode::SingleMusic || mode_ == Mode::SinglePodcast)
    {
        collectEditsIntoTracks();
    }
    else // multi-edit
    {
        const juce::String newPrefix  = titleEdit_.getText().trimStart();
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
    if (onDismiss) onDismiss();
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
    if (mode_ == Mode::SingleMusic || mode_ == Mode::SinglePodcast)
    {
        btnRow.removeFromRight(8);
        nextButton_.setBounds(btnRow.removeFromRight(navBtnW));
        btnRow.removeFromRight(4);
        prevButton_.setBounds(btnRow.removeFromRight(navBtnW));
    }
    if (mode_ == Mode::SingleMusic)
        lookupButton_.setBounds(btnRow.removeFromLeft(120));
}

} // namespace Stylus
