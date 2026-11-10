#include "Panels.h"

namespace lt
{
static juce::String pct (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }

static juce::String formatTime (float seconds)
{
    const int s = juce::jmax (0, juce::roundToInt (seconds));
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

static juce::String modelName (const juce::String& desc)
{
    auto n = desc.upToFirstOccurrenceOf ("  |", false, false).trim();
    // "birds_dawnchorus_b2048_r48000_z8" -> "birds dawnchorus"
    juce::StringArray parts;
    parts.addTokens (n, "_", "");
    juce::StringArray keep;
    for (auto& p : parts)
    {
        const bool tech = (p.startsWithChar ('b') || p.startsWithChar ('r') || p.startsWithChar ('z') || p.startsWith ("sr"))
                          && p.substring (p.startsWith ("sr") ? 2 : 1).containsOnly ("0123456789") && p.length() > 1;
        if (! tech) keep.add (p);
    }
    auto out = keep.joinIntoString (" ");
    return out.isNotEmpty() ? out : n;
}

static juce::RangedAudioParameter* param (SedimentsProcessor& p, const char* id)
{
    return dynamic_cast<juce::RangedAudioParameter*> (p.params.getParameter (id));
}

//==============================================================================
TopBar::TopBar (SedimentsProcessor& p, EditorActions& a)
    : proc (p), actions (a),
      volume (param (p, "gain"), "Volume", {}, {}, [] (float db)
      {
          const int d = juce::roundToInt (db);
          return d <= -48 ? juce::String ("Off") : (d > 0 ? "+" : (d < 0 ? juce::String (juce::CharPointer_UTF8 ("\xe2\x88\x92")) : juce::String())) + juce::String (std::abs (d)) + " dB";
      }, LabeledSlider::Layout::inlineRow)
{
    for (auto* b : { &modelStep, &soundsStep, &terrainStep })
    {
        b->setStyle (LtButton::Style::step);
        addAndMakeVisible (*b);
    }
    modelStep.stepNumber = "1"; soundsStep.stepNumber = "2"; terrainStep.stepNumber = "3";
    modelStep.onClick = [this]
    {
        juce::PopupMenu m;
        m.addItem (1, "Choose a model...");
        m.addItem (2, "Play without a model (your original sounds)", true, proc.engine.isWithoutModel());
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&modelStep), [this] (int r)
        {
            if (r == 1 && actions.chooseModel) actions.chooseModel();
            if (r == 2 && actions.useWithoutModel) actions.useWithoutModel();
        });
    };
    soundsStep.onClick = [this] { if (actions.chooseFiles) actions.chooseFiles(); };
    modelStep.setTooltip ("Choose the sound model (a RAVE .ts file), or play your original sounds without one");
    soundsStep.setTooltip ("Add audio files to draw the map");
    terrainStep.setTooltip ("The learned terrain is trained by itself after every change of sounds");
    volume.setValueWidth (58);
    addAndMakeVisible (volume);
    help.onClick = [this] { if (actions.openHelp) actions.openHelp(); };
    settings.onClick = [this] { if (actions.openSettings) actions.openSettings(); };
    addAndMakeVisible (help);
    addAndMakeVisible (settings);
    refresh();
}

void TopBar::refresh()
{
    auto& e = proc.engine;
    const bool hasModel = e.getModelDescription().isNotEmpty();
    const bool withoutModel = e.isWithoutModel();
    auto corpus = e.getCorpus();
    const int nSounds = corpus != nullptr ? (int) corpus->sources.size() : 0;
    const bool hasSounds = corpus != nullptr && corpus->size() > 0;
    const bool busy = e.isBusy();

    modelStep.stepState = hasModel || withoutModel ? 2 : 1;
    modelStep.setSubText (hasModel ? modelName (e.getModelDescription()) : (withoutModel ? "Originals" : (busy ? "Loading..." : "Choose one")));
    modelStep.setTitle ("Step 1, model: " + modelStep.getButtonText() + " " + (hasModel ? modelName (e.getModelDescription()) : "not chosen"));

    soundsStep.stepState = hasSounds ? 2 : (hasModel || withoutModel ? 1 : 0);
    soundsStep.setSubText (busy && (hasModel || withoutModel) ? juce::String ("Adding...") : (nSounds > 0 ? juce::String (nSounds) + (nSounds == 1 ? " sound" : " sounds") : juce::String ("None yet")));
    soundsStep.setTitle ("Step 2, sounds: " + juce::String (nSounds));

    if (withoutModel)
    {
        terrainStep.stepState = 0;
        terrainStep.setSubText ("Not used");
    }
    else if (e.isTraining())
    {
        terrainStep.stepState = 3;
        terrainStep.progress = juce::jmax (0.0f, e.getTrainingProgress());
        terrainStep.setSubText ("Learning...");
    }
    else if (e.getLearnedTerrain() != nullptr)
    {
        terrainStep.stepState = 2;
        terrainStep.setSubText ("Learned");
    }
    else
    {
        terrainStep.stepState = hasSounds ? 1 : 0;
        terrainStep.setSubText (hasSounds ? "Waiting" : "Not yet");
    }
    terrainStep.setTitle (juce::String ("Step 3, terrain: ") + (terrainStep.stepState == 2 ? "learned" : "not learned yet"));
    for (auto* b : { &modelStep, &soundsStep, &terrainStep })
        b->repaint();
}

void TopBar::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    g.setColour (t.panel);
    g.fillAll();
    g.setColour (t.line);
    g.fillRect (0, getHeight() - 1, getWidth(), 1);

    // logo
    auto logo = juce::Rectangle<float> (20.0f, getHeight() * 0.5f - 15.0f, 30.0f, 30.0f);
    g.setColour (s.accent());
    g.drawEllipse (logo.reduced (1.0f, 4.0f), 1.6f);
    g.drawEllipse (logo.reduced (6.0f, 8.5f).translated (1.0f, 0.0f), 1.6f);
    g.drawEllipse (logo.reduced (11.0f, 12.5f).translated (2.0f, 0.0f), 1.6f);
    g.setColour (t.fg);
    g.setFont (s.font (17.0f, true));
    if (brandWidth > 60)
    {
        if (getWidth() > 1180)
            g.drawText ("Sediments", 60, getHeight() / 2 - 20, brandWidth - 60, 22, juce::Justification::bottomLeft);
        else
            g.drawText ("Sediments", 60, 0, brandWidth - 60, getHeight(), juce::Justification::centredLeft);
    }
    if (getWidth() > 1180 && brandWidth > 60)
    {
        g.setColour (t.muted);
        g.setFont (s.font (12.0f));
        g.drawText ("Play your sounds as a landscape", 60, getHeight() / 2 + 2, 220, 18, juce::Justification::topLeft);
    }

    // links between steps
    g.setColour (t.line);
    for (auto* pair : { &modelStep, &soundsStep })
    {
        const auto r = pair->getBounds();
        g.fillRoundedRectangle ((float) r.getRight() + 4.0f, (float) r.getCentreY() - 1.0f, 12.0f, 2.0f, 1.0f);
    }
}

void TopBar::resized()
{
    auto b = getLocalBounds().reduced (16, 8);
    const int sw = settings.idealWidth (44), hw = help.idealWidth (44);
    settings.setBounds (b.removeFromRight (sw).withSizeKeepingCentre (sw, 44));
    b.removeFromRight (8);
    help.setBounds (b.removeFromRight (hw).withSizeKeepingCentre (hw, 44));
    b.removeFromRight (20);
    volume.setBounds (b.removeFromRight (210).withSizeKeepingCentre (210, 44));
    b.removeFromRight (20);

    const int h = 48, gap = 20;
    LtButton* stepsTmp[] = { &modelStep, &soundsStep, &terrainStep };
    int need = 2 * gap;
    for (auto* st : stepsTmp) need += juce::jlimit (128, 230, st->idealWidth ((float) h));
    const int nameW = (int) juce::GlyphArrangement::getStringWidth (ui().font (17.0f, true), "Sediments") + 8;
    juce::ignoreUnused (need);
    brandWidth = b.getWidth() - (60 + nameW + 4) >= 3 * 128 + 2 * gap ? 60 + nameW : 52;
    b.removeFromLeft ((getWidth() > 1180 ? juce::jmax (brandWidth, 250) : brandWidth) - 16 + 4);
    LtButton* steps[] = { &modelStep, &soundsStep, &terrainStep };
    int widths[3], total = 2 * gap;
    for (int i = 0; i < 3; ++i) { widths[i] = juce::jlimit (128, 230, steps[i]->idealWidth ((float) h)); total += widths[i]; }
    if (total > b.getWidth())
    {
        const float k = (float) (b.getWidth() - 2 * gap) / (float) (total - 2 * gap);
        total = 2 * gap;
        for (auto& w : widths) { w = juce::jmax (110, (int) (w * k)); total += w; }
    }
    int x = juce::jmax (b.getX(), b.getCentreX() - total / 2);
    for (int i = 0; i < 3; ++i)
    {
        steps[i]->setBounds (x, b.getCentreY() - h / 2, widths[i], h);
        x += widths[i] + gap;
    }
}

