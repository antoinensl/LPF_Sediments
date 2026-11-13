#include "MapView.h"
#include "RoomView.h"

namespace lt
{
static const char* noteNames[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

static juce::String formatTime (float seconds)
{
    const int s = juce::jmax (0, (int) seconds);
    return juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
}

//==============================================================================
CursorHandle::CursorHandle (SedimentsProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    setTitle ("Map cursor");
    setDescription ("Arrow keys move it. Shift for fine steps. Space plays the nearest original sound.");
    setInterceptsMouseClicks (false, false);
}

void CursorHandle::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto c = getLocalBounds().toFloat().getCentre();
    g.setColour (s.theme().map);
    g.fillEllipse (c.x - 15.0f, c.y - 15.0f, 30.0f, 30.0f);
    g.setColour (s.accent());
    g.fillEllipse (c.x - 12.0f, c.y - 12.0f, 24.0f, 24.0f);
    if (hasKeyboardFocus (false))
    {
        g.setColour (s.theme().focus);
        g.drawEllipse (c.x - 18.0f, c.y - 18.0f, 36.0f, 36.0f, 3.0f);
    }
}

void CursorHandle::moveBy (float dx, float dy)
{
    auto* px = proc.params.getParameter ("x");
    auto* py = proc.params.getParameter ("y");
    if (px == nullptr || py == nullptr)
        return;
    const float x = juce::jlimit (0.0f, 1.0f, px->getValue() + dx);
    const float y = juce::jlimit (0.0f, 1.0f, py->getValue() - dy);   // Y parameter: 1 = top
    px->beginChangeGesture(); px->setValueNotifyingHost (x); px->endChangeGesture();
    py->beginChangeGesture(); py->setValueNotifyingHost (y); py->endChangeGesture();
    proc.engine.posX = x;
    proc.engine.posY = 1.0f - y;
}

bool CursorHandle::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 0.005f : 0.02f;
    if (k.isKeyCode (juce::KeyPress::leftKey))  { moveBy (-step, 0.0f); return true; }
    if (k.isKeyCode (juce::KeyPress::rightKey)) { moveBy (step, 0.0f);  return true; }
    if (k.isKeyCode (juce::KeyPress::upKey))    { moveBy (0.0f, -step); return true; }
    if (k.isKeyCode (juce::KeyPress::downKey))  { moveBy (0.0f, step);  return true; }
    return false;
}

bool CursorHandle::keyStateChanged (bool)
{
    const bool down = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::returnKey);
    if (down != proc.keyHeld.load())
        proc.keyHeld = down;
    return false;
}

void CursorHandle::focusLost (FocusChangeType)
{
    proc.keyHeld = false;
    repaint();
}

