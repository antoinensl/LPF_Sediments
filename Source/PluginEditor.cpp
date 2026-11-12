#include "PluginEditor.h"

using namespace lt;

static bool isAudioFile (const juce::String& path)
{
    return juce::File (path).hasFileExtension ("wav;aif;aiff;flac;mp3;ogg;wave");
}

static bool looksLikeError (const juce::String& st)
{
    return st.startsWith ("Could not") || st.startsWith ("This model") || st.startsWith ("Model not found")
        || st.startsWith ("Model is missing") || st.startsWith ("Unexpected") || st.startsWith ("Decoding error")
        || st.startsWith ("Terrain training failed") || st.contains ("Unsupported or missing") || st.contains ("too short");
}

//==============================================================================
SedimentsEditor::Content::Content (SedimentsEditor& o, SedimentsProcessor& p)
    : owner (o), proc (p),
      top (p, actions), modes (actions), map (p), mix (p, actions), gesture (p, map), keys (p), space (p), room (p),
      sounds (p, actions), shape (p), welcome (p, actions), settingsPanel (p)
{
    setTitle ("Sediments");
    setWantsKeyboardFocus (true);

    actions.chooseModel = [this] { owner.chooseModel(); };
    actions.chooseFiles = [this] { owner.chooseFiles(); };
    actions.useWithoutModel = [this] { proc.engine.useWithoutModelAsync(); };
    actions.openSettings = [this] { owner.showSettings (true); };
    actions.openHelp = [this] { owner.showHelp (true); };
    actions.setMode = [this] (Mode m) { owner.setUiMode ((int) m); };
    actions.colourOf = [this] (const Corpus::Source& src)
    {
        auto it = proc.soundColours.find (juce::String (src.path));
        return ui().soundColour (src.colour, it != proc.soundColours.end() ? it->second : juce::Colour());
    };
    actions.shapeOf = [] (const Corpus::Source& src) { return shapeFor (src.colour, ui().shapes); };
    actions.coloursChanged = [this] { map.invalidateDots(); sounds.rebuildColours(); sounds.refresh (proc.engine.getCorpus()); mix.repaint(); };
    actions.calloutParent = [this] { return (juce::Component*) this; };
    map.colourOf = actions.colourOf;
    map.shapeOf = actions.shapeOf;
    room.colourOf = actions.colourOf;
    room.shapeOf = actions.shapeOf;

    for (auto* c : { (juce::Component*) &top, (juce::Component*) &modes, (juce::Component*) &map, (juce::Component*) &mix,
                     (juce::Component*) &gesture, (juce::Component*) &keys, (juce::Component*) &space, (juce::Component*) &room, (juce::Component*) &sideViewport,
                     (juce::Component*) &welcome, (juce::Component*) &banner, (juce::Component*) &layoutButton })
        addAndMakeVisible (c);
    layoutButton.setTooltip ("Choose how the map is laid out: by similarity, or by pitch, brightness, noisiness or loudness");
    layoutButton.onClick = [this]
    {
        auto chooser = std::make_unique<LayoutChooser> (proc);
        juce::CallOutBox::launchAsynchronously (std::move (chooser), layoutButton.getBounds(), this);
    };
    side.addAndMakeVisible (sounds);
    side.addAndMakeVisible (shape);
    sideViewport.setViewedComponent (&side, false);
    sideViewport.setScrollBarsShown (true, false);
    sideViewport.setScrollBarThickness (10);
    shape.onLayoutChange = [this] { layoutSide(); };
    banner.setVisible (false);

    addChildComponent (settingsPanel);
    addChildComponent (help);
    settingsPanel.onClose = [this] { owner.showSettings (false); };
    help.onClose = [this] { owner.showHelp (false); };
}

void SedimentsEditor::Content::paint (juce::Graphics& g)
{
    g.fillAll (ui().theme().bg);
}