//==============================================================================
ModeBar::ModeBar (EditorActions& a) : actions (a)
{
    const char* names[] = { "Explore", "Gesture", "Keys" };
    const char* icons[] = { "compass", "gesture", "keys" };
    for (int i = 0; i < 3; ++i)
    {
        auto* b = tabs.add (new LtButton (names[i], icons[i], LtButton::Style::segment));
        b->setRadioGroupId (777);
        b->setClickingTogglesState (true);
        b->setTitle (juce::String (names[i]) + " mode");
        b->setTooltip (juce::String ("Shortcut: ") + juce::String (i + 1));
        b->onClick = [this, i] { if (tabs[i]->getToggleState() && actions.setMode) actions.setMode ((Mode) i); };
        addAndMakeVisible (b);
    }
    tabs[0]->setToggleState (true, juce::dontSendNotification);
}

void ModeBar::setMode (Mode m)
{
    mode = m;
    tabs[(int) m]->setToggleState (true, juce::dontSendNotification);
    repaint();
}

void ModeBar::setMidiOn (bool on)
{
    if (on != midiOn) { midiOn = on; repaint(); }
}

void ModeBar::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    auto seg = tabs.getFirst()->getBounds().getUnion (tabs.getLast()->getBounds()).expanded (3).toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (seg, 14.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (seg.reduced (0.5f), 14.0f, 1.0f);

    const char* hints[] = { "Drag the cursor, or focus it and use the arrow keys",
                            "Press Record, then draw on the map",
                            "Play from a MIDI keyboard, a MIDI item or the keys below" };
    auto area = getLocalBounds().withTrimmedLeft ((int) seg.getRight() + 16);
    if (midiOn)
    {
        auto chip = area.removeFromRight (100).withSizeKeepingCentre (96, 30).toFloat();
        g.setColour (t.panel);
        g.fillRoundedRectangle (chip, 15.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (chip.reduced (0.5f), 15.0f, 1.0f);
        g.setColour (s.accent());
        g.fillEllipse (chip.getX() + 12.0f, chip.getCentreY() - 4.0f, 8.0f, 8.0f);
        g.setColour (t.fg);
        g.setFont (s.font (13.0f, true));
        g.drawText ("MIDI on", chip.withTrimmedLeft (26.0f), juce::Justification::centredLeft);
    }
    g.setColour (t.muted);
    g.setFont (s.font (14.5f));
    if (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hints[(int) mode]) <= (float) area.getWidth() - 8.0f)
        g.drawText (hints[(int) mode], area, juce::Justification::centredRight, false);
}

void ModeBar::resized()
{
    int x = 3;
    for (auto* b : tabs)
    {
        const int w = b->idealWidth (42.0f) + 6;
        b->setBounds (x, (getHeight() - 42) / 2, w, 42);
        x += w + 3;
    }
}

//==============================================================================
MixStrip::MixStrip (SedimentsProcessor& p, EditorActions& a) : proc (p), actions (a)
{
    setTitle ("You're hearing");
}

std::unique_ptr<juce::AccessibilityHandler> MixStrip::createAccessibilityHandler()
{
    struct Value : public juce::AccessibilityTextValueInterface
    {
        explicit Value (MixStrip& m) : owner (m) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return owner.text; }
        void setValueAsString (const juce::String&) override {}
        MixStrip& owner;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::staticText, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

void MixStrip::refresh (const std::shared_ptr<const Corpus>& c)
{
    const juce::Point<float> pos (proc.engine.curX.load(), proc.engine.curY.load());
    const float blur = proc.engine.blur.load();
    if (c == corpus && pos.getDistanceFrom (lastPos) < 0.002f && std::abs (blur - lastBlur) < 0.001f)
        return;
    corpus = c; lastPos = pos; lastBlur = blur;
    parts.clear();
    if (corpus == nullptr || corpus->size() == 0)
    {
        text = "nothing yet";
        repaint();
        return;
    }
    const float sigma = juce::jmax (0.03f, Terrain::blurToSigma (blur) * 1.4f);
    std::vector<double> w (corpus->sources.size(), 0.0);
    double total = 0.0;
    for (int i = 0; i < corpus->size(); ++i)
    {
        const float dx = corpus->xy[(size_t) i * 2] - pos.x, dy = corpus->xy[(size_t) i * 2 + 1] - pos.y;
        const double v = std::exp (-(dx * dx + dy * dy) / (2.0f * sigma * sigma));
        w[(size_t) corpus->sourceIndex[(size_t) i]] += v;
        total += v;
    }
    if (total < 1e-9)
    {
        // far from everything: name the nearest sound
        int best = 0; float bd = 1e9f;
        for (int i = 0; i < corpus->size(); ++i)
        {
            const float dx = corpus->xy[(size_t) i * 2] - pos.x, dy = corpus->xy[(size_t) i * 2 + 1] - pos.y;
            if (dx * dx + dy * dy < bd) { bd = dx * dx + dy * dy; best = corpus->sourceIndex[(size_t) i]; }
        }
        parts.push_back ({ best, 1.0f });
    }
    else
    {
        for (size_t i = 0; i < w.size(); ++i)
        {
            const float share = (float) (w[i] / total);
            if (share >= 0.03f) parts.push_back ({ (int) i, share });
        }
        std::sort (parts.begin(), parts.end(), [] (const Part& a, const Part& b) { return a.share > b.share; });
        if (parts.size() > 4) parts.resize (4);
    }

    juce::StringArray bits;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        auto name = juce::File::createFileWithoutCheckingPath (corpus->sources[(size_t) parts[i].source].path).getFileNameWithoutExtension();
        bits.add ((i == 0 && parts.size() > 1 ? "mostly " : "") + name + (parts.size() > 1 ? " " + pct (parts[i].share) : juce::String()));
    }
    text = bits.joinIntoString (juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")));
    const int dominant = parts.empty() ? -1 : parts.front().source;
    if (dominant != lastDominant)
    {
        lastDominant = dominant;
        if (ui().announce && dominant >= 0)
            juce::AccessibilityHandler::postAnnouncement ("Now " + text, juce::AccessibilityHandler::AnnouncementPriority::low);
    }
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    repaint();
}

void MixStrip::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    paintCard (g, getLocalBounds().toFloat());
    auto b = getLocalBounds().reduced (16, 10);
    Icons::draw (g, "wave", b.removeFromLeft (24).withSizeKeepingCentre (22, 22).toFloat(), s.accent(), 2.0f);
    b.removeFromLeft (12);

    // keyboard hints on the right
    struct Hint { const char* key; const char* what; };
    const Hint hints[] = { { "Arrows", "move" }, { "Shift", "fine" }, { "Space", "hear nearest" } };
    float hw = 0.0f;
    for (auto& h : hints) hw += keycapWidth (h.key) + juce::GlyphArrangement::getStringWidth (s.font (13.0f), h.what) + 22.0f;
    if (b.getWidth() > hw + 260.0f)
    {
        auto hr = b.removeFromRight ((int) hw).toFloat();
        float x = hr.getX();
        for (auto& h : hints)
        {
            const float kw = keycapWidth (h.key);
            drawKeycap (g, { x, hr.getCentreY() - 12.0f, kw, 24.0f }, h.key);
            x += kw + 6.0f;
            g.setColour (t.muted);
            g.setFont (s.font (13.0f));
            const float tw = juce::GlyphArrangement::getStringWidth (s.font (13.0f), h.what);
            g.drawText (h.what, juce::Rectangle<float> (x, hr.getY(), tw + 2.0f, hr.getHeight()), juce::Justification::centredLeft);
            x += tw + 16.0f;
        }
        b.removeFromRight (16);
    }

    auto top = b.removeFromTop (b.getHeight() - 14).toFloat();
    g.setColour (t.fg);
    g.setFont (s.font (15.0f, true));
    const juce::String head = "You're hearing";
    const float hwid = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), head) + 10.0f;
    g.drawText (head, top.removeFromLeft (hwid), juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (s.font (15.0f));
    g.drawText (text, top, juce::Justification::centredLeft, true);

    auto bar = b.toFloat().withHeight (8.0f).withY ((float) b.getBottom() - 9.0f);
    g.setColour (t.raised);
    g.fillRoundedRectangle (bar, 4.0f);
    float x = bar.getX();
    for (auto& p : parts)
    {
        const float w = bar.getWidth() * p.share;
        auto& src = corpus->sources[(size_t) p.source];
        g.setColour (actions.colourOf ? actions.colourOf (src) : s.accent());
        g.fillRoundedRectangle (x, bar.getY(), juce::jmax (2.0f, w - 2.0f), bar.getHeight(), 3.0f);
        x += w;
    }
}

//==============================================================================
LabeledGroup::LabeledGroup (const juce::String& l, juce::Component& c) : label (l), child (c)
{
    addAndMakeVisible (child);
}