std::unique_ptr<juce::AccessibilityHandler> CursorHandle::createAccessibilityHandler()
{
    struct Value : public juce::AccessibilityTextValueInterface
    {
        explicit Value (CursorHandle& c) : owner (c) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return owner.valueText; }
        void setValueAsString (const juce::String&) override {}
        CursorHandle& owner;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
MapView::MapView (SedimentsProcessor& p) : cursor (p), proc (p)
{
    setTitle ("Terrain map");
    setDescription ("Each mark is a moment of one of your sounds. Similar moments sit close together.");
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    addAndMakeVisible (cursor);
}

bool MapView::showZones() const
{
    return corpus != nullptr && corpus->numZones() > 0 && (mode == Mode::keys || proc.engine.midiMode.load());
}

void MapView::refresh()
{
    const int v = proc.engine.getCorpusVersion();
    if (v != shownCorpus)
    {
        shownCorpus = v;
        corpus = proc.engine.getCorpus();
        hover = -1; hoverSource = -1; listHighlight = -1;
        dotsDirty = true;
        rebuildContours();
        rebuildRegions();
    }
    shownPath = proc.engine.getPath();

    juce::Point<float> target (proc.engine.curX.load(), proc.engine.curY.load());
    if (ui().calm || shownCursor.getDistanceFrom (target) > 0.25f)
        shownCursor = target;
    else
        shownCursor += (target - shownCursor) * 0.45f;
    positionCursor();
    repaint();
}

void MapView::resized()
{
    dotsDirty = true;
    positionCursor();
}

void MapView::positionCursor()
{
    const int x = juce::roundToInt (shownCursor.x * (float) getWidth());
    const int y = juce::roundToInt (shownCursor.y * (float) getHeight());
    cursor.setBounds (x - 22, y - 22, 44, 44);
}

//==============================================================================
void MapView::rebuildDots()
{
    dotsDirty = false;
    const int w = juce::jmax (1, getWidth()), h = juce::jmax (1, getHeight());
    dots = juce::Image (juce::Image::ARGB, w, h, true);
    if (corpus == nullptr || corpus->size() == 0)
        return;

    juce::Graphics g (dots);
    const int n = corpus->size();
    const float size = n > 6000 ? 5.0f : (n > 1500 ? 6.5f : 8.0f);
    dotSize = size;
    const float alpha = juce::jlimit (0.35f, 0.92f, 90.0f / std::sqrt ((float) n));

    std::vector<juce::Colour> colours;
    std::vector<int> shapes;
    for (auto& src : corpus->sources)
    {
        colours.push_back (colourOf ? colourOf (src) : ui().soundColour (src.colour));
        shapes.push_back (shapeOf ? shapeOf (src) : shapeFor (src.colour, ui().shapes));
    }
    for (int i = 0; i < n; ++i)
    {
        const int si = corpus->sourceIndex[(size_t) i];
        drawGlyph (g, shapes[(size_t) si], { corpus->xy[(size_t) i * 2] * w, corpus->xy[(size_t) i * 2 + 1] * h }, size,
                   colours[(size_t) si].withAlpha (alpha));
    }
}

void MapView::rebuildContours()
{
    contours.clear();
    if (corpus == nullptr || corpus->size() < 8)
        return;

    // Density of moments on a grid, blurred, then iso-lines (marching squares).
    constexpr int GW = 120, GH = 90;
    std::vector<float> grid ((size_t) GW * GH, 0.0f), tmp ((size_t) GW * GH, 0.0f);
    for (int i = 0; i < corpus->size(); ++i)
    {
        const float gx = corpus->xy[(size_t) i * 2] * (GW - 1), gy = corpus->xy[(size_t) i * 2 + 1] * (GH - 1);
        const int x0 = juce::jlimit (0, GW - 2, (int) gx), y0 = juce::jlimit (0, GH - 2, (int) gy);
        const float fx = gx - x0, fy = gy - y0;
        grid[(size_t) (y0 * GW + x0)]           += (1 - fx) * (1 - fy);
        grid[(size_t) (y0 * GW + x0 + 1)]       += fx * (1 - fy);
        grid[(size_t) ((y0 + 1) * GW + x0)]     += (1 - fx) * fy;
        grid[(size_t) ((y0 + 1) * GW + x0 + 1)] += fx * fy;
    }
    const float sigma = 3.2f;
    const int R = 9;
    std::vector<float> k ((size_t) (2 * R + 1));
    for (int i = -R; i <= R; ++i) k[(size_t) (i + R)] = std::exp (-(float) (i * i) / (2.0f * sigma * sigma));
    for (int y = 0; y < GH; ++y)
        for (int x = 0; x < GW; ++x)
        {
            float acc = 0.0f;
            for (int i = -R; i <= R; ++i) acc += k[(size_t) (i + R)] * grid[(size_t) (y * GW + juce::jlimit (0, GW - 1, x + i))];
            tmp[(size_t) (y * GW + x)] = acc;
        }
    float maxV = 0.0f;
    for (int y = 0; y < GH; ++y)
        for (int x = 0; x < GW; ++x)
        {
            float acc = 0.0f;
            for (int i = -R; i <= R; ++i) acc += k[(size_t) (i + R)] * tmp[(size_t) (juce::jlimit (0, GH - 1, y + i) * GW + x)];
            grid[(size_t) (y * GW + x)] = acc;
            maxV = juce::jmax (maxV, acc);
        }
    if (maxV <= 0.0f)
        return;

    auto val = [&] (int x, int y) { return grid[(size_t) (y * GW + x)] / maxV; };
    auto px = [] (float gx) { return gx / (GW - 1); };
    auto py = [] (float gy) { return gy / (GH - 1); };
    for (float level : { 0.07f, 0.18f, 0.34f, 0.55f, 0.78f })
    {
        for (int y = 0; y < GH - 1; ++y)
            for (int x = 0; x < GW - 1; ++x)
            {
                const float a = val (x, y), b = val (x + 1, y), c = val (x + 1, y + 1), d = val (x, y + 1);
                const int idx = (a > level ? 1 : 0) | (b > level ? 2 : 0) | (c > level ? 4 : 0) | (d > level ? 8 : 0);
                if (idx == 0 || idx == 15)
                    continue;
                auto lerp = [level] (float v0, float v1) { return v1 == v0 ? 0.5f : (level - v0) / (v1 - v0); };
                const juce::Point<float> top    (px (x + lerp (a, b)), py ((float) y));
                const juce::Point<float> right  (px ((float) x + 1), py (y + lerp (b, c)));
                const juce::Point<float> bottom (px (x + lerp (d, c)), py ((float) y + 1));
                const juce::Point<float> left   (px ((float) x), py (y + lerp (a, d)));
                auto seg = [this] (juce::Point<float> p, juce::Point<float> q) { contours.startNewSubPath (p); contours.lineTo (q); };
                switch (idx)
                {
                    case 1: case 14: seg (left, top); break;
                    case 2: case 13: seg (top, right); break;
                    case 3: case 12: seg (left, right); break;
                    case 4: case 11: seg (right, bottom); break;
                    case 5: seg (left, top); seg (right, bottom); break;
                    case 6: case 9: seg (top, bottom); break;
                    case 7: case 8: seg (left, bottom); break;
                    case 10: seg (top, right); seg (left, bottom); break;
                    default: break;
                }
            }
    }
}

void MapView::rebuildRegions()
{
    regions.clear();
    if (corpus == nullptr || corpus->sources.empty() || corpus->sources.size() > 10)
        return;
    const size_t ns = corpus->sources.size();
    std::vector<double> sx (ns, 0.0), sy (ns, 0.0);
    std::vector<int> cnt (ns, 0);
    for (int i = 0; i < corpus->size(); ++i)
    {
        const auto si = (size_t) corpus->sourceIndex[(size_t) i];
        sx[si] += corpus->xy[(size_t) i * 2];
        sy[si] += corpus->xy[(size_t) i * 2 + 1];
        ++cnt[si];
    }
    for (size_t i = 0; i < ns; ++i)
        if (cnt[i] > 0)
        {
            auto name = juce::File::createFileWithoutCheckingPath (corpus->sources[i].path).getFileNameWithoutExtension();
            if (name.isEmpty()) name = corpus->sources[i].name;
            regions.push_back ({ name, (float) (sx[i] / cnt[i]), (float) (sy[i] / cnt[i]), (int) i });
        }
    // push labels apart a little so they don't sit on top of each other
    for (int it = 0; it < 20; ++it)
        for (size_t a = 0; a < regions.size(); ++a)
            for (size_t b = a + 1; b < regions.size(); ++b)
            {
                const float dx = regions[b].x - regions[a].x, dy = regions[b].y - regions[a].y;
                if (std::abs (dx) < 0.16f && std::abs (dy) < 0.05f)
                {
                    const float push = dy >= 0 ? 0.01f : -0.01f;
                    regions[a].y -= push; regions[b].y += push;
                }
            }
}

//==============================================================================
void MapView::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    auto bounds = getLocalBounds().toFloat();
    const float W = bounds.getWidth(), H = bounds.getHeight();

    juce::Path clip;
    clip.addRoundedRectangle (bounds, 16.0f);
    {
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (clip);
        g.setColour (t.map);
        g.fillRect (bounds);

        // where sounds gather (contour lines)
        g.setColour (t.contour);
        g.strokePath (contours, juce::PathStrokeType (1.3f), juce::AffineTransform::scale (W, H));

        if (dotsDirty || dots.getWidth() != getWidth() || dots.getHeight() != getHeight())
            rebuildDots();
        g.setOpacity (1.0f);
        g.drawImageAt (dots, 0, 0);

        // A sound's name only shows when it is pointed at (on the map or in the list):
        // its moments are brought forward and the others recede.
        const int hl = hoverSource >= 0 ? hoverSource : listHighlight;
        if (hl >= 0 && corpus != nullptr && hl < (int) corpus->sources.size() && mode != Mode::space && ! recording)
        {
            const auto& src = corpus->sources[(size_t) hl];
            const auto col = colourOf ? colourOf (src) : s.accent();
            const int shape = shapeOf ? shapeOf (src) : 0;
            g.setColour (t.map.withAlpha (0.62f));
            g.fillRect (bounds);
            for (int i = 0; i < corpus->size(); ++i)
                if (corpus->sourceIndex[(size_t) i] == hl)
                    drawGlyph (g, shape, { corpus->xy[(size_t) i * 2] * W, corpus->xy[(size_t) i * 2 + 1] * H }, dotSize + 2.5f, col);
            for (auto& r : regions)
            {
                if (r.source != hl) continue;
                g.setFont (s.font (12.5f, true));
                const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), r.name);
                auto pill = juce::Rectangle<float> (0, 0, tw + 34.0f, 26.0f).withCentre ({ juce::jlimit (tw * 0.5f + 24.0f, W - tw * 0.5f - 24.0f, r.x * W), juce::jlimit (16.0f, H - 16.0f, r.y * H + 26.0f) });
                g.setColour (t.panel.withAlpha (0.95f));
                g.fillRoundedRectangle (pill, 13.0f);
                g.setColour (col);
                g.drawRoundedRectangle (pill.reduced (0.75f), 13.0f, 1.5f);
                drawGlyph (g, shape, { pill.getX() + 14.0f, pill.getCentreY() }, 9.0f, col);
                g.setColour (t.fg);
                g.drawText (r.name, pill.withTrimmedLeft (24.0f).withTrimmedRight (8.0f), juce::Justification::centredLeft, true);
            }
        }

        // MIDI zones, one per note name, clockwise from C at the top
        if (showZones())
        {
            const int active = proc.engine.activeZone.load();
            for (int z = 0; z < corpus->numZones(); ++z)
            {
                const float zx = corpus->zones[(size_t) z * 2] * W, zy = corpus->zones[(size_t) z * 2 + 1] * H;
                const float r = juce::jmax (24.0f, corpus->zoneRadius[(size_t) z] * juce::jmin (W, H));
                const bool on = z == active;
                g.setColour (on ? s.accent().withAlpha (0.18f) : t.panel.withAlpha (0.35f));
                g.fillEllipse (zx - r, zy - r, 2 * r, 2 * r);
                g.setColour (on ? s.accent() : t.muted);
                g.drawEllipse (zx - r, zy - r, 2 * r, 2 * r, on ? 3.0f : 1.5f);
                juce::String label (noteNames[z]);
                for (int k = z + corpus->numZones(); k < 12; k += corpus->numZones())
                    label << "/" << noteNames[k];
                g.setColour (t.fg);
                g.setFont (s.font (15.0f, true));
                g.drawText (label, juce::Rectangle<float> (zx - 40.0f, zy - 11.0f, 80.0f, 22.0f), juce::Justification::centred);
            }
        }

        if (mode == Mode::space)
            paintSpace (g);

        // the recorded gesture
        auto drawLine = [&] (const std::vector<LatentEngine::PathPoint>& pts, float dx, float dy, juce::Colour c, float th)
        {
            if (pts.size() < 2) return;
            juce::Path line;
            line.startNewSubPath ((pts[0].x + dx) * W, (pts[0].y + dy) * H);
            for (size_t i = 1; i < pts.size(); ++i)
                line.lineTo ((pts[i].x + dx) * W, (pts[i].y + dy) * H);
            g.setColour (c);
            g.strokePath (line, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        };
        if (recording)
            drawLine (recorded, 0.0f, 0.0f, t.rec, 3.0f);
        else if (shownPath != nullptr && (mode == Mode::gesture || proc.engine.pathOn.load()))
        {
            const bool follows = proc.engine.pathFollows.load();
            const float dx = follows ? proc.engine.baseX.load() - shownPath->front().x : 0.0f;
            const float dy = follows ? proc.engine.baseY.load() - shownPath->front().y : 0.0f;
            const bool playing = proc.engine.pathOn.load();
            drawLine (*shownPath, dx, dy, s.accent().withAlpha (playing ? 0.9f : 0.4f), 3.0f);
            const juce::Point<float> start ((shownPath->front().x + dx) * W, (shownPath->front().y + dy) * H);
            g.setColour (t.panel);
            g.fillEllipse (start.x - 7.0f, start.y - 7.0f, 14.0f, 14.0f);
            g.setColour (s.accent());
            g.drawEllipse (start.x - 7.0f, start.y - 7.0f, 14.0f, 14.0f, 3.0f);
            g.setFont (s.font (12.0f, true));
            auto tag = juce::Rectangle<float> (start.x - 56.0f, start.y + 10.0f, 46.0f, 22.0f);
            g.setColour (t.panel);
            g.fillRoundedRectangle (tag, 8.0f);
            g.setColour (t.line);
            g.drawRoundedRectangle (tag.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (t.fg);
            g.drawText ("Start", tag, juce::Justification::centred);
        }

        // focus circle: how wide the blend of neighbours reaches
        if (corpus != nullptr && corpus->size() > 0 && proc.engine.terrainMix.load() < 0.99f)
        {
            const float rr = Terrain::blurToSigma (proc.engine.blur.load()) * W * 1.4f;
            const float cx = shownCursor.x * W, cy = shownCursor.y * H;
            g.setColour (s.accent().withAlpha (0.12f));
            g.fillEllipse (cx - rr, cy - rr, rr * 2, rr * 2);
            juce::Path ring;
            ring.addEllipse (cx - rr, cy - rr, rr * 2, rr * 2);
            juce::Path dashed;
            const float dash[] = { 6.0f, 5.0f };
            juce::PathStrokeType (1.5f).createDashedStroke (dashed, ring, dash, 2);
            g.setColour (s.accent().withAlpha (0.8f));
            g.fillPath (dashed);
        }

        // what is under the mouse
        if (hover >= 0 && corpus != nullptr && hover < corpus->size() && ! recording)
        {
            const auto& src = corpus->sources[(size_t) corpus->sourceIndex[(size_t) hover]];
            const juce::String title = juce::String (src.name) + juce::String::fromUTF8 ("  \xc2\xb7  ") + formatTime (corpus->frameTime.empty() ? 0.0f : corpus->frameTime[(size_t) hover]);
            const juce::String sub = "Shift-click to hear this moment";
            const float tw = juce::jmax (juce::GlyphArrangement::getStringWidth (s.font (13.0f, true), title) + 26.0f,
                                         juce::GlyphArrangement::getStringWidth (s.font (12.5f), sub)) + 24.0f;
            auto card = juce::Rectangle<float> (hoverMouse.x + 14.0f, hoverMouse.y - 62.0f, tw, 52.0f);
            if (card.getRight() > W - 8.0f) card.setX (hoverMouse.x - tw - 14.0f);
            if (card.getY() < 8.0f) card.setY (hoverMouse.y + 16.0f);
            g.setColour (juce::Colours::black.withAlpha (0.25f));
            g.fillRoundedRectangle (card.translated (0.0f, 4.0f), 12.0f);
            g.setColour (t.panel);
            g.fillRoundedRectangle (card, 12.0f);
            g.setColour (t.line);
            g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);
            auto inner = card.reduced (12.0f, 8.0f);
            auto row = inner.removeFromTop (19.0f);
            drawGlyph (g, shapeOf ? shapeOf (src) : 0, { row.getX() + 6.0f, row.getCentreY() }, 10.0f, colourOf ? colourOf (src) : s.accent());
            g.setColour (t.fg);
            g.setFont (s.font (13.0f, true));
            g.drawText (title, row.withTrimmedLeft (20.0f), juce::Justification::centredLeft, true);
            g.setColour (t.muted);
            g.setFont (s.font (12.5f));
            g.drawText (sub, inner, juce::Justification::centredLeft, true);
        }

        // axis names when the map is sorted by descriptors
        if (corpus != nullptr && (corpus->layoutX > 0 || corpus->layoutY > 0))
        {
            static const char* names[] = { "Similarity", "Pitch", "Brightness", "Noisiness", "Loudness" };
            g.setFont (s.font (13.0f, true));
            auto pill = [&] (const juce::String& text, juce::Point<float> c)
            {
                const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 20.0f;
                auto r = juce::Rectangle<float> (tw, 26.0f).withCentre (c);
                g.setColour (t.panel.withAlpha (0.92f));
                g.fillRoundedRectangle (r, 13.0f);
                g.setColour (t.line);
                g.drawRoundedRectangle (r.reduced (0.5f), 13.0f, 1.0f);
                g.setColour (t.fg);
                g.drawText (text, r, juce::Justification::centred);
            };
            if (corpus->layoutX > 0)
                pill (juce::String (names[corpus->layoutX]) + juce::String::fromUTF8 (" \xe2\x86\x92"), { W - 80.0f, H - 22.0f });
            if (corpus->layoutY > 0)
                pill (juce::String::fromUTF8 ("\xe2\x86\x91 ") + names[corpus->layoutY], { 68.0f, H * 0.5f });
        }

        // caption (not over the note zones, not needed when axes are named)
        if (! showZones() && corpus != nullptr && corpus->layoutX == 0 && corpus->layoutY == 0)
        {
            const juce::String cap = juce::String::fromUTF8 ("Lines show where sounds gather  \xc2\xb7  close together = similar");
            g.setFont (s.font (12.5f));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), cap);
            auto pill = juce::Rectangle<float> (12.0f, H - 40.0f, tw + 46.0f, 28.0f);
            g.setColour (t.panel.withAlpha (0.92f));
            g.fillRoundedRectangle (pill, 14.0f);
            g.setColour (t.line);
            g.drawRoundedRectangle (pill.reduced (0.5f), 14.0f, 1.0f);
            Icons::draw (g, "contour", { pill.getX() + 10.0f, pill.getCentreY() - 8.0f, 16.0f, 16.0f }, t.muted, 1.8f);
            g.setColour (t.muted);
            g.drawText (cap, pill.withTrimmedLeft (34.0f), juce::Justification::centredLeft);
        }

        if (recordArmed && ! recording)
        {
            const juce::String hint = juce::String::fromUTF8 ("Draw your gesture on the map  \xc2\xb7  release to finish");
            g.setFont (s.font (15.0f, true));
            const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), hint) + 36.0f;
            auto pill = juce::Rectangle<float> (W * 0.5f - tw * 0.5f, 16.0f, tw, 40.0f);
            g.setColour (t.panel);
            g.fillRoundedRectangle (pill, 20.0f);
            g.setColour (t.rec);
            g.drawRoundedRectangle (pill.reduced (1.0f), 20.0f, 2.0f);
            g.setColour (t.fg);
            g.drawText (hint, pill, juce::Justification::centred);
        }
    }

    // frame
    const bool redFrame = recordArmed || recording;
    g.setColour (redFrame ? t.rec : (dragHighlight ? s.accent() : t.line));
    g.drawRoundedRectangle (bounds.reduced (redFrame || dragHighlight ? 1.5f : 0.5f), 16.0f, redFrame || dragHighlight ? 3.0f : 1.0f);
}

