#include "RoomView.h"

namespace lt
{
static constexpr float degToRad = 3.14159265358979f / 180.0f;

void drawGrainMark (juce::Graphics& g, int shape, juce::Point<float> at, float age, juce::Colour c, float scale)
{
    if (age < 0.0f || age > grainMarkLifeMs)
        return;
    float r, alpha;
    if (ui().calm)
    {
        r = 5.0f * scale;
        alpha = 1.0f - age / grainMarkLifeMs;
    }
    else if (age < 220.0f)
    {
        const float k = age / 220.0f;
        g.setColour (c.withAlpha (0.9f * (1.0f - k)));
        const float rr = (6.0f + 12.0f * k) * scale;
        g.drawEllipse (at.x - rr, at.y - rr, rr * 2.0f, rr * 2.0f, 2.0f);
        r = (3.0f + 4.0f * juce::jmin (1.0f, age / 80.0f)) * scale;
        alpha = 1.0f;
    }
    else
    {
        const float k = (age - 220.0f) / (grainMarkLifeMs - 220.0f);
        r = (7.0f - 4.0f * k) * scale;
        alpha = std::pow (juce::jmax (0.0f, 1.0f - k), 1.3f);
    }
    drawGlyph (g, shape, at, r * 2.0f, c.withMultipliedAlpha (alpha));
}

//==============================================================================
SoundDirHandle::SoundDirHandle (SedimentsProcessor& p, RoomView& r, int src, const juce::String& filePath, const juce::String& n)
    : source (src), path (filePath), name (n), proc (p), room (r)
{
    setWantsKeyboardFocus (true);
    setTitle (name + " direction");
    setDescription ("Drag it around the room, or use the arrow keys (Shift for fine steps).");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

float SoundDirHandle::direction() const
{
    auto it = proc.soundDirections.find (path);
    if (it != proc.soundDirections.end())
        return it->second;
    auto c = room.getCorpus();
    return Spatial::defaultSoundAz (source, c != nullptr ? (int) c->sources.size() : 1);
}

void SoundDirHandle::setDirection (float deg)
{
    deg = std::fmod (deg + 540.0f, 360.0f) - 180.0f;
    proc.setSoundDirection (path, deg);
    setTooltip (name + ": " + describeDirection (deg));
    if (auto* h = getAccessibilityHandler())
        h->notifyAccessibilityEvent (juce::AccessibilityEvent::valueChanged);
    room.refresh();
}

void SoundDirHandle::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto b = getLocalBounds().toFloat().reduced (4.0f);
    g.setColour (s.theme().panel);
    g.fillEllipse (b);
    g.setColour (colour);
    g.drawEllipse (b.reduced (1.5f), 3.0f);
    drawGlyph (g, shape, b.getCentre(), 10.0f, colour);
    if (hasKeyboardFocus (false))
    {
        g.setColour (s.theme().focus);
        g.drawEllipse (getLocalBounds().toFloat().reduced (1.0f), 2.5f);
    }
}

void SoundDirHandle::mouseDown (const juce::MouseEvent& e) { mouseDrag (e); }

void SoundDirHandle::mouseDrag (const juce::MouseEvent& e)
{
    float a = room.angleAt (e.getEventRelativeTo (&room).position);
    if (e.mods.isShiftDown())
        a = 15.0f * std::round (a / 15.0f);
    setDirection (a);
}

bool SoundDirHandle::keyPressed (const juce::KeyPress& k)
{
    const float step = k.getModifiers().isShiftDown() ? 1.0f : 15.0f;
    if (k.isKeyCode (juce::KeyPress::rightKey) || k.isKeyCode (juce::KeyPress::upKey))   { setDirection (direction() + step); return true; }
    if (k.isKeyCode (juce::KeyPress::leftKey)  || k.isKeyCode (juce::KeyPress::downKey)) { setDirection (direction() - step); return true; }
    if (k.isKeyCode (juce::KeyPress::homeKey)) { setDirection (0.0f); return true; }
    return false;
}

std::unique_ptr<juce::AccessibilityHandler> SoundDirHandle::createAccessibilityHandler()
{
    struct Value : public juce::AccessibilityTextValueInterface
    {
        explicit Value (SoundDirHandle& h) : owner (h) {}
        bool isReadOnly() const override { return true; }
        juce::String getCurrentValueAsString() const override { return describeDirection (owner.direction()); }
        void setValueAsString (const juce::String&) override {}
        SoundDirHandle& owner;
    };
    return std::make_unique<juce::AccessibilityHandler> (*this, juce::AccessibilityRole::slider, juce::AccessibilityActions{},
                                                         juce::AccessibilityHandler::Interfaces { std::make_unique<Value> (*this) });
}

//==============================================================================
RoomView::RoomView (SedimentsProcessor& p) : proc (p)
{
    setTitle ("Room");
    setOpaque (false);
}

void RoomView::resized()
{
    area = getLocalBounds().toFloat().withTrimmedTop (60.0f).withTrimmedBottom (40.0f).reduced (16.0f, 0.0f);
}

float RoomView::radius() const
{
    if (fmt.kind == Spatial::Kind::ambisonics)
        return juce::jmax (40.0f, juce::jmin (area.getWidth() * 0.5f - 70.0f, area.getHeight() * 0.5f - 30.0f));
    return juce::jmax (40.0f, juce::jmin (area.getWidth(), area.getHeight()) * 0.5f - 34.0f);
}

juce::Point<float> RoomView::topPoint (float az, float r) const
{
    const auto c = area.getCentre();
    const float R = radius();
    return { c.x + R * r * std::sin (az * degToRad), c.y - R * r * std::cos (az * degToRad) };
}

RoomView::P3 RoomView::project (float az, float el, float r) const
{
    const float a = az * degToRad, e = el * degToRad, yw = yaw * degToRad, t = tilt * degToRad;
    float x = r * std::cos (e) * std::sin (a), y = r * std::cos (e) * std::cos (a);
    const float z = r * std::sin (e);
    const float xr = x * std::cos (yw) - y * std::sin (yw), yr = x * std::sin (yw) + y * std::cos (yw);
    const auto c = area.getCentre();
    const float R = radius();
    return { c.x + R * xr, c.y - R * (z * std::cos (t) + yr * std::sin (t)), yr * std::cos (t) - z * std::sin (t) };
}

juce::Point<float> RoomView::handlePoint (float az) const
{
    if (fmt.kind == Spatial::Kind::ambisonics)
    {
        const auto p = project (az, 0.0f, 1.0f);
        return { p.x, p.y };
    }
    if (fmt.kind == Spatial::Kind::stereo)
    {
        const float half = juce::jmin (area.getWidth() * 0.5f - 40.0f, 420.0f);
        return { area.getCentreX() + half * std::sin (az * degToRad), area.getY() + 14.0f };
    }
    return topPoint (az, 1.0f);
}

float RoomView::angleAt (juce::Point<float> p) const
{
    const auto c = area.getCentre();
    if (fmt.kind == Spatial::Kind::stereo)
    {
        const float half = juce::jmin (area.getWidth() * 0.5f - 40.0f, 420.0f);
        return std::asin (juce::jlimit (-1.0f, 1.0f, (p.x - c.x) / half)) / degToRad;
    }
    if (fmt.kind == Spatial::Kind::ambisonics)
    {
        // back onto the floor (horizontal plane), then undo the view's turn
        const float R = radius(), t = juce::jmax (5.0f, tilt) * degToRad, yw = yaw * degToRad;
        const float xr = (p.x - c.x) / R, yr = -(p.y - c.y) / (R * std::sin (t));
        const float x = xr * std::cos (yw) + yr * std::sin (yw), y = -xr * std::sin (yw) + yr * std::cos (yw);
        return std::atan2 (x, y) / degToRad;
    }
    return std::atan2 (p.x - c.x, c.y - p.y) / degToRad;
}

void RoomView::rebuildHandles()
{
    for (auto* h : handles)
        removeChildComponent (h);
    handles.clear();
    if (corpus == nullptr)
        return;
    for (size_t i = 0; i < corpus->sources.size(); ++i)
    {
        const auto& src = corpus->sources[i];
        if (src.path.empty())
            continue;
        auto name = juce::File::createFileWithoutCheckingPath (src.path).getFileNameWithoutExtension();
        addChildComponent (handles.add (new SoundDirHandle (proc, *this, (int) i, src.path, name)));
    }
}

void RoomView::refresh()
{
    const int v = proc.engine.getCorpusVersion();
    if (v != shownCorpus)
    {
        shownCorpus = v;
        corpus = proc.engine.getCorpus();
        rebuildHandles();
    }
    fmt = Spatial::formatFor ((int) proc.params.getRawParameterValue ("output")->load(),
                              proc.params.getRawParameterValue ("ringStart")->load() > 0.5f);
    const bool each = (int) proc.params.getRawParameterValue ("place")->load() == (int) Spatial::PlaceBy::eachSound;
    for (auto* h : handles)
    {
        h->setVisible (each && isVisible());
        if (! each)
            continue;
        h->setBounds (juce::Rectangle<int> (34, 34).withCentre (handlePoint (h->direction()).roundToInt()));
        if (corpus != nullptr && h->source < (int) corpus->sources.size())
        {
            const auto& src = corpus->sources[(size_t) h->source];
            auto col = colourOf ? colourOf (src) : ui().accent();
            const int sh = shapeOf ? shapeOf (src) : 0;
            if (col != h->colour || sh != h->shape) { h->colour = col; h->shape = sh; h->repaint(); }
        }
    }
    setDescription (describe());
    repaint();
}

juce::String RoomView::describe() const
{
    if (fmt.kind == Spatial::Kind::stereo)
        return "Stereo, from left to right. New grains appear at the top and slide down as they fade.";
    if (fmt.kind == Spatial::Kind::ring)
        return juce::String (fmt.channels) + " speakers around you, seen from above. Each grain appears where it is heard, then fades.";
    return "Ambisonics: the whole sphere around you. Drag to turn the view, double-click to reset.";
}

void RoomView::mouseDown (const juce::MouseEvent&)
{
    dragYaw = yaw;
    dragTilt = tilt;
}

void RoomView::mouseDrag (const juce::MouseEvent& e)
{
    if (fmt.kind != Spatial::Kind::ambisonics)
        return;
    yaw = dragYaw - (float) e.getDistanceFromDragStartX() * 0.4f;
    tilt = juce::jlimit (0.0f, 80.0f, dragTilt + (float) e.getDistanceFromDragStartY() * 0.3f);
    refresh();
}

void RoomView::mouseDoubleClick (const juce::MouseEvent&)
{
    yaw = -20.0f;
    tilt = 24.0f;
    refresh();
}

//==============================================================================
void RoomView::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    auto bounds = getLocalBounds().toFloat();
    g.setColour (t.map);
    g.fillRoundedRectangle (bounds, 16.0f);