void LabeledGroup::paint (juce::Graphics& g)
{
    g.setColour (ui().theme().muted);
    g.setFont (ui().font (12.5f));
    g.drawText (label, getLocalBounds().removeFromTop (18), juce::Justification::centredLeft);
}

void LabeledGroup::resized()
{
    child.setBounds (getLocalBounds().withTrimmedTop (20));
}

GesturePanel::GesturePanel (SedimentsProcessor& p, MapView& m)
    : proc (p), map (m),
      loop ({ "Free", "1 beat", "2 beats", "1 bar", "2 bars", "4 bars" }, "Loop length"),
      dir ({ "Forward", "Backward", "Back and forth" }, "Direction"),
      follow ("Follow the cursor", "Moves with X/Y or MIDI notes"),
      speed (param (p, "pathSpeed"), "Speed", {}, {}, [] (float v) { return juce::String (v, 2) + juce::String (juce::CharPointer_UTF8 ("\xc3\x97")); }),
      gestureGroup ("Gesture", buttonRow), loopGroup ("Loop length", loop), dirGroup ("Direction", dir)
{
    for (auto* b : { &rec, &play, &clear })
        buttonRow.addAndMakeVisible (*b);
    rec.setClickingTogglesState (true);
    rec.setTooltip ("Shortcut: R");
    rec.onClick = [this]
    {
        map.recordArmed = rec.getToggleState();
        if (map.recordArmed)
            if (auto* pp = proc.params.getParameter ("pathOn")) pp->setValueNotifyingHost (0.0f);
        map.repaint();
    };
    map.onRecordingFinished = [this] { rec.setToggleState (false, juce::dontSendNotification); };
    play.setClickingTogglesState (true);
    playAttach = std::make_unique<juce::ButtonParameterAttachment> (*param (p, "pathOn"), play);
    clear.onClick = [this]
    {
        proc.engine.setPath ({});
        if (auto* pp = proc.params.getParameter ("pathOn")) pp->setValueNotifyingHost (0.0f);
    };
    loop.attachToChoice (*param (p, "pathSync"));
    dir.attachToChoice (*param (p, "pathDir"));
    follow.boxed = false;
    followAttach = std::make_unique<juce::ParameterAttachment> (*param (p, "pathAnchor"),
        [this] (float v) { follow.setToggleState (v > 0.5f, juce::dontSendNotification); });
    followAttach->sendInitialUpdate();
    follow.onClick = [this] { followAttach->setValueAsCompleteGesture (follow.getToggleState() ? 1.0f : 0.0f); };
    for (auto* c : { (juce::Component*) &gestureGroup, (juce::Component*) &loopGroup, (juce::Component*) &dirGroup,
                     (juce::Component*) &follow, (juce::Component*) &speed })
        addAndMakeVisible (c);
}

void GesturePanel::refresh()
{
    const bool hasPath = proc.engine.getPath() != nullptr;
    play.setEnabled (hasPath);
    const bool playing = play.getToggleState();
    if (play.getButtonText() != (playing ? "Pause" : "Play"))
    {
        play.setButtonText (playing ? "Pause" : "Play");
        play.setIcon (playing ? "pause" : "play");
    }
    clear.setEnabled (hasPath);
    rec.setButtonText (rec.getToggleState() ? "Cancel" : "Record");
    const bool free = loop.getSelected() == 0;
    speed.setEnabled (free);
    speed.slider.setTooltip (free ? juce::String() : juce::String ("The loop length follows the tempo of your project"));
}

std::vector<FlowItem> GesturePanel::items()
{
    const int bw = rec.idealWidth (44) + play.idealWidth (44) + clear.idealWidth (44) + 16;
    return {
        { &gestureGroup, bw, 66, 24 },
        { &loopGroup, loop.preferredWidth(), 66, 24 },
        { &dirGroup, dir.preferredWidth(), 66, 24 },
        { &follow, 250, 50, 20 },
        { &speed, 190, 50, 0 }
    };
}

int GesturePanel::preferredHeight (int width)
{
    return flowLayout (items(), { 16, 12, width - 32, 1000 }, 12, false) + 26;
}

void GesturePanel::paint (juce::Graphics& g) { paintCard (g, getLocalBounds().toFloat()); }

void GesturePanel::resized()
{
    auto all = items();
    auto area = getLocalBounds().reduced (16, 12);
    flowLayout (all, area, 12, true);
    // vertically centre the follow/speed controls on their row
    for (auto* c : { (juce::Component*) &follow, (juce::Component*) &speed })
        c->setTopLeftPosition (c->getX(), c->getY() + 12);
    int x = 0;
    for (auto* b : { &rec, &play, &clear })
    {
        const int w = b->idealWidth (44);
        b->setBounds (x, 0, w, 44);
        x += w + 8;
    }
}

//==============================================================================
KeysPanel::KeysPanel (SedimentsProcessor& p)
    : proc (p),
      midiOn ("Play with MIDI notes", "Notes from your MIDI keyboard, a MIDI item, or these keys"),
      spray (param (p, "spray"), "Spray", "Exact", "Scattered", [] (float v) { return pct (v); }),
      attack (param (p, "attack"), "Attack", "Snappy", "Swell", [] (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 1) + " s"; }),
      release (param (p, "release"), "Release", "Short", "Long", [] (float v) { return v < 1.0f ? juce::String (juce::roundToInt (v * 1000.0f)) + " ms" : juce::String (v, 1) + " s"; })
{
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    static const int order[] = { 0, 2, 4, 5, 7, 9, 11, 1, 3, 6, 8, 10 };   // white keys first, black on top
    LtButton* made[12] = {};
    for (int k : order)
    {
        const bool black = juce::String (names[k]).containsChar ('#');
        auto* b = new LtButton (names[k], {}, black ? LtButton::Style::keyBlack : LtButton::Style::keyWhite);
        b->setTitle ("Play zone " + juce::String (names[k]));
        auto down = std::make_shared<bool> (false);
        b->onStateChange = [this, b, k, down]
        {
            const bool isDown = b->getState() == juce::Button::buttonDown;
            if (isDown != *down) { *down = isDown; playKey (k, isDown); }
        };
        made[k] = b;
        addAndMakeVisible (b);
    }
    for (auto* b : made)
        keys.add (b);
    midiOn.boxed = false;
    midiAttach = std::make_unique<juce::ButtonParameterAttachment> (*param (p, "midiMode"), midiOn);
    addAndMakeVisible (midiOn);
    for (auto* s : { &spray, &attack, &release })
        addAndMakeVisible (s);
}

void KeysPanel::playKey (int semitone, bool down)
{
    if (down && ! proc.engine.midiMode.load())
    {
        if (auto* p = proc.params.getParameter ("midiMode")) p->setValueNotifyingHost (1.0f);
        proc.engine.midiMode = true;
    }
    proc.engine.pushNoteFromUI (60 + semitone, 0.8f, down);
}

void KeysPanel::refresh()
{
    const int zone = proc.engine.activeZone.load();
    auto c = proc.engine.getCorpus();
    const int nz = c != nullptr ? juce::jmax (1, c->numZones()) : 12;
    for (int k = 0; k < 12; ++k)
        keys[k]->setHighlight (zone >= 0 && (k % nz) == zone);
}

int KeysPanel::preferredHeight (int width) const
{
    const int right = width - 32 - 448 - 28;
    return right >= 420 ? 170 : 300;
}

void KeysPanel::paint (juce::Graphics& g)
{
    auto& s = ui();
    paintCard (g, getLocalBounds().toFloat());
    g.setColour (s.theme().muted);
    g.setFont (s.font (12.5f));
    g.drawText ("One octave = the whole map. Every octave plays the same zone.", 16, 12, 448, 18, juce::Justification::centredLeft);
}

void KeysPanel::resized()
{
    const int kx = 16, ky = 36;
    static const int whites[] = { 0, 2, 4, 5, 7, 9, 11 };
    for (int i = 0; i < 7; ++i)
        keys[whites[i]]->setBounds (kx + i * 64, ky, 64, 108);
    static const std::pair<int, int> blacks[] = { { 1, 42 }, { 3, 106 }, { 6, 234 }, { 8, 298 }, { 10, 362 } };
    for (auto& b : blacks)
        keys[b.first]->setBounds (kx + b.second, ky, 44, 64);
    for (int k : { 1, 3, 6, 8, 10 })
        keys[k]->toFront (false);

    auto right = getLocalBounds().reduced (16, 12).withTrimmedLeft (448 + 28);
    if (right.getWidth() < 420)
        right = juce::Rectangle<int> (16, ky + 120, getWidth() - 32, getHeight() - ky - 132);
    midiOn.setBounds (right.removeFromTop (midiOn.preferredHeight (right.getWidth())));
    right.removeFromTop (8);
    const int w = (right.getWidth() - 32) / 3;
    int x = right.getX();
    for (auto* s : { &spray, &attack, &release })
    {
        s->setBounds (x, right.getY(), w, 66);
        x += w + 16;
    }
}