//==============================================================================
void MapView::setFromMouse (juce::Point<float> p)
{
    const float x = juce::jlimit (0.0f, 1.0f, p.x / (float) juce::jmax (1, getWidth()));
    const float y = juce::jlimit (0.0f, 1.0f, p.y / (float) juce::jmax (1, getHeight()));
    if (auto* px = proc.params.getParameter ("x")) px->setValueNotifyingHost (x);
    if (auto* py = proc.params.getParameter ("y")) py->setValueNotifyingHost (1.0f - y);
    proc.engine.posX = x;
    proc.engine.posY = y;
}

int MapView::nearestPoint (juce::Point<float> p, float maxPixels) const
{
    if (corpus == nullptr)
        return -1;
    const float W = (float) getWidth(), H = (float) getHeight();
    float best = maxPixels * maxPixels;
    int idx = -1;
    for (int i = 0; i < corpus->size(); ++i)
    {
        const float dx = corpus->xy[(size_t) i * 2] * W - p.x, dy = corpus->xy[(size_t) i * 2 + 1] * H - p.y;
        const float d = dx * dx + dy * dy;
        if (d < best) { best = d; idx = i; }
    }
    return idx;
}

int MapView::zoneAt (juce::Point<float> p) const
{
    if (! showZones())
        return -1;
    const float W = (float) getWidth(), H = (float) getHeight();
    for (int z = 0; z < corpus->numZones(); ++z)
    {
        const float r = juce::jmax (24.0f, corpus->zoneRadius[(size_t) z] * juce::jmin (W, H));
        if (p.getDistanceFrom ({ corpus->zones[(size_t) z * 2] * W, corpus->zones[(size_t) z * 2 + 1] * H }) <= r)
            return z;
    }
    return -1;
}