void SedimentsEditor::Content::layoutSide()
{
    const int w = sideViewport.getWidth() - 12;
    const int avail = sideViewport.getHeight();
    const int shapeH = shape.preferredHeight();
    const int soundsH = juce::jmax (260, avail - shapeH - 16);
    sounds.setBounds (0, 0, w, soundsH);
    shape.setBounds (0, soundsH + 16, w, shapeH);
    side.setSize (w, soundsH + 16 + shapeH);
}

void SedimentsEditor::Content::resized()
{
    const int W = getWidth(), H = getHeight();
    top.setBounds (0, 0, W, 68);
    auto body = juce::Rectangle<int> (16, 68 + 16, W - 32, H - 68 - 32);

    settingsPanel.setBounds (getLocalBounds());
    help.setBounds (getLocalBounds());

    roomShown = wantsRoom();
    for (auto* c : { (juce::Component*) &modes, (juce::Component*) &sideViewport })
        c->setVisible (! welcomeShown);
    // In the Space tab the room replaces the map, unless grains are placed by the map.
    map.setVisible (! welcomeShown && ! roomShown);
    layoutButton.setVisible (! welcomeShown && ! roomShown);
    room.setVisible (! welcomeShown && roomShown);
    welcome.setVisible (welcomeShown);
    mix.setVisible (! welcomeShown && mode == Mode::explore);
    gesture.setVisible (! welcomeShown && mode == Mode::gesture);
    keys.setVisible (! welcomeShown && mode == Mode::keys);
    space.setVisible (! welcomeShown && mode == Mode::space);

    if (welcomeShown)
    {
        welcome.setBounds (body);
        banner.setBounds (body.getX() + 12, body.getY(), body.getWidth() - 24, 48);
        return;
    }

    auto sideArea = body.removeFromRight (juce::jlimit (320, 380, W / 4));
    body.removeFromRight (16);
    sideViewport.setBounds (sideArea.withTrimmedRight (-12));
    layoutSide();

    modes.setBounds (body.removeFromTop (48));
    body.removeFromTop (10);
    juce::Component* panel = nullptr;
    int panelH = 0;
    if (mode == Mode::explore) { panel = &mix; panelH = 64; }
    if (mode == Mode::gesture) { panel = &gesture; panelH = gesture.preferredHeight (body.getWidth()); }
    if (mode == Mode::keys)    { panel = &keys; panelH = keys.preferredHeight (body.getWidth()); }
    if (mode == Mode::space)   { panel = &space; panelH = space.preferredHeight (body.getWidth()); }
    if (panel != nullptr)
    {
        panel->setBounds (body.removeFromBottom (panelH));
        body.removeFromBottom (10);
    }
    map.setBounds (body);
    room.setBounds (body);
    const int lw = juce::jmin (layoutButton.idealWidth (40), body.getWidth() / 2);
    layoutButton.setBounds (body.getRight() - 12 - lw, body.getY() + 12, lw, 40);
    banner.setBounds (body.getX() + 12, body.getY() + 60, body.getWidth() - 24, 48);
}

bool SedimentsEditor::Content::wantsRoom() const
{
    return mode == Mode::space
           && (int) proc.params.getRawParameterValue ("place")->load() != (int) Spatial::PlaceBy::map;
}

bool SedimentsEditor::Content::keyPressed (const juce::KeyPress& k)
{
    if (settingsPanel.isVisible() || help.isVisible())
    {
        if (k.isKeyCode (juce::KeyPress::escapeKey)) { owner.showSettings (false); owner.showHelp (false); return true; }
        return false;
    }
    const auto ch = k.getTextCharacter();
    if (ch == '?' || k.isKeyCode (juce::KeyPress::F1Key)) { owner.showHelp (true); return true; }
    if (welcomeShown)
        return false;
    if (ch == '1') { owner.setUiMode (0); return true; }
    if (ch == '2') { owner.setUiMode (1); return true; }
    if (ch == '3') { owner.setUiMode (2); return true; }
    if (ch == '4') { owner.setUiMode (3); return true; }
    if (ch == 'r' || ch == 'R')
    {
        owner.setUiMode (1);
        gesture.toggleRecord();
        return true;
    }
    if (k.isKeyCode (juce::KeyPress::spaceKey)) { map.hearNearestToCursor(); return true; }
    if (! map.isVisible())
        return false;
    if (k.isKeyCode (juce::KeyPress::leftKey) || k.isKeyCode (juce::KeyPress::rightKey)
        || k.isKeyCode (juce::KeyPress::upKey) || k.isKeyCode (juce::KeyPress::downKey))
    {
        map.cursor.grabKeyboardFocus();
        return map.cursor.keyPressed (k);
    }
    return false;
}