//==============================================================================
namespace
{
    // Choosing a sound's colour: palette swatches, any colour, or back to default.
    class ColourChoice : public juce::Component, private juce::ChangeListener
    {
    public:
        ColourChoice (SedimentsProcessor& p, EditorActions& a, const juce::String& path, const juce::String& name, int colourIndex)
            : proc (p), actions (a), filePath (path), fileName (name)
        {
            auto& pal = paletteById (ui().paletteId).colours;
            for (size_t i = 0; i < pal.size(); ++i)
            {
                auto* b = swatches.add (new LtButton ("Colour " + juce::String ((int) i + 1), {}, LtButton::Style::swatch));
                const auto c = ensureContrast (pal[i], ui().theme().map, 3.0);
                b->setSwatch (c);
                b->setRadioGroupId (991);
                b->onClick = [this, c] { set (c); };
                addAndMakeVisible (b);
            }
            reset.onClick = [this] { proc.soundColours.erase (filePath); done(); };
            addAndMakeVisible (reset);
            auto it = p.soundColours.find (path);
            selector.setCurrentColour (it != p.soundColours.end() ? it->second : ui().soundColour (colourIndex));
            selector.addChangeListener (this);
            addAndMakeVisible (selector);
            setSize (300, 380);
        }

        void paint (juce::Graphics& g) override
        {
            auto& s = ui();
            g.setColour (s.theme().fg);
            g.setFont (s.font (15.0f, true));
            g.drawText ("Colour for " + fileName, 12, 8, getWidth() - 24, 22, juce::Justification::centredLeft, true);
            g.setColour (s.theme().muted);
            g.setFont (s.font (12.5f));
            g.drawFittedText ("Shades are nudged a little so every colour stands out on the map.", 12, getHeight() - 42, getWidth() - 24, 36,
                              juce::Justification::topLeft, 2);
        }

        void resized() override
        {
            int x = 8;
            for (auto* b : swatches) { b->setBounds (x, 36, 36, 36); x += 36; }
            selector.setBounds (8, 80, getWidth() - 16, 200);
            reset.setBounds (8, 288, getWidth() - 16, 44);
        }

    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        juce::String filePath, fileName;
        juce::OwnedArray<LtButton> swatches;
        juce::ColourSelector selector { juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders, 4, 4 };
        LtButton reset { "Back to the default colour" };

        void set (juce::Colour c) { proc.soundColours[filePath] = c; if (actions.coloursChanged) actions.coloursChanged(); }
        void done() { if (actions.coloursChanged) actions.coloursChanged(); if (auto* box = findParentComponentOfClass<juce::CallOutBox>()) box->dismiss(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { set (selector.getCurrentColour()); }
    };
}

SoundRow::SoundRow (SedimentsProcessor& p, EditorActions& a, const juce::String& filePath)
    : path (filePath), proc (p), actions (a)
{
    name = juce::File (path).getFileName();
    for (auto* b : { &swatch, &preview, &remove })
    {
        b->setIconOnly (true);
        addAndMakeVisible (*b);
    }
    swatch.setTitle ("Change the colour of " + name);
    swatch.setTooltip ("Change colour");
    preview.setTitle ("Preview " + name);
    remove.setTitle ("Remove " + name + " from the map");
    remove.setTooltip ("Remove from the map");
    swatch.onClick = [this] { openColourChooser(); };
    preview.onClick = [this]
    {
        if (proc.engine.getAuditionPath() == path) proc.engine.stopAudition();
        else proc.engine.auditionAsync (path, 0.0, 0.0);
    };
    remove.onClick = [this] { proc.engine.removeFileAsync (path); };
}

void SoundRow::openColourChooser()
{
    int colourIndex = proc.engine.getColourIndex (path);
    auto content = std::make_unique<ColourChoice> (proc, actions, path, name, colourIndex);
    auto* parent = actions.calloutParent ? actions.calloutParent() : nullptr;
    auto area = parent != nullptr ? parent->getLocalArea (&swatch, swatch.getLocalBounds()) : swatch.getScreenBounds();
    juce::CallOutBox::launchAsynchronously (std::move (content), area, parent);
}

void SoundRow::update (const Corpus* corpus, int frames, const juce::String& auditioning)
{
    meta = frames < 0 ? juce::String ("Waiting...") : juce::String (frames) + " moments";
    if (corpus != nullptr)
        for (auto& src : corpus->sources)
            if (juce::String (src.path) == path)
            {
                meta = formatTime (src.durationSec) + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + meta;
                colour = actions.colourOf ? actions.colourOf (src) : ui().accent();
                shape = actions.shapeOf ? actions.shapeOf (src) : 0;
            }
    const bool playing = auditioning == path;
    preview.setIcon (playing ? "stop" : "play");
    preview.setTitle ((playing ? "Stop preview of " : "Preview ") + name);
    preview.setTooltip (playing ? "Stop" : "Hear the original file");
    repaint();
}

void SoundRow::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    g.setColour (t.raised);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);
    drawGlyph (g, shape, swatch.getBounds().toFloat().getCentre(), 14.0f, colour.isTransparent() ? s.accent() : colour);
    auto text = getLocalBounds().withLeft (swatch.getRight() + 4).withRight (preview.getX() - 4);
    g.setColour (t.fg);
    g.setFont (s.font (14.5f, true));
    g.drawText (name, text.removeFromTop (getHeight() / 2).withTrimmedTop (5), juce::Justification::bottomLeft, true);
    g.setColour (t.muted);
    g.setFont (s.font (12.5f));
    auto m = meta;
    if (juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), m) > (float) text.getWidth())
        m = m.fromLastOccurrenceOf (juce::String::fromUTF8 ("\xc2\xb7  "), false, false);
    g.drawText (m, text.withTrimmedBottom (5), juce::Justification::topLeft, true);
}

void SoundRow::resized()
{
    auto b = getLocalBounds().reduced (2, 2);
    swatch.setBounds (b.removeFromLeft (44));
    remove.setBounds (b.removeFromRight (44));
    preview.setBounds (b.removeFromRight (44));
}

SoundsCard::SoundsCard (SedimentsProcessor& p, EditorActions& a) : proc (p), actions (a)
{
    add.onClick = [this] { if (actions.chooseFiles) actions.chooseFiles(); };
    add.setTooltip ("Add audio files (or drop them anywhere on the window)");
    addAndMakeVisible (add);
    viewport.setViewedComponent (&list, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    addAndMakeVisible (viewport);
    setTitle ("Sounds");
}

void SoundsCard::rebuildColours()
{
    shownCorpus = -1;
}

void SoundsCard::refresh (const std::shared_ptr<const Corpus>& corpus)
{
    auto paths = proc.engine.getFilePaths();
    const int fv = proc.engine.getFileListVersion(), cv = proc.engine.getCorpusVersion();
    const auto auditioning = proc.engine.getAuditionPath();

    if (fv != shownFiles)
    {
        shownFiles = fv;
        rows.clear();
        for (auto& path : paths)
            list.addAndMakeVisible (rows.add (new SoundRow (proc, actions, path)));
        layoutRows();
        shownCorpus = -1;
    }

    std::map<juce::String, int> counts;
    if (corpus != nullptr)
        for (size_t i = 0; i < corpus->sourceIndex.size(); ++i)
            ++counts[juce::String (corpus->sources[(size_t) corpus->sourceIndex[i]].path)];
    for (auto* r : rows)
    {
        auto it = counts.find (r->path);
        const bool encoded = corpus != nullptr && std::any_of (corpus->sources.begin(), corpus->sources.end(),
                                                               [r] (const Corpus::Source& s) { return juce::String (s.path) == r->path; });
        r->update (corpus.get(), encoded ? (it != counts.end() ? it->second : 0) : -1, auditioning);
    }
    shownCorpus = cv;

    const int moments = corpus != nullptr ? corpus->size() : 0;
    summary = juce::String (paths.size()) + (paths.size() == 1 ? " sound" : " sounds")
              + juce::String (juce::CharPointer_UTF8 ("  \xc2\xb7  ")) + juce::String (moments) + " moments";
    auto st = proc.engine.getStatus();
    busyText = proc.engine.isBusy() && st.endsWith ("...") ? st : juce::String();
    repaint();
}

void SoundsCard::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    paintCard (g, getLocalBounds().toFloat());
    g.setColour (t.fg);
    g.setFont (s.font (17.0f, true));
    g.drawText ("Sounds", 16, 12, 200, 24, juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (s.font (13.0f));
    auto sum = busyText.isNotEmpty() ? busyText : summary;
    if (busyText.isEmpty() && juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), sum) > (float) (add.getX() - 24))
        sum = sum.upToFirstOccurrenceOf (juce::String::fromUTF8 ("  \xc2\xb7"), false, false);
    g.drawText (sum, 16, 36, add.getX() - 24, 18, juce::Justification::centredLeft, true);

    // drop hint
    auto drop = getLocalBounds().reduced (16, 12).removeFromBottom (48).toFloat();
    juce::Path r;
    r.addRoundedRectangle (drop, 12.0f);
    juce::Path dashed;
    const float dash[] = { 6.0f, 5.0f };
    juce::PathStrokeType (2.0f).createDashedStroke (dashed, r, dash, 2);
    g.setColour (t.line);
    g.fillPath (dashed);
    Icons::draw (g, "folder", { drop.getX() + 14.0f, drop.getCentreY() - 10.0f, 20.0f, 20.0f }, t.muted, 2.0f);
    g.setColour (t.muted);
    g.setFont (s.font (13.0f));
    g.drawFittedText ("Drop audio files or a folder anywhere", drop.withTrimmedLeft (44.0f).withTrimmedRight (8.0f).toNearestInt(), juce::Justification::centredLeft, 2, 1.0f);
}