void MapView::hearNearestToCursor()
{
    if (corpus == nullptr || corpus->frameTime.empty())
        return;
    const int i = nearestPoint ({ proc.engine.curX.load() * getWidth(), proc.engine.curY.load() * getHeight() }, 1.0e6f);
    if (i < 0)
        return;
    const auto& src = corpus->sources[(size_t) corpus->sourceIndex[(size_t) i]];
    proc.engine.auditionAsync (juce::String (src.path), juce::jmax (0.0f, corpus->frameTime[(size_t) i] - 0.25f), 1.6);
}

void MapView::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isShiftDown() && ! recordArmed)
    {
        const int i = nearestPoint (e.position, 24.0f);
        if (i >= 0 && ! corpus->frameTime.empty())
        {
            const auto& src = corpus->sources[(size_t) corpus->sourceIndex[(size_t) i]];
            proc.engine.auditionAsync (juce::String (src.path), juce::jmax (0.0f, corpus->frameTime[(size_t) i] - 0.25f), 1.6);
        }
        return;
    }

    if (! recordArmed)
    {
        const int z = zoneAt (e.position);
        if (z >= 0 && mode == Mode::keys)
        {
            if (! proc.engine.midiMode.load())
                if (auto* p = proc.params.getParameter ("midiMode")) p->setValueNotifyingHost (1.0f);
            proc.engine.midiMode = true;
            heldNote = 60 + z;
            proc.engine.pushNoteFromUI (heldNote, 0.8f, true);
            return;
        }
    }

    cursor.grabKeyboardFocus();
    proc.mapHeld = true;
    if (recordArmed)
    {
        recording = true;
        recordArmed = false;
        recorded.clear();
        recordStart = juce::Time::getMillisecondCounterHiRes();
        recorded.push_back ({ 0.0f, juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth()), juce::jlimit (0.0f, 1.0f, e.position.y / (float) getHeight()) });
    }
    if (auto* px = proc.params.getParameter ("x")) px->beginChangeGesture();
    if (auto* py = proc.params.getParameter ("y")) py->beginChangeGesture();
    setFromMouse (e.position);
}