//==============================================================================
SedimentsEditor::SedimentsEditor (SedimentsProcessor& p)
    : AudioProcessorEditor (&p), proc (p), look (*settings)
{
    setLookAndFeel (&look);
    settings->addChangeListener (this);
    content = std::make_unique<Content> (*this, p);
    addAndMakeVisible (*content);
    tooltips = std::make_unique<juce::TooltipWindow> (content.get(), 700);

    content->mode = (Mode) juce::jlimit (0, 3, proc.uiMode);
    content->modes.setMode (content->mode);
    content->map.setMode (content->mode);

    setResizable (true, true);
    scale = settings->textScale;
    applyScale();
    timerCallback();
    startTimerHz (30);
}

SedimentsEditor::~SedimentsEditor()
{
    stopTimer();
    settings->removeChangeListener (this);
    tooltips.reset();
    content.reset();
    setLookAndFeel (nullptr);
}

void SedimentsEditor::applyScale()
{
    applyingScale = true;
    scale = settings->textScale;
    setResizeLimits (juce::roundToInt (980 * scale), juce::roundToInt (700 * scale), 3200, 2200);
    setSize (juce::roundToInt (proc.uiWidth * scale), juce::roundToInt (proc.uiHeight * scale));
    applyingScale = false;
    resized();
}

void SedimentsEditor::paint (juce::Graphics& g)
{
    g.fillAll (settings->theme().bg);
}

void SedimentsEditor::resized()
{
    if (content == nullptr)
        return;
    const int w = juce::roundToInt (getWidth() / scale), h = juce::roundToInt (getHeight() / scale);
    if (! applyingScale)
    {
        proc.uiWidth = w;
        proc.uiHeight = h;
    }
    content->setTransform (juce::AffineTransform::scale (scale));
    content->setBounds (0, 0, w, h);
}

void SedimentsEditor::setUiMode (int m)
{
    content->mode = (Mode) juce::jlimit (0, 3, m);
    proc.uiMode = (int) content->mode;
    content->modes.setMode (content->mode);
    content->map.setMode (content->mode);
    content->resized();
}

void SedimentsEditor::showSettings (bool show)
{
    if (show) content->help.setVisible (false);
    content->settingsPanel.syncFromSettings();
    content->settingsPanel.setVisible (show);
    if (show) { content->settingsPanel.toFront (true); content->settingsPanel.grabKeyboardFocus(); }
    else content->grabKeyboardFocus();
}

void SedimentsEditor::showHelp (bool show)
{
    if (show) content->settingsPanel.setVisible (false);
    content->help.setVisible (show);
    if (show) { content->help.toFront (true); content->help.grabKeyboardFocus(); }
    else content->grabKeyboardFocus();
}

// Fonts and sizes changed: every component must measure its text again.
static void relayoutTree (juce::Component& c)
{
    c.resized();
    for (auto* child : c.getChildren())
        relayoutTree (*child);
}

void SedimentsEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    look.refreshColours();
    content->settingsPanel.syncFromSettings();
    content->sounds.rebuildColours();
    content->map.invalidateDots();
    content->welcome.refresh();
    if (std::abs (settings->textScale - scale) > 0.01f)
        applyScale();
    content->resized();
    relayoutTree (*content);
    content->repaint();
    repaint();
}