void SoundsCard::resized()
{
    auto b = getLocalBounds().reduced (16, 12);
    auto head = b.removeFromTop (46);
    add.setBounds (head.removeFromRight (add.idealWidth (44)).withSizeKeepingCentre (add.idealWidth (44), 44));
    b.removeFromTop (8);
    b.removeFromBottom (56);
    viewport.setBounds (b);
    layoutRows();
}

void SoundsCard::layoutRows()
{
    const int w = viewport.getWidth() - (rows.size() * 50 > viewport.getHeight() ? 12 : 0);
    int y = 0;
    for (auto* r : rows) { r->setBounds (0, y, w, 48); y += 50; }
    list.setSize (juce::jmax (1, w), juce::jmax (1, y));
}

//==============================================================================
ShapeCard::ShapeCard (SedimentsProcessor& p)
    : proc (p),
      source (param (p, "source"), "Source", "Model", "Your originals", [] (float v)
      {
          return v < 0.005f ? juce::String ("Model") : (v > 0.995f ? juce::String ("Originals") : pct (v) + " originals");
      }),
      focus (param (p, "blur"), "Focus", "Sharp", "Blended", [] (float v) { return pct (v); }),
      terrain (param (p, "terrain"), "Terrain", "Your sounds", "Learned", [] (float v) { return v < 0.005f ? juce::String ("Your sounds") : pct (v) + " learned"; }),
      glide (param (p, "glide"), "Glide", "Instant", "Slow", [] (float v)
      {
          const float tau = 0.0005f + 3.0f * v * v;
          return tau < 0.01f ? juce::String ("Instant") : juce::String (tau, tau < 1.0f ? 2 : 1) + " s";
      }),
      flow (param (p, "flow"), "Flow", "Frozen", "Breathing", [] (float v) { return v < 0.005f ? juce::String ("Off") : pct (v); }),
      grain (param (p, "grain"), "Grain size", "Short", "Long", [] (float v) { return v < 1000.0f ? juce::String (juce::roundToInt (v)) + " ms" : juce::String (v / 1000.0f, 1) + " s"; }),
      flowSpeed (param (p, "flowSpeed"), "Flow speed", "Stretched", "Faster", [] (float v) { return juce::String (v, 2) + juce::String::fromUTF8 ("\xc3\x97"); }),
      dry (param (p, "dry"), "Input", "Off", "Let it through", [] (float v) { return v < 0.005f ? juce::String ("Off") : pct (v); })
{
    for (auto* s : { &source, &focus, &terrain, &glide, &flow, &grain, &flowSpeed, &dry })
        addChildComponent (s);
    source.slider.setTooltip ("Model: the model replays everything. Originals: your recordings themselves, cut into grains (concatenative synthesis).");
    terrain.slider.setTooltip ("Your sounds: blends the moments around the cursor. Learned: a small network invents what lies between them.");
    flow.slider.setTooltip ("Let each sound keep moving the way it did in the recording, even when the cursor is still");
    grain.slider.setTooltip ("Length of the pieces of your recordings that are played (originals only)");
    addAndMakeVisible (more);
    more.setTitle ("More settings: flow speed and input");
    more.onClick = [this]
    {
        expanded = ! expanded;
        more.setIcon (expanded ? "chevronUp" : "chevronDown");
        more.setButtonText (expanded ? "Less" : "More");
        resized();
        if (onLayoutChange) onLayoutChange();
    };
    refresh();
}

std::vector<LabeledSlider*> ShapeCard::visibleSliders() const
{
    std::vector<LabeledSlider*> v;
    auto* self = const_cast<ShapeCard*> (this);
    if (hasModel) v.push_back (&self->source);
    v.push_back (&self->focus);
    if (hasModel) v.push_back (&self->terrain);
    v.push_back (&self->glide);
    v.push_back (&self->flow);
    if (usesOriginals) v.push_back (&self->grain);
    if (expanded) { v.push_back (&self->flowSpeed); v.push_back (&self->dry); }
    return v;
}

void ShapeCard::refresh()
{
    const bool m = proc.engine.getModelDescription().isNotEmpty();
    const bool o = ! m || proc.engine.sourceMix.load() > 0.001f;
    if (m != hasModel || o != usesOriginals)
    {
        hasModel = m;
        usesOriginals = o;
        resized();
        if (onLayoutChange) onLayoutChange();
    }
}

int ShapeCard::preferredHeight() const
{
    return 12 + 32 + (int) visibleSliders().size() * (66 + 4) + 44 + 12;
}

void ShapeCard::paint (juce::Graphics& g)
{
    paintCard (g, getLocalBounds().toFloat());
    g.setColour (ui().theme().fg);
    g.setFont (ui().font (17.0f, true));
    g.drawText ("Shape the sound", 16, 12, getWidth() - 32, 24, juce::Justification::centredLeft);
}

void ShapeCard::resized()
{
    auto b = getLocalBounds().reduced (16, 12);
    b.removeFromTop (32);
    auto shown = visibleSliders();
    for (auto* s : { &source, &focus, &terrain, &glide, &flow, &grain, &flowSpeed, &dry })
        s->setVisible (std::find (shown.begin(), shown.end(), s) != shown.end());
    for (auto* s : shown)
    {
        s->setBounds (b.removeFromTop (66));
        b.removeFromTop (4);
    }
    more.setBounds (b.removeFromTop (44));
}

//==============================================================================
WelcomeView::WelcomeView (SedimentsProcessor& p, EditorActions& a) : proc (p), actions (a)
{
    chooseModel.onClick = [this] { if (actions.chooseModel) actions.chooseModel(); };
    noModel.onClick = [this] { if (actions.useWithoutModel) actions.useWithoutModel(); };
    noModel.setTooltip ("Play your recordings themselves, cut into small grains (concatenative synthesis). You can add a model later.");
    addAndMakeVisible (noModel);
    addFiles.onClick = [this] { if (actions.chooseFiles) actions.chooseFiles(); };
    findModels.setFont (ui().font (15.0f, true), false, juce::Justification::centredLeft);
    for (auto* c : { (juce::Component*) &chooseModel, (juce::Component*) &findModels, (juce::Component*) &addFiles })
        addAndMakeVisible (c);
    setTitle ("Welcome to Sediments");
    addFiles.setEnabled (false);
    addFiles.setTooltip ("Choose a model first");
}

void WelcomeView::refresh()
{
    const bool m = proc.engine.getModelDescription().isNotEmpty() || proc.engine.isWithoutModel();
    const bool wm = proc.engine.isWithoutModel();
    auto st = proc.engine.getStatus();
    const bool isError = st.startsWith ("Could not") || st.startsWith ("This model") || st.startsWith ("Model not found")
                         || st.startsWith ("Unsupported") || st.contains ("too short") || st.startsWith ("Unexpected") || st.startsWith ("Model is missing");
    juce::String newStatus = proc.engine.isBusy() ? st : (isError ? st : juce::String());
    if (! initialised || m != hasModel || wm != withoutModel || newStatus != status)
    {
        initialised = true;
        hasModel = m;
        withoutModel = wm;
        status = newStatus;
        addFiles.setEnabled (hasModel);
        noModel.setVisible (! withoutModel);
        chooseModel.setButtonText (withoutModel ? "Use a model instead..." : (hasModel ? "Choose another model..." : "Choose a model..."));
        chooseModel.setStyle (hasModel ? LtButton::Style::secondary : LtButton::Style::primary);
        addFiles.setStyle (hasModel ? LtButton::Style::primary : LtButton::Style::secondary);
        findModels.setFont (ui().font (15.0f, true), false, juce::Justification::centredLeft);
        resized();
        repaint();
    }
}