void MapView::mouseDrag (const juce::MouseEvent& e)
{
    if (heldNote >= 0 || e.mods.isShiftDown())
        return;
    setFromMouse (e.position);
    if (recording)
    {
        const float t = (float) ((juce::Time::getMillisecondCounterHiRes() - recordStart) * 0.001);
        if (t > recorded.back().t + 0.004f)
            recorded.push_back ({ t, juce::jlimit (0.0f, 1.0f, e.position.x / (float) getWidth()), juce::jlimit (0.0f, 1.0f, e.position.y / (float) getHeight()) });
        repaint();
    }
}

void MapView::mouseUp (const juce::MouseEvent&)
{
    proc.mapHeld = false;
    if (heldNote >= 0)
    {
        proc.engine.pushNoteFromUI (heldNote, 0.0f, false);
        heldNote = -1;
        return;
    }
    if (recording)
    {
        recording = false;
        const float t = (float) ((juce::Time::getMillisecondCounterHiRes() - recordStart) * 0.001);
        recorded.push_back ({ t, recorded.back().x, recorded.back().y });
        if (recorded.size() >= 3 && t > 0.15f)
        {
            proc.engine.setPath (recorded);
            if (auto* p = proc.params.getParameter ("pathOn")) p->setValueNotifyingHost (1.0f);
        }
        recorded.clear();
        if (onRecordingFinished) onRecordingFinished();
    }
    if (auto* px = proc.params.getParameter ("x")) px->endChangeGesture();
    if (auto* py = proc.params.getParameter ("y")) py->endChangeGesture();
}