void SedimentsEditor::timerCallback()
{
    auto& c = *content;
    auto& e = proc.engine;

    // Reflect host automation of X/Y when the user isn't dragging.
    if (! c.map.isMouseButtonDown())
    {
        e.posX = proc.params.getRawParameterValue ("x")->load();
        e.posY = 1.0f - proc.params.getRawParameterValue ("y")->load();
    }

    const bool welcome = (e.getModelDescription().isEmpty() && ! e.isWithoutModel()) || e.getFilePaths().isEmpty();
    if (welcome != c.welcomeShown)
    {
        c.welcomeShown = welcome;
        c.resized();
    }

    auto corpus = e.getCorpus();
    c.top.refresh();
    if (welcome)
        c.welcome.refresh();
    else
    {
        c.map.refresh();
        c.mix.refresh (c.map.getCorpus());
        c.gesture.refresh();
        c.keys.refresh();
        if (c.mode == Mode::space)
            c.space.refresh();
        if (c.wantsRoom() != c.roomShown)
            c.resized();
        if (c.roomShown)
            c.room.refresh();
        c.shape.refresh();
        c.sounds.refresh (corpus);
        const int lay = e.getLayoutX() * 10 + e.getLayoutY();
        if (lay != c.shownLayout)
        {
            c.shownLayout = lay;
            c.layoutButton.setButtonText (LayoutChooser::describe (e.getLayoutX(), e.getLayoutY()));
            c.layoutButton.setTitle (c.layoutButton.getButtonText() + ". Change the map layout");
            c.resized();
        }
        c.modes.setMidiOn (e.midiMode.load());
        c.map.cursor.valueText = "X " + juce::String (e.curX.load(), 2) + ", Y " + juce::String (1.0f - e.curY.load(), 2)
                                 + ". You're hearing " + c.mix.text;
    }

    // Errors show up as a banner (once per message).
    const auto st = e.getStatus();
    if (st != lastBannerStatus)
    {
        lastBannerStatus = st;
        auto msg = st.contains ("  |  ") ? st.fromFirstOccurrenceOf ("  |  ", false, false) : st;
        if (looksLikeError (msg))
            c.banner.show (msg);
    }
}

//==============================================================================
void SedimentsEditor::chooseModel()
{
    chooser = std::make_unique<juce::FileChooser> ("Choose a RAVE model exported for nn~", juce::File(), "*.ts");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc)
                          {
                              auto f = fc.getResult();
                              if (f.existsAsFile())
                                  proc.engine.loadModelAsync (f);
                          });
}

void SedimentsEditor::chooseFiles()
{
    chooser = std::make_unique<juce::FileChooser> ("Add audio files", juce::File(), "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                              | juce::FileBrowserComponent::canSelectMultipleItems,
                          [this] (const juce::FileChooser& fc)
                          {
                              juce::StringArray paths;
                              for (auto& f : fc.getResults())
                                  paths.add (f.getFullPathName());
                              if (! paths.isEmpty())
                                  proc.engine.addFilesAsync (paths);
                          });
}

bool SedimentsEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (isAudioFile (f) || juce::File (f).hasFileExtension ("ts") || juce::File (f).isDirectory())
            return true;
    return false;
}

void SedimentsEditor::fileDragEnter (const juce::StringArray&, int, int)
{
    content->map.dragHighlight = true;
    content->welcome.dragHighlight = true;
    content->repaint();
}

void SedimentsEditor::fileDragExit (const juce::StringArray&)
{
    content->map.dragHighlight = false;
    content->welcome.dragHighlight = false;
    content->repaint();
}

void SedimentsEditor::filesDropped (const juce::StringArray& files, int, int)
{
    fileDragExit (files);
    juce::StringArray audio;
    for (auto& path : files)
    {
        juce::File f (path);
        if (f.hasFileExtension ("ts"))
            proc.engine.loadModelAsync (f);
        else if (f.isDirectory())
            for (auto& child : f.findChildFiles (juce::File::findFiles, true, "*.wav;*.aif;*.aiff;*.flac;*.mp3;*.ogg"))
                audio.add (child.getFullPathName());
        else if (isAudioFile (path))
            audio.add (path);
    }
    if (! audio.isEmpty())
        proc.engine.addFilesAsync (audio);
}