void WelcomeView::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    const auto acc = s.accent();
    g.setColour (t.panel);
    g.fillRoundedRectangle (card.toFloat(), 24.0f);
    g.setColour (dragHighlight ? acc : t.line);
    g.drawRoundedRectangle (card.toFloat().reduced (0.5f), 24.0f, dragHighlight ? 3.0f : 1.0f);

    // illustration
    {
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRoundedRectangle ((float) illus.getX(), (float) illus.getY(), (float) illus.getWidth(), (float) illus.getHeight(), 24.0f, 24.0f, true, false, true, false);
        g.reduceClipRegion (clip);
        g.setColour (t.map);
        g.fillRect (illus);
        const auto o = illus.getPosition().toFloat();
        g.setColour (t.contour.withMultipliedAlpha (1.6f));
        auto ell = [&] (float cx, float cy, float rx, float ry, float rot)
        {
            juce::Path p;
            p.addEllipse (-rx, -ry, rx * 2, ry * 2);
            g.strokePath (p, juce::PathStrokeType (1.5f), juce::AffineTransform::rotation (rot).translated (o.x + cx, o.y + cy));
        };
        for (int i = 0; i < 3; ++i) ell (150.0f + i * 6, 190.0f, 130.0f - i * 42.0f, 96.0f - i * 33.0f, -0.3f);
        for (int i = 0; i < 3; ++i) ell (280.0f + i * 6, 352.0f, 120.0f - i * 43.0f, 86.0f - i * 32.0f, 0.42f);
        const float dots[][3] = { { 70, 150, 0 }, { 96, 170, 0 }, { 128, 140, 0 }, { 176, 160, 0 }, { 210, 200, 0 }, { 150, 230, 0 },
                                  { 250, 330, 2 }, { 282, 362, 2 }, { 300, 320, 2 }, { 318, 388, 2 }, { 270, 400, 2 },
                                  { 96, 420, 1 }, { 130, 446, 1 }, { 70, 470, 1 } };
        for (auto& d : dots)
            drawGlyph (g, shapeFor ((int) d[2], s.shapes), { o.x + d[0], o.y + d[1] }, 10.0f, s.soundColour ((int) d[2]));
        juce::Path walk;
        walk.startNewSubPath (o.x + 120, o.y + 200);
        walk.cubicTo (o.x + 170, o.y + 150, o.x + 240, o.y + 230, o.x + 270, o.y + 320);
        juce::Path dashed;
        const float dash[] = { 4.0f, 7.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, walk, dash, 2);
        g.setColour (acc);
        g.fillPath (dashed);
        g.fillEllipse (o.x + 112, o.y + 192, 16, 16);
        g.setColour (t.muted);
        g.setFont (s.font (15.0f));
        g.drawFittedText ("Similar sounds settle close together, like places on a map. You wander between them.",
                          illus.reduced (28).removeFromBottom (60), juce::Justification::bottomLeft, 3);
    }

    auto right = card.withTrimmedLeft (illus.getWidth()).reduced (40, 36);
    g.setColour (t.fg);
    g.setFont (s.font (30.0f, true));
    g.drawText ("Welcome to Sediments", right.removeFromTop (40), juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (s.font (17.0f));
    g.drawText ("Three steps and you are playing. Nothing here can break.", right.removeFromTop (28), juce::Justification::centredLeft);

    struct StepInfo { juce::Rectangle<int> r; int state; const char* num; const char* title; const char* text; };
    const StepInfo steps[] = {
        { step1, hasModel ? 2 : 1, "1", withoutModel ? "No model: your original sounds" : "Choose a sound model",
          withoutModel ? "Your recordings are played as they are, in small grains. You can add a model later."
                       : "A model replays everything with its own timbre. Or skip it and play your recordings as they are." },
        { step2, hasModel ? 1 : 0, "2", "Add your sounds", "Drop recordings to draw the map. A few minutes of sound is plenty to start." },
        { step3, 0, "3", "Play", "Three ways in, one tab each: Explore, Gesture and Keys." }
    };
    for (auto& st : steps)
    {
        auto r = st.r.toFloat();
        g.setColour (st.state == 1 ? t.raised : t.panel);
        g.fillRoundedRectangle (r, 16.0f);
        g.setColour (st.state == 1 ? acc : t.line);
        g.drawRoundedRectangle (r.reduced (st.state == 1 ? 1.0f : 0.5f), 16.0f, st.state == 1 ? 2.0f : 1.0f);
        auto badge = juce::Rectangle<float> (r.getX() + 18.0f, r.getY() + 18.0f, 36.0f, 36.0f);
        if (st.state >= 1)
        {
            g.setColour (acc);
            g.fillEllipse (badge);
            if (st.state == 2) Icons::draw (g, "check", badge.reduced (9.0f), s.onAccent(), 3.2f);
            else { g.setColour (s.onAccent()); g.setFont (s.font (16.0f, true)); g.drawText (st.num, badge, juce::Justification::centred); }
        }
        else
        {
            g.setColour (t.line);
            g.drawEllipse (badge.reduced (1.0f), 2.0f);
            g.setColour (t.muted);
            g.setFont (s.font (16.0f, true));
            g.drawText (st.num, badge, juce::Justification::centred);
        }
        auto text = r.withTrimmedLeft (70.0f).withTrimmedTop (16.0f).withTrimmedRight (16.0f);
        g.setColour (t.fg);
        g.setFont (s.font (18.0f, true));
        g.drawText (st.title, text.removeFromTop (26.0f), juce::Justification::centredLeft);
        g.setColour (t.muted);
        g.setFont (s.font (15.0f));
        g.drawFittedText (st.text, text.removeFromTop (40.0f).toNearestInt(), juce::Justification::topLeft, 2);
    }

    // drop zone in step 2
    {
        juce::Path r;
        r.addRoundedRectangle (drop.toFloat(), 12.0f);
        juce::Path dashed;
        const float dash[] = { 6.0f, 5.0f };
        juce::PathStrokeType (2.0f).createDashedStroke (dashed, r, dash, 2);
        g.setColour (hasModel ? acc.withAlpha (0.7f) : t.line);
        g.fillPath (dashed);
        g.setColour (t.muted);
        g.setFont (s.font (14.0f));
        g.drawText (hasModel ? juce::String::fromUTF8 ("Drop audio files or a folder here  \xc2\xb7  WAV, AIFF, FLAC, MP3, OGG") : "Unlocks after step 1",
                    drop.reduced (14, 0).withTrimmedRight (addFiles.getWidth() + 8), juce::Justification::centredLeft, true);
    }

    // mode chips in step 3
    {
        float x = (float) step3.getX() + 70.0f;
        const float y = (float) step3.getBottom() - 50.0f;
        const char* chips[][2] = { { "compass", "Explore" }, { "gesture", "Gesture" }, { "keys", "Keys" } };
        for (auto& c : chips)
        {
            g.setFont (s.font (14.0f, true));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), c[1]) + 46.0f;
            auto chip = juce::Rectangle<float> (x, y, w, 34.0f);
            g.setColour (t.raised);
            g.fillRoundedRectangle (chip, 17.0f);
            Icons::draw (g, c[0], { chip.getX() + 12.0f, chip.getCentreY() - 8.0f, 16.0f, 16.0f }, t.fg, 2.0f);
            g.setColour (t.fg);
            g.drawText (c[1], chip.withTrimmedLeft (34.0f), juce::Justification::centredLeft);
            x += w + 10.0f;
        }
    }

    if (status.isNotEmpty())
    {
        g.setColour (t.muted);
        g.setFont (s.font (14.0f));
        g.drawText (status, card.withTrimmedLeft (illus.getWidth() + 40).removeFromBottom (30).withTrimmedRight (40), juce::Justification::centredLeft, true);
    }
}

void WelcomeView::resized()
{
    auto b = getLocalBounds();
    card = b.withSizeKeepingCentre (juce::jmin (1060, b.getWidth()), juce::jmin (600, b.getHeight()));
    illus = card.withWidth (card.getWidth() > 900 ? 380 : 0);
    auto right = card.withTrimmedLeft (illus.getWidth()).reduced (40, 36);
    right.removeFromTop (78);
    step1 = right.removeFromTop (152); right.removeFromTop (12);
    step2 = right.removeFromTop (152); right.removeFromTop (12);
    step3 = right.removeFromTop (130);

    const int bx = step1.getX() + 70, by = step1.getBottom() - 62;
    chooseModel.setBounds (bx, by, chooseModel.idealWidth (48), 48);
    int nx = chooseModel.getRight() + 10;
    if (noModel.isVisible())
    {
        noModel.setBounds (nx, by, noModel.idealWidth (48), 48);
        nx = noModel.getRight() + 10;
    }
    findModels.setBounds (nx, by, juce::jmax (120, step1.getRight() - 12 - nx), 48);
    findModels.setButtonText (step1.getRight() - nx > 230 ? "Where to find free models" : "Free models");
    drop = juce::Rectangle<int> (step2.getX() + 70, step2.getBottom() - 66, step2.getWidth() - 86, 52);
    addFiles.setBounds (drop.getRight() - addFiles.idealWidth (44) - 6, drop.getCentreY() - 22, addFiles.idealWidth (44), 44);
}

//==============================================================================
static const char* axisNames[] = { "Similarity", "Pitch", "Brightness", "Noisiness", "Loudness" };

juce::String LayoutChooser::describe (int x, int y)
{
    if (x == 0 && y == 0) return "Map: similarity";
    return juce::String ("Map: ") + axisNames[juce::jlimit (0, 4, x)] + " / " + axisNames[juce::jlimit (0, 4, y)];
}