void MapView::mouseMove (const juce::MouseEvent& e)
{
    hoverMouse = e.position;
    const int h = nearestPoint (e.position, 10.0f);
    const int near = nearestPoint (e.position, 22.0f);
    const int hs = near >= 0 && corpus != nullptr ? corpus->sourceIndex[(size_t) near] : -1;
    if (h != hover || hs != hoverSource) { hover = h; hoverSource = hs; repaint(); }
    else if (hover >= 0) repaint();
}

void MapView::mouseExit (const juce::MouseEvent&)
{
    if (hover >= 0 || hoverSource >= 0) { hover = -1; hoverSource = -1; repaint(); }
}

void MapView::highlightSound (const juce::String& path)
{
    int found = -1;
    if (corpus != nullptr && path.isNotEmpty())
        for (size_t i = 0; i < corpus->sources.size(); ++i)
            if (juce::String (corpus->sources[i].path) == path) { found = (int) i; break; }
    if (found != listHighlight) { listHighlight = found; repaint(); }
}

//==============================================================================
// Space tab
static constexpr float degToRad = 3.14159265358979f / 180.0f;

juce::String describeDirection (float deg)
{
    static const char* names[] = { "Front", "Front right", "Right", "Back right", "Back", "Back left", "Left", "Front left" };
    float d = std::fmod (deg + 360.0f, 360.0f);
    const int sector = (int) std::floor ((d + 22.5f) / 45.0f) % 8;
    int shown = juce::roundToInt (deg);
    if (shown > 180) shown -= 360;
    if (shown <= -180) shown += 360;
    return juce::String (names[sector]) + ", " + juce::String (shown) + juce::String::fromUTF8 ("\xc2\xb0");
}