    // heading
    g.setColour (t.fg);
    g.setFont (s.font (17.0f, true));
    const juce::String title = fmt.kind == Spatial::Kind::stereo ? "Stereo" : (fmt.kind == Spatial::Kind::ring ? "Room" : "Room in 3D");
    g.drawText (title, juce::Rectangle<float> (20.0f, 14.0f, bounds.getWidth() - 40.0f, 22.0f), juce::Justification::centredLeft);
    juce::String sub;
    if (fmt.kind == Spatial::Kind::stereo) sub = "Left to right. Newest grains at the top, sliding down as they fade.";
    else if (fmt.kind == Spatial::Kind::ring) sub = juce::String (fmt.channels) + " speakers, seen from above. You are in the middle.";
    else sub = juce::String ("Ambisonics, order ") + juce::String (fmt.order) + ": the whole sphere around you.";
    g.setColour (t.muted);
    g.setFont (s.font (13.5f));
    g.drawText (sub, juce::Rectangle<float> (20.0f, 36.0f, bounds.getWidth() - 40.0f, 18.0f), juce::Justification::centredLeft, true);

    auto pill = [&] (const juce::String& text, juce::Point<float> at)
    {
        g.setFont (s.font (12.5f, true));
        const float tw = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 18.0f;
        auto r = juce::Rectangle<float> (tw, 22.0f).withCentre (at);
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, 11.0f);
        g.setColour (t.line);
        g.drawRoundedRectangle (r.reduced (0.5f), 11.0f, 1.0f);
        g.setColour (t.fg);
        g.drawText (text, r, juce::Justification::centred);
    };
    auto speaker = [&] (const juce::String& text, juce::Point<float> p)
    {
        auto r = juce::Rectangle<float> (26.0f, 26.0f).withCentre (p);
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, 7.0f);
        g.setColour (t.fg);
        g.drawRoundedRectangle (r.reduced (0.75f), 7.0f, 1.5f);
        g.setFont (s.font (12.5f, true));
        g.drawText (text, r, juce::Justification::centred);
    };
    auto listener = [&] (juce::Point<float> c, juce::Point<float> towardsFront)
    {
        auto dir = towardsFront - c;
        const float len = std::sqrt (dir.x * dir.x + dir.y * dir.y);
        if (len > 0.1f) dir /= len;
        juce::Path nose;
        const juce::Point<float> n (-dir.y, dir.x);
        nose.addTriangle (c + dir * 12.0f, c + dir * 5.0f + n * 4.5f, c + dir * 5.0f - n * 4.5f);
        g.setColour (t.fg);
        g.fillPath (nose);
        g.setColour (t.raised);
        g.fillEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f);
        g.setColour (t.fg);
        g.drawEllipse (c.x - 7.0f, c.y - 7.0f, 14.0f, 14.0f, 1.5f);
    };

    const auto now = juce::Time::getMillisecondCounter();
    struct Mark { float az, el, dist, age; int shape; juce::Colour colour; };
    std::vector<Mark> marks;
    for (auto& m : proc.engine.grainMarks)
    {
        const int src = m.source.load();
        const auto age = (float) (now - m.when.load());
        if (src < 0 || age > grainMarkLifeMs || corpus == nullptr || src >= (int) corpus->sources.size())
            continue;
        const auto& source = corpus->sources[(size_t) src];
        marks.push_back ({ m.az.load(), m.el.load(), m.dist.load(), age, shapeOf ? shapeOf (source) : 0,
                           colourOf ? colourOf (source) : s.accent() });
    }
    std::sort (marks.begin(), marks.end(), [] (const Mark& a, const Mark& b) { return a.age > b.age; });
    const bool model = proc.engine.modelPlaceActive.load();

    juce::String legend = "Each mark is a grain: it appears where it is heard, then fades.";

    if (fmt.kind == Spatial::Kind::ring)
    {
        const auto c = area.getCentre();
        const float R = radius();
        g.setColour (t.panel.withAlpha (0.6f));
        g.fillEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f);
        g.setColour (t.line);
        g.drawEllipse (c.x - R * 0.5f, c.y - R * 0.5f, R, R, 1.0f);
        g.setColour (t.muted);
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.5f);
        for (auto& m : marks)
            drawGrainMark (g, m.shape, topPoint (m.az, m.dist), m.age, m.colour);
        if (model)
        {
            const auto p = topPoint (proc.engine.modelAz.load(), proc.engine.modelDist.load());
            g.setColour (s.accent());
            g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 3.0f);
        }
        for (int k = 0; k < fmt.channels; ++k)
            speaker (juce::String (k + 1), topPoint (fmt.speakerAngle (k), 1.0f));
        pill ("Front", topPoint (0.0f, 1.0f).translated (0.0f, -30.0f));
        listener (c, c.translated (0.0f, -10.0f));
        legend << " In the middle = from every speaker.";
    }
    else if (fmt.kind == Spatial::Kind::stereo)
    {
        const float half = juce::jmin (area.getWidth() * 0.5f - 40.0f, 420.0f);
        const float cx = area.getCentreX(), y0 = area.getY() + 40.0f, y1 = area.getBottom() - 6.0f;
        g.setColour (t.panel.withAlpha (0.6f));
        g.fillRoundedRectangle (juce::Rectangle<float> (cx - half - 16.0f, y0 - 16.0f, half * 2.0f + 32.0f, y1 - y0 + 22.0f), 12.0f);
        juce::Path centre;
        centre.startNewSubPath (cx, y0 - 8.0f);
        centre.lineTo (cx, y1);
        juce::Path dashed;
        const float dash[] = { 4.0f, 4.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, centre, dash, 2);
        g.setColour (t.line);
        g.fillPath (dashed);
        for (auto& m : marks)
        {
            const float pan = std::sin (m.az * degToRad) * m.dist;
            drawGrainMark (g, m.shape, { cx + pan * half, y0 + m.age / grainMarkLifeMs * (y1 - y0) }, m.age, m.colour);
        }
        if (model)
        {
            const float pan = std::sin (proc.engine.modelAz.load() * degToRad) * proc.engine.modelDist.load();
            g.setColour (s.accent());
            g.drawEllipse (cx + pan * half - 11.0f, y0 - 11.0f, 22.0f, 22.0f, 3.0f);
        }
        speaker ("L", { cx - half, area.getY() + 14.0f });
        speaker ("R", { cx + half, area.getY() + 14.0f });
        legend = "Left to right = where a grain is heard. Older grains slide down and fade.";
    }
    else
    {
        // the whole sphere, in 3D
        const auto c = area.getCentre();
        const float R = radius();
        auto curve = [&] (std::function<P3 (float)> f, float from, float to, float step, float farAlpha, float nearAlpha, float width)
        {
            P3 a = f (from);
            for (float u = from + step; u <= to + 0.001f; u += step)
            {
                const P3 b = f (u);
                g.setColour (t.muted.withAlpha ((a.depth + b.depth) > 0.0f ? farAlpha : nearAlpha));
                g.drawLine (a.x, a.y, b.x, b.y, width);
                a = b;
            }
        };
        g.setColour (t.line.withAlpha (0.6f));
        g.drawEllipse (c.x - R, c.y - R, R * 2.0f, R * 2.0f, 1.0f);
        for (float el : { -60.0f, -30.0f, 30.0f, 60.0f })
            curve ([&] (float az) { return project (az, el, 1.0f); }, 0.0f, 360.0f, 6.0f, 0.12f, 0.4f, 1.0f);
        for (float az : { 0.0f, 90.0f, 180.0f, 270.0f })
            curve ([&] (float el) { return project (az, el, 1.0f); }, -90.0f, 90.0f, 5.0f, 0.12f, 0.4f, 1.0f);

        struct M3 { P3 p, floor; float age, scale; int shape; juce::Colour colour; bool below; };
        std::vector<M3> m3;
        for (auto& m : marks)
        {
            const P3 p = project (m.az, m.el, 1.0f);
            const P3 f = project (m.az, 0.0f, std::cos (m.el * degToRad));
            m3.push_back ({ p, f, m.age, 1.0f - 0.25f * p.depth, m.shape, m.colour, m.el < 0.0f });
        }
        std::sort (m3.begin(), m3.end(), [] (const M3& a, const M3& b) { return a.p.depth > b.p.depth; });
        auto drawMarks = [&] (bool below)
        {
            for (auto& m : m3)
            {
                if (m.below != below) continue;
                const float a = std::pow (juce::jmax (0.0f, 1.0f - m.age / grainMarkLifeMs), 1.3f);
                juce::Path stem;
                stem.startNewSubPath (m.p.x, m.p.y);
                stem.lineTo (m.floor.x, m.floor.y);
                juce::Path dashed;
                const float dash[] = { 2.0f, 3.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, stem, dash, 2);
                g.setColour (m.colour.withAlpha (0.8f * a));
                g.fillPath (dashed);
                const float rx = 4.0f * m.scale, ry = rx * std::sin (juce::jmax (8.0f, tilt) * degToRad);
                g.setColour (m.colour.withAlpha (0.35f * a));
                g.fillEllipse (m.floor.x - rx, m.floor.y - ry, rx * 2.0f, ry * 2.0f);
                drawGrainMark (g, m.shape, { m.p.x, m.p.y }, m.age, m.colour, m.scale);
            }
        };
        drawMarks (true);

        // the floor (horizontal plane through your ears), slightly see-through
        juce::Path floor;
        for (int a = 0; a <= 360; a += 4)
        {
            const P3 p = project ((float) a, 0.0f, 1.0f);
            if (a == 0) floor.startNewSubPath (p.x, p.y); else floor.lineTo (p.x, p.y);
        }
        floor.closeSubPath();
        g.setColour (t.map.withAlpha (0.55f));
        g.fillPath (floor);
        curve ([&] (float az) { return project (az, 0.0f, 0.5f); }, 0.0f, 360.0f, 6.0f, 0.3f, 0.3f, 1.0f);
        curve ([&] (float az) { return project (az, 0.0f, 1.0f); }, 0.0f, 360.0f, 4.0f, 0.55f, 0.95f, 1.6f);

        // which way is front: an arrow on the floor
        {
            const P3 a = project (0.0f, 0.0f, 0.14f), b = project (0.0f, 0.0f, 0.62f);
            g.setColour (s.accent().withAlpha (0.9f));
            g.drawArrow ({ a.x, a.y, b.x, b.y }, 2.0f, 10.0f, 9.0f);
        }
        const P3 me = project (0.0f, 0.0f, 0.0f), ahead = project (0.0f, 0.0f, 0.3f);
        listener ({ me.x, me.y }, { ahead.x, ahead.y });

        drawMarks (false);
        if (model)
        {
            const P3 p = project (proc.engine.modelAz.load(), proc.engine.modelEl.load(), juce::jmax (0.05f, proc.engine.modelDist.load()));
            g.setColour (s.accent());
            g.drawEllipse (p.x - 11.0f, p.y - 11.0f, 22.0f, 22.0f, 3.0f);
        }

        const P3 front = project (0.0f, 0.0f, 0.78f);
        pill ("Front", { front.x, front.y - 4.0f });
        const P3 back = project (180.0f, 0.0f, 1.0f);
        pill ("Back", { back.x, back.y + 18.0f });
        for (auto [lab, az] : { std::pair<const char*, float> { "Left", -90.0f }, { "Right", 90.0f } })
        {
            const P3 p = project (az, 0.0f, 1.0f);
            const float out = p.x < c.x ? -34.0f : 34.0f;
            pill (lab, { p.x + out, p.y });
        }
        pill ("Above", { c.x, c.y - R - 16.0f });
        pill ("Below", { c.x, c.y + R + 16.0f });
        legend << " Dotted lines drop to the floor: above or below you. Drag to turn the view, double-click to reset.";
    }

    g.setColour (t.muted);
    g.setFont (s.font (13.0f));
    g.drawFittedText (legend, getLocalBounds().removeFromBottom (40).reduced (20, 4), juce::Justification::centredLeft, 2);
}
}