LayoutChooser::LayoutChooser (SedimentsProcessor& p)
    : proc (p),
      xAxis ({ "Similarity", "Pitch", "Brightness", "Noisiness", "Loudness" }, "Left to right"),
      yAxis ({ "Similarity", "Pitch", "Brightness", "Noisiness", "Loudness" }, "Bottom to top")
{
    xAxis.setSelected (proc.engine.getLayoutX());
    yAxis.setSelected (proc.engine.getLayoutY());
    xAxis.onChange = [this] (int) { apply(); };
    yAxis.onChange = [this] (int) { apply(); };
    addAndMakeVisible (xAxis);
    addAndMakeVisible (yAxis);
    setSize (juce::jmax (xAxis.preferredWidth(), 520) + 24, 250);
}

void LayoutChooser::apply()
{
    proc.engine.setLayoutAxesAsync (xAxis.getSelected(), yAxis.getSelected());
}

void LayoutChooser::paint (juce::Graphics& g)
{
    auto& s = ui();
    g.setColour (s.theme().fg);
    g.setFont (s.font (16.0f, true));
    g.drawText ("How the map is laid out", 12, 8, getWidth() - 24, 24, juce::Justification::centredLeft);
    g.setFont (s.font (14.0f, true));
    g.drawText ("Left to right", 12, 40, 200, 20, juce::Justification::centredLeft);
    g.drawText ("Bottom to top", 12, 116, 200, 20, juce::Justification::centredLeft);
    g.setColour (s.theme().muted);
    g.setFont (s.font (13.0f));
    g.drawFittedText ("Similarity: sounds that are alike sit together (as the model hears them, or by their overall character without a model). "
                      "The other choices sort moments by one property, low to high.",
                      12, 192, getWidth() - 24, 54, juce::Justification::topLeft, 3, 1.0f);
}

void LayoutChooser::resized()
{
    xAxis.setBounds (12, 62, getWidth() - 24, 46);
    yAxis.setBounds (12, 138, getWidth() - 24, 46);
}

//==============================================================================
SettingsPanel::Content::Content()
    : theme ({ "Dusk", "Daylight", "High contrast", "Night" }, "Theme"),
      palette ({ "Standard", "Red-green safe", "Blue-yellow safe" }, "Sound colours"),
      font ({ "Atkinson Hyperlegible", "System", "OpenDyslexic" }, "Font"),
      size ({ "100%", "125%", "150%" }, "Text size"),
      spacing ({ "Normal", "Wide", "Wider" }, "Letter spacing"),
      shapes ("Shapes as well as colours", "Each sound gets its own marker shape, so colour is never the only clue."),
      calm ("Calm motion", "No gliding or animation. The cursor jumps straight to where the sound is."),
      announce ("Announce what you hear", "Screen readers say which sounds are in the mix as you move.")
{
    font.setOptionFont (0, "atkinson");
    font.setOptionFont (1, "system");
    font.setOptionFont (2, "opendyslexic");
    for (auto* c : { &theme, &palette, &font, &size, &spacing })
    {
        addAndMakeVisible (c);
        c->onChange = [this] (int) { apply(); };
    }
    const juce::uint32 accentChoices[] = { 0, 0xFFF0B45E, 0xFFF28B82, 0xFFF48FB1, 0xFFB39DDB, 0xFF8AB4F8, 0xFF7FC8BA, 0xFFA5D6A7 };
    for (int i = 0; i < 8; ++i)
    {
        auto* b = accents.add (new LtButton (i == 0 ? "Theme colour" : "Accent colour " + juce::String (i), {}, LtButton::Style::swatch));
        b->setRadioGroupId (8811);
        b->setClickingTogglesState (true);
        b->getProperties().set ("argb", (juce::int64) accentChoices[i]);
        b->onClick = [this, b, i]
        {
            if (! b->getToggleState()) return;
            ui().customAccent = i == 0 ? juce::Colour() : juce::Colour ((juce::uint32) (juce::int64) b->getProperties()["argb"]);
            ui().save();
        };
        addAndMakeVisible (b);
    }
    customAccent.onClick = [this]
    {
        auto sel = std::make_unique<juce::ColourSelector> (juce::ColourSelector::showColourspace | juce::ColourSelector::showSliders);
        sel->setCurrentColour (ui().accent());
        sel->setSize (280, 240);
        struct L : juce::ChangeListener
        {
            void changeListenerCallback (juce::ChangeBroadcaster* b) override
            {
                if (auto* cs = dynamic_cast<juce::ColourSelector*> (b)) { ui().customAccent = cs->getCurrentColour(); ui().save(); }
            }
        };
        static L listener;
        sel->addChangeListener (&listener);
        auto* top = getTopLevelComponent();
        juce::CallOutBox::launchAsynchronously (std::move (sel), top->getLocalArea (&customAccent, customAccent.getLocalBounds()), top);
    };
    fixContrast.onClick = [this]
    {
        ui().customAccent = ensureContrast (ui().accent(), ui().theme().panel, 4.5);
        ui().save();
    };
    addAndMakeVisible (customAccent);
    addAndMakeVisible (fixContrast);
    for (auto* c : { &shapes, &calm, &announce })
    {
        addAndMakeVisible (c);
        c->onClick = [this] { apply(); };
    }
}

void SettingsPanel::Content::apply()
{
    auto& s = ui();
    s.themeId = themeIds()[theme.getSelected()];
    s.paletteId = paletteIds()[palette.getSelected()];
    static const char* fonts[] = { "atkinson", "system", "opendyslexic" };
    s.fontId = fonts[font.getSelected()];
    static const float sizes[] = { 1.0f, 1.25f, 1.5f };
    s.textScale = sizes[size.getSelected()];
    static const float spacings[] = { 0.0f, 0.025f, 0.05f };
    s.spacing = spacings[spacing.getSelected()];
    s.shapes = shapes.getToggleState();
    s.calm = calm.getToggleState();
    s.announce = announce.getToggleState();
    s.save();
}

int SettingsPanel::Content::layout (int width)
{
    const int w = width;
    int y = 4;
    auto heading = [&] (const juce::String& name) { headingY[name] = y; y += 28; };
    heading ("Theme");
    theme.setBounds (0, y, w, 46); y += 46 + 22;
    heading ("Sound colours");
    palette.setBounds (0, y, w, 46); y += 52;
    paletteY = y; y += 30 + 18;
    heading ("Accent colour");
    int x = 0;
    for (auto* b : accents) { b->setBounds (x, y, 44, 44); x += 46; }
    customAccent.setBounds (juce::jmin (x + 4, w - customAccent.idealWidth (44)), y, customAccent.idealWidth (44), 44);
    if (customAccent.getRight() > w) { y += 50; customAccent.setBounds (0, y, customAccent.idealWidth (44), 44); }
    y += 52;
    accentNoteY = y; y += 26;
    fixContrast.setBounds (0, y, fixContrast.idealWidth (40), 40);
    y += fixContrast.isVisible() ? 52 : 4;
    y += 10;
    heading ("Font");
    font.setBounds (0, y, w, 46); y += 50;
    fontNoteY = y; y += 44 + 14;
    heading ("Text size");
    size.setBounds (0, y, juce::jmin (w, 360), 46); y += 50;
    sizeNoteY = y; y += 24 + 14;
    heading ("Letter spacing");
    spacing.setBounds (0, y, juce::jmin (w, 360), 46); y += 46 + 22;
    heading ("Seeing and hearing");
    for (auto* c : { &shapes, &calm, &announce })
    {
        const int h = c->preferredHeight (w);
        c->setBounds (0, y, w, h);
        y += h + 6;
    }
    y += 16;
    heading ("Keyboard");
    keysY = y;
    y += 9 * 34 + 10;
    return y;
}

void SettingsPanel::Content::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    g.setColour (t.fg);
    g.setFont (s.font (16.0f, true));
    for (auto& h : headingY)
        g.drawText (h.first, 0, h.second, getWidth(), 24, juce::Justification::centredLeft);

    // palette preview
    for (int i = 0; i < 8; ++i)
    {
        juce::Rectangle<float> cell ((float) i * 40.0f, (float) paletteY, 34.0f, 30.0f);
        g.setColour (t.map);
        g.fillRoundedRectangle (cell, 8.0f);
        drawGlyph (g, shapeFor (i, s.shapes), cell.getCentre(), 13.0f, s.soundColour (i));
    }

    const double cr = contrastRatio (s.accent(), t.panel);
    const bool ok = cr >= 4.5;
    g.setColour (ok ? t.muted : t.rec);
    g.setFont (s.font (14.0f));
    g.drawText ("Contrast with the panels: " + juce::String (cr, 1) + ":1  " + (ok ? juce::String (juce::String::fromUTF8 ("\xc2\xb7 easy to read")) : juce::String (juce::String::fromUTF8 ("\xc2\xb7 too low for text"))),
                0, accentNoteY, getWidth(), 22, juce::Justification::centredLeft, true);

    g.setColour (t.muted);
    g.setFont (s.font (14.0f));
    g.drawFittedText ("Atkinson Hyperlegible keeps look-alike letters apart (I, l, 1). Bigger text and more spacing usually help more than a special font.",
                      0, fontNoteY, getWidth(), 44, juce::Justification::topLeft, 3, 1.0f);
    g.drawText ("The window grows with the text, so nothing gets cut off.", 0, sizeNoteY, getWidth(), 22, juce::Justification::centredLeft, true);

    const char* keys[][2] = { { "Arrows", "Move the cursor (when it has focus)" }, { "Shift + arrows", "Move in fine steps" },
                              { "Space", "Hear the nearest original moment" }, { "Shift + click", "Hear a moment on the map" },
                              { "R", "Record a gesture" }, { "1  2  3", "Explore, Gesture, Keys" }, { "Tab", "Go to the next control" },
                              { "?", "Help" }, { "Esc", "Close this panel" } };
    int y = keysY;
    for (auto& k : keys)
    {
        g.setColour (t.line);
        g.fillRect (0, y, getWidth(), 1);
        const float kw = keycapWidth (k[0]);
        drawKeycap (g, { 0.0f, (float) y + 6.0f, kw, 24.0f }, k[0]);
        g.setColour (t.fg);
        g.setFont (s.font (14.5f));
        g.drawText (k[1], 150, y, getWidth() - 150, 34, juce::Justification::centredLeft, true);
        y += 34;
    }
}