juce::Point<float> MapView::roomPoint (float az, float r) const
{
    const float W = (float) getWidth(), H = (float) getHeight();
    return { W * (0.5f + 0.42f * r * std::sin (az * degToRad)), H * (0.5f - 0.42f * r * std::cos (az * degToRad)) };
}

float MapView::roomAngleAt (juce::Point<float> p) const
{
    const float dx = p.x / (float) juce::jmax (1, getWidth()) - 0.5f, dy = 0.5f - p.y / (float) juce::jmax (1, getHeight());
    return std::atan2 (dx, dy) / degToRad;
}

void MapView::paintSpace (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    const float W = (float) getWidth(), H = (float) getHeight();

    // push the marks back so the room reads clearly
    g.setColour (t.map.withAlpha (0.6f));
    g.fillRect (getLocalBounds());

    const auto fmt = Spatial::formatFor ((int) proc.params.getRawParameterValue ("output")->load(),
                                         proc.params.getRawParameterValue ("ringStart")->load() > 0.5f,
                                         proc.params.getRawParameterValue ("useCentre")->load() > 0.5f,
                                         proc.params.getRawParameterValue ("lfe")->load() > 0.5f);
    const juce::Point<float> c (W * 0.5f, H * 0.5f);
    const float rx = W * 0.42f, ry = H * 0.42f;
    g.setColour (t.line);
    g.drawEllipse (c.x - rx * 0.5f, c.y - ry * 0.5f, rx, ry, 1.0f);
    g.setColour (t.muted.withAlpha (0.8f));
    g.drawEllipse (c.x - rx, c.y - ry, rx * 2.0f, ry * 2.0f, 1.5f);
    g.drawLine (c.x - 6.0f, c.y, c.x + 6.0f, c.y, 1.0f);
    g.drawLine (c.x, c.y - 6.0f, c.x, c.y + 6.0f, 1.0f);

    g.setFont (s.font (12.5f, true));
    auto label = [&] (const juce::String& text, juce::Point<float> at)
    {
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
        auto r = juce::Rectangle<float> (tw, 22.0f).withCentre (at);
        r.setX (juce::jlimit (4.0f, W - tw - 4.0f, r.getX()));
        r.setY (juce::jlimit (4.0f, H - 26.0f, r.getY()));
        g.setColour (t.panel.withAlpha (0.92f));
        g.fillRoundedRectangle (r, 11.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.0f);
        g.setColour (t.fg);
        g.drawText (text, r, juce::Justification::centred);
    };

    // where the last grains were heard (fading), and the model voice
    const auto now = juce::Time::getMillisecondCounter();
    const bool ambi = fmt.kind == Spatial::Kind::ambisonics;
    for (auto& m : proc.engine.grainMarks)
    {
        const int src = m.source.load();
        const auto age = (float) (now - m.when.load());
        if (src < 0 || age > grainMarkLifeMs || corpus == nullptr || src >= (int) corpus->sources.size())
            continue;
        const float el = ambi ? m.el.load() : 0.0f;
        const auto p = roomPoint (m.az.load(), m.dist.load() * std::cos (el * degToRad));
        const auto& source = corpus->sources[(size_t) src];
        drawGrainMark (g, shapeOf ? shapeOf (source) : 0, p, age, colourOf ? colourOf (source) : s.accent());
    }
    if (proc.engine.modelPlaceActive.load())
    {
        const float el = ambi ? proc.engine.modelEl.load() : 0.0f;
        const auto p = roomPoint (proc.engine.modelAz.load(), proc.engine.modelDist.load() * std::cos (el * degToRad));
        g.setColour (s.accent());
        g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 3.0f);
    }

    // loudspeakers
    auto speaker = [&] (const juce::String& text, float az, float radius = 1.0f, bool dim = false)
    {
        const auto p = roomPoint (az, radius);
        g.setFont (s.font (12.5f, true));
        const float w = juce::jmax (26.0f, juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 12.0f);
        auto r = juce::Rectangle<float> (w, 26.0f).withCentre (p);
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (dim ? t.muted.withAlpha (0.6f) : t.fg);
        g.drawRoundedRectangle (r.reduced (0.75f), 7.0f, dim ? 1.0f : 1.5f);
        g.drawText (text, r, juce::Justification::centred);
    };
    if (fmt.kind == Spatial::Kind::surround)
    {
        label ("Front", roomPoint (0.0f, 1.0f).translated (0.0f, -26.0f));
        const auto& L = *fmt.layout;
        for (int k = 0; k < (int) L.speakers.size(); ++k)
        {
            const auto& sp = L.speakers[(size_t) k];
            if (! sp.lfe)
                speaker (sp.name, sp.az, sp.el > 0.0f ? 0.62f : 1.0f, k == L.centreChannel && ! L.centreUsed);
        }
    }
    else if (fmt.kind == Spatial::Kind::ring)
    {
        label ("Front", roomPoint (0.0f, 1.0f).translated (0.0f, -26.0f));
        for (int k = 0; k < fmt.channels; ++k)
            speaker (juce::String (k + 1), fmt.speakerAngle (k));
    }
    else if (fmt.kind == Spatial::Kind::stereo)
    {
        label ("Front", roomPoint (0.0f, 1.0f).translated (0.0f, -26.0f));
        speaker ("L", -90.0f);
        speaker ("R", 90.0f);
    }
    else
    {
        label ("Front", roomPoint (0.0f, 1.0f).translated (0.0f, -20.0f));
        label ("Back", roomPoint (180.0f, 1.0f).translated (0.0f, 20.0f));
        label ("Left", roomPoint (-90.0f, 1.0f).translated (-10.0f, 0.0f));
        label ("Right", roomPoint (90.0f, 1.0f).translated (10.0f, 0.0f));
        if ((int) proc.params.getRawParameterValue ("height")->load() > 0)
            label ("Above", c.translated (0.0f, -22.0f));
    }

}

}