SettingsPanel::SettingsPanel (SedimentsProcessor& p) : proc (p)
{
    setTitle ("Display and accessibility");
    setWantsKeyboardFocus (true);
    close.setIconOnly (true);
    close.onClick = [this] { if (onClose) onClose(); };
    done.onClick = [this] { if (onClose) onClose(); };
    reset.onClick = [this] { ui().resetToDefaults(); syncFromSettings(); };
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (10);
    for (auto* c : { (juce::Component*) &close, (juce::Component*) &reset, (juce::Component*) &done, (juce::Component*) &viewport })
        addAndMakeVisible (c);
    syncFromSettings();
}

void SettingsPanel::syncFromSettings()
{
    auto& s = ui();
    content.theme.setSelected (juce::jmax (0, themeIds().indexOf (s.themeId)));
    content.palette.setSelected (juce::jmax (0, paletteIds().indexOf (s.paletteId)));
    content.font.setSelected (s.fontId == "system" ? 1 : (s.fontId == "opendyslexic" ? 2 : 0));
    content.size.setSelected (s.textScale > 1.4f ? 2 : (s.textScale > 1.1f ? 1 : 0));
    content.spacing.setSelected (s.spacing > 0.04f ? 2 : (s.spacing > 0.01f ? 1 : 0));
    content.shapes.setToggleState (s.shapes, juce::dontSendNotification);
    content.calm.setToggleState (s.calm, juce::dontSendNotification);
    content.announce.setToggleState (s.announce, juce::dontSendNotification);
    int match = 0;
    for (int i = 1; i < content.accents.size(); ++i)
        if (! s.customAccent.isTransparent() && s.customAccent == juce::Colour ((juce::uint32) (juce::int64) content.accents[i]->getProperties()["argb"]))
            match = i;
    for (int i = 0; i < content.accents.size(); ++i)
    {
        auto* b = content.accents[i];
        const auto c = i == 0 ? s.theme().accent : juce::Colour ((juce::uint32) (juce::int64) b->getProperties()["argb"]);
        b->setSwatch (c);
        b->setToggleState (s.customAccent.isTransparent() ? i == 0 : i == match, juce::dontSendNotification);
    }
    content.fixContrast.setVisible (contrastRatio (s.accent(), s.theme().panel) < 4.5);
    resized();
    repaint();
}

void SettingsPanel::paint (juce::Graphics& g)
{
    auto& t = ui().theme();
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    g.setColour (t.panel);
    g.fillRect (sheet);
    g.setColour (t.line);
    g.fillRect (sheet.getX(), sheet.getY(), 1, sheet.getHeight());
    g.fillRect (sheet.getX(), sheet.getY() + 76, sheet.getWidth(), 1);
    g.fillRect (sheet.getX(), sheet.getBottom() - 76, sheet.getWidth(), 1);
    g.setColour (t.fg);
    g.setFont (ui().font (22.0f, true));
    g.drawText ("Display and accessibility", sheet.getX() + 24, sheet.getY() + 14, sheet.getWidth() - 100, 28, juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (ui().font (14.0f));
    g.drawText ("Saved for every project", sheet.getX() + 24, sheet.getY() + 42, sheet.getWidth() - 100, 20, juce::Justification::centredLeft);
}

void SettingsPanel::resized()
{
    sheet = getLocalBounds().removeFromRight (juce::jmin (500, getWidth()));
    close.setBounds (sheet.getRight() - 24 - 44, sheet.getY() + 16, 44, 44);
    auto footer = juce::Rectangle<int> (sheet.getX() + 24, sheet.getBottom() - 62, sheet.getWidth() - 48, 48);
    done.setBounds (footer.removeFromRight (juce::jmax (110, done.idealWidth (48))));
    reset.setBounds (footer.removeFromLeft (reset.idealWidth (48)));
    viewport.setBounds (sheet.getX() + 24, sheet.getY() + 92, sheet.getWidth() - 36, sheet.getHeight() - 92 - 88);
    const int w = viewport.getWidth() - 16;
    content.setSize (w, content.layout (w));
}

void SettingsPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! sheet.contains (e.getPosition()) && onClose)
        onClose();
}

bool SettingsPanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey) && onClose) { onClose(); return true; }
    return false;
}

//==============================================================================
HelpPanel::HelpPanel()
{
    setTitle ("Help");
    setWantsKeyboardFocus (true);
    close.setIconOnly (true);
    close.onClick = [this] { if (onClose) onClose(); };
    done.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (close);
    addAndMakeVisible (done);
}

void HelpPanel::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    g.fillAll (juce::Colours::black.withAlpha (0.45f));
    paintCard (g, card.toFloat(), 20.0f);
    auto b = card.reduced (32, 24);
    g.setColour (t.fg);
    g.setFont (s.font (24.0f, true));
    g.drawText ("How Sediments works", b.removeFromTop (40), juce::Justification::centredLeft);
    b.removeFromTop (6);
    const char* blocks[][2] = {
        { "The model", "A RAVE model (.ts file) is the voice of the instrument. Everything you hear is played by it, with its own timbre." },
        { "The map", "Your sounds are cut into short moments. Moments that sound alike to the model sit close together." },
        { "Explore", "Move the cursor. Focus says how many neighbours blend, Terrain lets a learned surface fill the gaps, Flow lets sounds keep moving." },
        { "Gesture", "Record a movement on the map and loop it, freely or locked to your project's tempo." },
        { "Keys", "Each note name plays one zone of the map. Play from a MIDI keyboard, a MIDI item, or the on-screen keys." },
        { "Originals", "Without a model, or with Source turned up, your recordings themselves are played in small grains (concatenative synthesis). Grain size sets their length." },
        { "Tips", "Shift-click the map to hear an original moment. Double-click a slider to reset it. Drop files anywhere on the window." }
    };
    for (auto& bl : blocks)
    {
        g.setColour (t.fg);
        g.setFont (s.font (16.0f, true));
        g.drawText (bl[0], b.removeFromTop (24), juce::Justification::centredLeft);
        g.setColour (t.muted);
        g.setFont (s.font (15.0f));
        g.drawFittedText (bl[1], b.removeFromTop (44), juce::Justification::topLeft, 2, 1.0f);
        b.removeFromTop (6);
    }
}

void HelpPanel::resized()
{
    card = getLocalBounds().withSizeKeepingCentre (juce::jmin (700, getWidth() - 40), juce::jmin (700, getHeight() - 40));
    close.setBounds (card.getRight() - 24 - 44, card.getY() + 20, 44, 44);
    done.setBounds (card.getRight() - 32 - 120, card.getBottom() - 24 - 48, 120, 48);
}

void HelpPanel::mouseDown (const juce::MouseEvent& e)
{
    if (! card.contains (e.getPosition()) && onClose)
        onClose();
}

bool HelpPanel::keyPressed (const juce::KeyPress& k)
{
    if (k.isKeyCode (juce::KeyPress::escapeKey) && onClose) { onClose(); return true; }
    return false;
}

//==============================================================================
Banner::Banner()
{
    dismiss.setIconOnly (true);
    dismiss.onClick = [this] { setVisible (false); };
    addAndMakeVisible (dismiss);
    setTitle ("Message");
}

void Banner::show (const juce::String& m)
{
    message = m;
    setVisible (m.isNotEmpty());
    if (m.isNotEmpty())
        juce::AccessibilityHandler::postAnnouncement (m, juce::AccessibilityHandler::AnnouncementPriority::high);
    repaint();
}

void Banner::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (t.rec);
    g.drawRoundedRectangle (r.reduced (1.0f), 12.0f, 2.0f);
    g.setColour (t.fg);
    g.setFont (s.font (14.5f));
    g.drawText (message, getLocalBounds().reduced (16, 0).withTrimmedRight (48), juce::Justification::centredLeft, true);
}

void Banner::resized()
{
    dismiss.setBounds (getLocalBounds().removeFromRight (48).reduced (2));
}
}
