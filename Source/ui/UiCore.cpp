#include "UiCore.h"
#include "BinaryData.h"

#include <map>

namespace lt
{
//==============================================================================
// Themes

static std::vector<Theme> makeThemes()
{
    auto c = [] (const char* hex) { return juce::Colour::fromString (juce::String ("FF") + juce::String (hex).substring (1)); };
    std::vector<Theme> t;

    Theme dusk;
    dusk.id = "dusk"; dusk.name = "Dusk"; dusk.dark = true;
    dusk.bg = c ("#121614"); dusk.panel = c ("#1A1F1C"); dusk.raised = c ("#252C28"); dusk.line = c ("#3A433E");
    dusk.fg = c ("#EEF1EA"); dusk.muted = c ("#AEB8B0"); dusk.accent = c ("#F0B45E"); dusk.onAccent = c ("#1B1406");
    dusk.map = c ("#0E1210"); dusk.contour = c ("#EEF1EA").withAlpha (0.11f); dusk.focus = c ("#8FD3FF"); dusk.rec = c ("#FF8A80");
    dusk.keyWhite = c ("#E9E6DD"); dusk.keyBlack = c ("#0E1210"); dusk.keyInk = c ("#15181A");
    t.push_back (dusk);

    Theme day;
    day.id = "daylight"; day.name = "Daylight"; day.dark = false;
    day.bg = c ("#EFECE4"); day.panel = c ("#FFFFFF"); day.raised = c ("#F3F0E9"); day.line = c ("#BDB7AA");
    day.fg = c ("#15181A"); day.muted = c ("#4A5048"); day.accent = c ("#8F5300"); day.onAccent = c ("#FFFFFF");
    day.map = c ("#FBFAF6"); day.contour = c ("#15181A").withAlpha (0.14f); day.focus = c ("#0057B8"); day.rec = c ("#B3261E");
    day.keyWhite = c ("#FFFFFF"); day.keyBlack = c ("#15181A"); day.keyInk = c ("#15181A");
    t.push_back (day);

    Theme hc;
    hc.id = "contrast"; hc.name = "High contrast"; hc.dark = true;
    hc.bg = c ("#000000"); hc.panel = c ("#0A0A0A"); hc.raised = c ("#1C1C1C"); hc.line = c ("#9A9A9A");
    hc.fg = c ("#FFFFFF"); hc.muted = c ("#E2E2E2"); hc.accent = c ("#FFD166"); hc.onAccent = c ("#000000");
    hc.map = c ("#000000"); hc.contour = c ("#FFFFFF").withAlpha (0.30f); hc.focus = c ("#4FC3FF"); hc.rec = c ("#FF6B6B");
    hc.keyWhite = c ("#FFFFFF"); hc.keyBlack = c ("#000000"); hc.keyInk = c ("#000000");
    t.push_back (hc);

    Theme night;
    night.id = "night"; night.name = "Night"; night.dark = true;
    night.bg = c ("#0B0907"); night.panel = c ("#12100D"); night.raised = c ("#1C1915"); night.line = c ("#332D25");
    night.fg = c ("#D8C9B4"); night.muted = c ("#A3937D"); night.accent = c ("#D98C4A"); night.onAccent = c ("#140C05");
    night.map = c ("#080706"); night.contour = c ("#D8C9B4").withAlpha (0.09f); night.focus = c ("#E6B97A"); night.rec = c ("#E07A66");
    night.keyWhite = c ("#CDBFA9"); night.keyBlack = c ("#080706"); night.keyInk = c ("#140C05");
    t.push_back (night);
    return t;
}

static const std::vector<Theme>& themes()
{
    static const std::vector<Theme> t = makeThemes();
    return t;
}

const juce::StringArray& themeIds()
{
    static const juce::StringArray ids { "dusk", "daylight", "contrast", "night" };
    return ids;
}

const Theme& themeById (const juce::String& id)
{
    for (auto& t : themes())
        if (t.id == id)
            return t;
    return themes().front();
}

//==============================================================================
// Sound palettes. Colours are adjusted at draw time so they always stand out
// from the map background (3:1, the WCAG level for graphics).

static std::vector<SoundPalette> makePalettes()
{
    auto c = [] (juce::uint32 argb) { return juce::Colour (argb); };
    return {
        { "standard", "Standard",
          { c (0xFFF0B45E), c (0xFF7FC8BA), c (0xFFC9A7F2), c (0xFFE8E3D6), c (0xFFF28B82), c (0xFF8AB4F8), c (0xFFB5D77A), c (0xFFF7A8D0) } },
        // Okabe & Ito: designed to stay distinct for protanopia and deuteranopia.
        { "redgreen", "Red-green safe",
          { c (0xFFE69F00), c (0xFF56B4E9), c (0xFF009E73), c (0xFFF0E442), c (0xFF0072B2), c (0xFFD55E00), c (0xFFCC79A7), c (0xFFBBBBBB) } },
        // Paul Tol's set, avoiding the blue / yellow axis that tritanopia confuses.
        { "blueyellow", "Blue-yellow safe",
          { c (0xFFEE3377), c (0xFF33BBEE), c (0xFFCC3311), c (0xFFDDDDDD), c (0xFF009988), c (0xFFF4A6B8), c (0xFF8E8E8E), c (0xFF88CCEE) } }
    };
}

static const std::vector<SoundPalette>& palettes()
{
    static const std::vector<SoundPalette> p = makePalettes();
    return p;
}

const juce::StringArray& paletteIds()
{
    static const juce::StringArray ids { "standard", "redgreen", "blueyellow" };
    return ids;
}

const SoundPalette& paletteById (const juce::String& id)
{
    for (auto& p : palettes())
        if (p.id == id)
            return p;
    return palettes().front();
}

//==============================================================================
static double luminance (juce::Colour c)
{
    auto ch = [] (juce::uint8 v)
    {
        const double s = v / 255.0;
        return s <= 0.03928 ? s / 12.92 : std::pow ((s + 0.055) / 1.055, 2.4);
    };
    return 0.2126 * ch (c.getRed()) + 0.7152 * ch (c.getGreen()) + 0.0722 * ch (c.getBlue());
}

double contrastRatio (juce::Colour a, juce::Colour b)
{
    const double la = luminance (a), lb = luminance (b);
    return (juce::jmax (la, lb) + 0.05) / (juce::jmin (la, lb) + 0.05);
}

juce::Colour ensureContrast (juce::Colour c, juce::Colour bg, double ratio)
{
    c = c.withAlpha (1.0f);
    if (contrastRatio (c, bg) >= ratio)
        return c;
    const auto target = luminance (bg) < 0.4 ? juce::Colours::white : juce::Colours::black;
    for (float t = 0.05f; t <= 1.0f; t += 0.05f)
    {
        auto m = c.interpolatedWith (target, t);
        if (contrastRatio (m, bg) >= ratio)
            return m;
    }
    return target;
}

juce::Colour textOn (juce::Colour bg)
{
    const auto dark = juce::Colour (0xFF111111), light = juce::Colour (0xFFFFFFFF);
    return contrastRatio (dark, bg) >= contrastRatio (light, bg) ? dark : light;
}

//==============================================================================
static UiSettings* currentSettings = nullptr;

UiSettings::UiSettings()
{
    currentSettings = this;
    atkRegular = juce::Typeface::createSystemTypefaceFor (BinaryData::AtkinsonHyperlegibleRegular_ttf, BinaryData::AtkinsonHyperlegibleRegular_ttfSize);
    atkBold    = juce::Typeface::createSystemTypefaceFor (BinaryData::AtkinsonHyperlegibleBold_ttf, BinaryData::AtkinsonHyperlegibleBold_ttfSize);
    odRegular  = juce::Typeface::createSystemTypefaceFor (BinaryData::OpenDyslexicRegular_otf, BinaryData::OpenDyslexicRegular_otfSize);
    odBold     = juce::Typeface::createSystemTypefaceFor (BinaryData::OpenDyslexicBold_otf, BinaryData::OpenDyslexicBold_otfSize);

    juce::PropertiesFile::Options o;
    o.applicationName = "Sediments";
    o.filenameSuffix = ".settings";
    o.folderName = "Sediments";
    o.osxLibrarySubFolder = "Application Support";
    file = std::make_unique<juce::PropertiesFile> (o);
    load();
}

UiSettings::~UiSettings()
{
    if (currentSettings == this)
        currentSettings = nullptr;
}

void UiSettings::load()
{
    themeId   = file->getValue ("theme", themeId);
    paletteId = file->getValue ("palette", paletteId);
    auto acc  = file->getValue ("accent");
    customAccent = acc.isNotEmpty() ? juce::Colour::fromString (acc) : juce::Colour();
    fontId    = file->getValue ("font", fontId);
    textScale = juce::jlimit (1.0f, 1.5f, (float) file->getDoubleValue ("textScale", textScale));
    spacing   = juce::jlimit (0.0f, 0.06f, (float) file->getDoubleValue ("spacing", spacing));
    shapes    = file->getBoolValue ("shapes", shapes);
    calm      = file->getBoolValue ("calm", calm);
    announce  = file->getBoolValue ("announce", announce);
    if (! themeIds().contains (themeId)) themeId = "dusk";
    if (! paletteIds().contains (paletteId)) paletteId = "standard";
}

void UiSettings::save()
{
    file->setValue ("theme", themeId);
    file->setValue ("palette", paletteId);
    file->setValue ("accent", customAccent.isTransparent() ? juce::String() : customAccent.toString());
    file->setValue ("font", fontId);
    file->setValue ("textScale", (double) textScale);
    file->setValue ("spacing", (double) spacing);
    file->setValue ("shapes", shapes);
    file->setValue ("calm", calm);
    file->setValue ("announce", announce);
    file->saveIfNeeded();
    sendChangeMessage();
}

void UiSettings::resetToDefaults()
{
    themeId = "dusk"; paletteId = "standard"; customAccent = {}; fontId = "atkinson";
    textScale = 1.0f; spacing = 0.0f; shapes = true; calm = false; announce = false;
    save();
}

juce::Colour UiSettings::accent() const
{
    return customAccent.isTransparent() ? theme().accent : customAccent;
}

juce::Colour UiSettings::soundColour (int index, juce::Colour customOverride) const
{
    auto& pal = paletteById (paletteId).colours;
    auto c = ! customOverride.isTransparent() ? customOverride : pal[(size_t) (juce::jmax (0, index) % (int) pal.size())];
    return ensureContrast (c, theme().map, 3.0);
}

juce::Font UiSettings::fontFor (const juce::String& id, float size, bool bold) const
{
    // Sizes are em sizes (like CSS px), so every font gets the same letter size
    // whatever its own line metrics are.
    juce::FontOptions o;
    if (id == "opendyslexic")
        o = juce::FontOptions().withTypeface (bold ? odBold : odRegular);
    else if (id == "system")
        o = juce::FontOptions().withStyle (bold ? "Bold" : "Regular");
    else
        o = juce::FontOptions().withTypeface (bold ? atkBold : atkRegular);
    // OpenDyslexic letters are much wider: a touch smaller keeps labels from being cut.
    return juce::Font (o.withPointHeight (id == "opendyslexic" ? size * 0.9f : size).withKerningFactor (spacing));
}

juce::Font UiSettings::font (float size, bool bold) const
{
    return fontFor (fontId, size, bold);
}

UiSettings& ui()
{
    jassert (currentSettings != nullptr);
    return *currentSettings;
}

//==============================================================================
namespace Icons
{
    static const std::map<juce::String, const char*>& table()
    {
        static const std::map<juce::String, const char*> t {
            { "compass",  "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M15.5 8.5l-2 5-5 2 2-5z" },
            { "gesture",  "M3 17c3-6 6-10 9-6s5 4 9-4" },
            { "keys",     "M4 5h16v14H4z M9 5v9 M15 5v9 M12 14v5" },
            { "help",     "M12 3a9 9 0 1 0 0 18a9 9 0 1 0 0-18z M9.5 9.5a2.5 2.5 0 1 1 3.5 2.3c-.7.3-1 .8-1 1.5v.7 M12 17.2v.1" },
            { "settings", "M4 7h10 M18 7h2 M4 17h4 M12 17h8 M16 5v4 M10 15v4" },
            { "play",     "M8 5l11 7-11 7z" },
            { "pause",    "M8 5v14 M16 5v14" },
            { "stop",     "M7 7h10v10H7z" },
            { "x",        "M6 6l12 12 M18 6L6 18" },
            { "plus",     "M12 5v14 M5 12h14" },
            { "folder",   "M3 7h6l2 2h10v10H3z" },
            { "check",    "M5 12l5 5 9-10" },
            { "chevronDown", "M6 9l6 6 6-6" },
            { "chevronUp",   "M6 15l6-6 6 6" },
            { "wave",     "M3 12h2 M7 8v8 M11 5v14 M15 9v6 M19 11v2" },
            { "contour",  "M12 5c5.5 0 10 3.1 10 7s-4.5 7-10 7S2 15.9 2 12s4.5-7 10-7z M12 9c2.8 0 5 1.3 5 3s-2.2 3-5 3-5-1.3-5-3 2.2-3 5-3z" },
            { "link",     "M14 4h6v6 M20 4l-9 9 M18 14v6H4V6h6" },
            { "palette",  "M12 3a9 9 0 1 0 0 18c1.1 0 1.5-.8 1.2-1.6-.4-1 .1-2.4 1.6-2.4H17a4 4 0 0 0 4-4c0-5.5-4-10-9-10z M7.5 11.5h.1 M10 7.5h.1 M14.5 7.5h.1" },
            { "record",   "M12 6a6 6 0 1 0 0 12a6 6 0 1 0 0-12z" }
        };
        return t;
    }

    juce::Path get (const juce::String& name)
    {
        static std::map<juce::String, juce::Path> cache;
        auto it = cache.find (name);
        if (it != cache.end())
            return it->second;
        juce::Path p;
        auto t = table().find (name);
        if (t != table().end())
            p = juce::Drawable::parseSVGPath (t->second);
        cache[name] = p;
        return p;
    }

    void draw (juce::Graphics& g, const juce::String& name, juce::Rectangle<float> area, juce::Colour c, float strokeWidth, bool filled)
    {
        auto p = get (name);
        if (p.isEmpty())
            return;
        const float s = juce::jmin (area.getWidth(), area.getHeight()) / 24.0f;
        auto tr = juce::AffineTransform::scale (s).translated (area.getCentreX() - 12.0f * s, area.getCentreY() - 12.0f * s);
        g.setColour (c);
        if (filled)
            g.fillPath (p, tr);
        else
            g.strokePath (p, juce::PathStrokeType (strokeWidth * s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), tr);
    }
}

void drawGlyph (juce::Graphics& g, int shape, juce::Point<float> c, float s, juce::Colour col)
{
    g.setColour (col);
    const float h = s * 0.5f;
    switch (shape)
    {
        case 1: g.fillRoundedRectangle (c.x - h, c.y - h, s, s, juce::jmax (1.0f, s * 0.15f)); break;
        case 2: { juce::Path p; p.addTriangle (c.x, c.y - h * 1.15f, c.x + h * 1.1f, c.y + h * 0.85f, c.x - h * 1.1f, c.y + h * 0.85f); g.fillPath (p); } break;
        case 3: { juce::Path p; const float d = h * 1.3f; p.addQuadrilateral (c.x, c.y - d, c.x + d, c.y, c.x, c.y + d, c.x - d, c.y); g.fillPath (p); } break;
        case 4: { juce::Path p; p.addTriangle (c.x, c.y + h * 1.15f, c.x + h * 1.1f, c.y - h * 0.85f, c.x - h * 1.1f, c.y - h * 0.85f); g.fillPath (p); } break;
        case 5: { const float t = juce::jmax (1.6f, s * 0.28f); g.drawEllipse (c.x - h + t * 0.5f, c.y - h + t * 0.5f, s - t, s - t, t); } break;
        default: g.fillEllipse (c.x - h, c.y - h, s, s); break;
    }
}

void drawFocusRing (juce::Graphics& g, juce::Rectangle<float> r, float radius, const UiSettings& s)
{
    g.setColour (s.theme().focus);
    g.drawRoundedRectangle (r.expanded (3.0f), radius + 3.0f, 2.5f);
}

//==============================================================================
LtButton::LtButton (const juce::String& text, const juce::String& icon, Style s)
    : juce::Button (text), iconName (icon), style (s)
{
    setTitle (text);
    setWantsKeyboardFocus (true);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int LtButton::idealWidth (float height) const
{
    auto& s = ui();
    if (iconOnly)
        return (int) height;
    const float fs = style == Style::segment ? 14.5f : 15.0f;
    float w = juce::GlyphArrangement::getStringWidth (s.fontFor (fontOverride.isNotEmpty() ? fontOverride : s.fontId, fs, true), getButtonText());
    if (style == Style::step)
    {
        auto sub = juce::GlyphArrangement::getStringWidth (s.font (15.0f, true), subText);
        w = juce::jmax (juce::GlyphArrangement::getStringWidth (s.font (12.0f), getButtonText()), sub) + height + 8.0f;
    }
    if (iconName.isNotEmpty()) w += 26.0f;
    return (int) std::ceil (w + (style == Style::segment ? 26.0f : 32.0f));
}

void LtButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto& s = ui();
    auto& t = s.theme();
    const auto acc = s.accent(), onAcc = s.onAccent();
    auto b = getLocalBounds().toFloat();
    const bool on = getToggleState();
    const float alpha = isEnabled() ? 1.0f : 0.45f;
    float radius = 12.0f;

    juce::Colour fill = juce::Colours::transparentBlack, border = juce::Colours::transparentBlack, ink = t.fg;
    float borderW = 1.0f;
    bool bold = false;

    auto hoverFill = t.dark ? t.raised.brighter (0.12f) : t.raised.darker (0.04f);

    switch (style)
    {
        case Style::primary:   fill = over ? acc.brighter (0.08f) : acc; ink = onAcc; bold = true; break;
        case Style::secondary: fill = over ? hoverFill : t.raised; border = t.line; break;
        case Style::ghost:     fill = over ? t.raised : juce::Colours::transparentBlack; break;
        case Style::danger:    fill = over ? hoverFill : t.raised; border = on ? t.rec : t.line; borderW = on ? 2.0f : 1.0f; bold = true; break;
        case Style::segment:
            radius = 11.0f;
            if (on) { fill = acc; ink = onAcc; bold = true; }
            else fill = over ? (t.dark ? t.panel.brighter (0.1f) : t.panel) : juce::Colours::transparentBlack;
            break;
        case Style::step:
            radius = b.getHeight() * 0.5f;
            fill = stepState == 0 ? juce::Colours::transparentBlack : t.raised;
            if (over) fill = hoverFill;
            border = stepState == 1 ? acc : t.line;
            borderW = stepState == 1 ? 2.0f : 1.0f;
            break;
        case Style::keyWhite:
            radius = 8.0f;
            fill = highlight || down ? acc : t.keyWhite;
            ink = highlight || down ? onAcc : t.keyInk;
            border = t.line; bold = true;
            break;
        case Style::keyBlack:
            radius = 6.0f;
            fill = highlight || down ? acc : t.keyBlack;
            ink = highlight || down ? onAcc : juce::Colour (0xFFEEF1EA);
            border = t.dark ? t.line : t.keyBlack; bold = true;
            break;
        case Style::swatch:
            break;
    }
    if (down && style != Style::keyWhite && style != Style::keyBlack && style != Style::swatch)
        fill = fill.isTransparent() ? t.raised : fill.darker (0.08f);

    g.setOpacity (alpha);

    if (style == Style::swatch)
    {
        auto c = b.reduced (5.0f);
        auto sq = c.withSizeKeepingCentre (juce::jmin (c.getWidth(), c.getHeight()), juce::jmin (c.getWidth(), c.getHeight()));
        g.setColour (swatch);
        g.fillEllipse (sq);
        g.setColour (t.line);
        g.drawEllipse (sq, 1.0f);
        if (on)
        {
            g.setColour (t.fg);
            g.drawEllipse (sq.expanded (3.0f), 2.0f);
            Icons::draw (g, "check", sq.reduced (sq.getWidth() * 0.22f), textOn (swatch), 3.0f);
        }
        if (hasKeyboardFocus (false))
            drawFocusRing (g, sq.expanded (3.0f), sq.getWidth() * 0.5f + 3.0f, s);
        return;
    }

    if (style == Style::keyWhite || style == Style::keyBlack)
    {
        juce::Path p;
        p.addRoundedRectangle (b.getX(), b.getY(), b.getWidth(), b.getHeight(), radius, radius, false, false, true, true);
        g.setColour (fill);
        g.fillPath (p);
        g.setColour (border);
        g.strokePath (p, juce::PathStrokeType (1.0f));
        g.setColour (ink);
        g.setFont (s.font (style == Style::keyBlack ? 12.0f : 14.0f, true));
        g.drawText (getButtonText(), b.removeFromBottom (style == Style::keyBlack ? 22.0f : 28.0f), juce::Justification::centred);
        if (hasKeyboardFocus (false))
            drawFocusRing (g, getLocalBounds().toFloat().reduced (2.0f), radius, s);
        return;
    }

    if (! fill.isTransparent())
    {
        g.setColour (fill);
        g.fillRoundedRectangle (b.reduced (borderW * 0.5f), radius);
    }
    if (! border.isTransparent())
    {
        g.setColour (border);
        g.drawRoundedRectangle (b.reduced (borderW * 0.5f), radius, borderW);
    }

    if (style == Style::step)
    {
        const float d = b.getHeight() - 14.0f;
        auto badge = juce::Rectangle<float> (b.getX() + 7.0f, b.getCentreY() - d * 0.5f, d, d);
        if (stepState == 2 || stepState == 1)
        {
            g.setColour (acc);
            g.fillEllipse (badge);
            if (stepState == 2)
                Icons::draw (g, "check", badge.reduced (d * 0.24f), onAcc, 3.2f);
            else
            {
                g.setColour (onAcc);
                g.setFont (s.font (14.0f, true));
                g.drawText (stepNumber, badge, juce::Justification::centred);
            }
        }
        else if (stepState == 3)
        {
            g.setColour (t.line);
            g.drawEllipse (badge.reduced (1.5f), 3.0f);
            juce::Path arc;
            arc.addCentredArc (badge.getCentreX(), badge.getCentreY(), d * 0.5f - 1.5f, d * 0.5f - 1.5f, 0.0f, 0.0f,
                               juce::MathConstants<float>::twoPi * juce::jlimit (0.02f, 1.0f, progress), true);
            g.setColour (acc);
            g.strokePath (arc, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (t.fg);
            g.setFont (s.font (10.0f, true));
            g.drawText (juce::String (juce::roundToInt (progress * 100.0f)), badge, juce::Justification::centred);
        }
        else
        {
            g.setColour (t.line);
            g.drawEllipse (badge.reduced (1.0f), 2.0f);
            g.setColour (t.muted);
            g.setFont (s.font (14.0f, true));
            g.drawText (stepNumber, badge, juce::Justification::centred);
        }
        auto text = b.withTrimmedLeft (d + 16.0f).withTrimmedRight (14.0f);
        g.setColour (t.muted);
        g.setFont (s.font (12.0f));
        g.drawText (getButtonText(), text.removeFromTop (text.getHeight() * 0.5f).withTrimmedTop (2.0f), juce::Justification::bottomLeft);
        g.setColour (t.fg);
        g.setFont (s.font (15.0f, true));
        g.drawText (subText, text.withTrimmedBottom (2.0f), juce::Justification::topLeft, true);
        if (hasKeyboardFocus (false))
            drawFocusRing (g, b, radius, s);
        return;
    }

    const float fs = style == Style::segment ? 14.5f : 15.0f;
    auto font = s.fontFor (fontOverride.isNotEmpty() ? fontOverride : s.fontId, fs, bold);
    if (iconOnly)
    {
        Icons::draw (g, iconName, b.withSizeKeepingCentre (18.0f, 18.0f), ink, 2.2f, iconName == "play" || iconName == "record");
    }
    else
    {
        const float textW = juce::GlyphArrangement::getStringWidth (font, getButtonText());
        const float iconW = iconName.isNotEmpty() ? 26.0f : 0.0f;
        const float total = juce::jmin (b.getWidth() - 16.0f, textW + iconW);
        float x = b.getCentreX() - total * 0.5f;
        if (iconName.isNotEmpty())
        {
            const bool fillIcon = iconName == "play" || iconName == "record";
            auto iconColour = iconName == "record" ? t.rec : ink;
            Icons::draw (g, iconName, { x, b.getCentreY() - 9.0f, 18.0f, 18.0f }, iconColour, 2.2f, fillIcon);
            x += iconW;
        }
        g.setColour (ink);
        g.setFont (font);
        g.drawText (getButtonText(), juce::Rectangle<float> (x, b.getY(), total - iconW + 2.0f, b.getHeight()), juce::Justification::centredLeft, true);
    }

    if (hasKeyboardFocus (false))
        drawFocusRing (g, b, radius, s);
}

//==============================================================================
Look::Look (UiSettings& s) : settings (s)
{
    refreshColours();
}

void Look::refreshColours()
{
    auto& t = settings.theme();
    setColour (juce::ScrollBar::thumbColourId, t.line);
    setColour (juce::TooltipWindow::backgroundColourId, t.panel);
    setColour (juce::TooltipWindow::textColourId, t.fg);
    setColour (juce::TooltipWindow::outlineColourId, t.line);
    setColour (juce::Label::textColourId, t.fg);
    setColour (juce::PopupMenu::backgroundColourId, t.panel);
    setColour (juce::PopupMenu::textColourId, t.fg);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, settings.accent());
    setColour (juce::PopupMenu::highlightedTextColourId, settings.onAccent());
    setColour (juce::ColourSelector::backgroundColourId, t.panel);
    setColour (juce::ColourSelector::labelTextColourId, t.fg);
    setColour (juce::TextEditor::backgroundColourId, t.raised);
    setColour (juce::TextEditor::textColourId, t.fg);
    setColour (juce::TextEditor::outlineColourId, t.line);
    setColour (juce::Slider::textBoxTextColourId, t.fg);
    setColour (juce::Slider::textBoxBackgroundColourId, t.raised);
    setColour (juce::Slider::textBoxOutlineColourId, t.line);
    setColour (juce::HyperlinkButton::textColourId, settings.accent());
}

void Look::drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                             juce::Slider::SliderStyle, juce::Slider& slider)
{
    auto& t = settings.theme();
    const auto acc = settings.accent();
    g.setOpacity (slider.isEnabled() ? 1.0f : 0.4f);
    const float cy = y + h * 0.5f;
    const float r = 11.0f;
    const float x0 = x + r, x1 = x + w - r;
    auto track = juce::Rectangle<float> (x0 - 3.0f, cy - 3.5f, x1 - x0 + 6.0f, 7.0f);
    g.setColour (t.raised);
    g.fillRoundedRectangle (track, 3.5f);
    g.setColour (t.line);
    g.drawRoundedRectangle (track, 3.5f, 1.0f);
    g.setColour (acc);
    g.fillRoundedRectangle (track.withRight (juce::jmax (track.getX() + 7.0f, pos)), 3.5f);
    const float px = juce::jlimit (x0, x1, pos);
    g.setColour (t.panel);
    g.fillEllipse (px - r - 2.0f, cy - r - 2.0f, 2.0f * r + 4.0f, 2.0f * r + 4.0f);
    g.setColour (acc);
    g.fillEllipse (px - r, cy - r, 2.0f * r, 2.0f * r);
    if (slider.hasKeyboardFocus (false))
    {
        g.setColour (t.focus);
        g.drawEllipse (px - r - 5.0f, cy - r - 5.0f, 2.0f * r + 10.0f, 2.0f * r + 10.0f, 2.5f);
    }
}

void Look::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool over, bool)
{
    auto& t = settings.theme();
    auto box = juce::Rectangle<float> (2.0f, b.getHeight() * 0.5f - 11.0f, 22.0f, 22.0f);
    g.setOpacity (b.isEnabled() ? 1.0f : 0.45f);
    if (b.getToggleState())
    {
        g.setColour (settings.accent());
        g.fillRoundedRectangle (box, 6.0f);
        Icons::draw (g, "check", box.reduced (4.0f), settings.onAccent(), 3.2f);
    }
    else
    {
        g.setColour (over ? t.raised : t.panel);
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (t.muted);
        g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.5f);
    }
    if (b.hasKeyboardFocus (false))
        drawFocusRing (g, box, 6.0f, settings);
    g.setColour (t.fg);
    g.setFont (settings.font (15.0f));
    g.drawText (b.getButtonText(), b.getLocalBounds().withTrimmedLeft (34), juce::Justification::centredLeft, true);
}

juce::Font Look::getLabelFont (juce::Label&)    { return settings.font (15.0f); }
juce::Font Look::getPopupMenuFont()             { return settings.font (15.0f); }

void Look::drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&)
{
    auto& t = settings.theme();
    g.setColour (juce::Colours::black.withAlpha (0.25f));
    g.fillPath (path, juce::AffineTransform::translation (0.0f, 3.0f));
    g.setColour (t.panel);
    g.fillPath (path);
    g.setColour (t.line);
    g.strokePath (path, juce::PathStrokeType (1.0f));
}

void Look::drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                          int thumbStart, int thumbSize, bool over, bool)
{
    auto& t = settings.theme();
    auto r = vertical ? juce::Rectangle<int> (x + w / 2 - 3, thumbStart, 6, thumbSize)
                      : juce::Rectangle<int> (thumbStart, y + h / 2 - 3, thumbSize, 6);
    g.setColour (over ? t.muted : t.line);
    g.fillRoundedRectangle (r.toFloat(), 3.0f);
}

void Look::drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h)
{
    auto& t = settings.theme();
    g.setColour (t.panel);
    g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 8.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (0.5f, 0.5f, w - 1.0f, h - 1.0f, 8.0f, 1.0f);
    g.setColour (t.fg);
    g.setFont (settings.font (14.0f));
    g.drawFittedText (text, juce::Rectangle<int> (10, 6, w - 20, h - 12), juce::Justification::centredLeft, 4);
}

juce::Rectangle<int> Look::getTooltipBounds (const juce::String& tipText, juce::Point<int> pos, juce::Rectangle<int> parentArea)
{
    const int w = juce::jmin (360, (int) juce::GlyphArrangement::getStringWidth (settings.font (14.0f), tipText) + 24);
    const int lines = (int) std::ceil (juce::GlyphArrangement::getStringWidth (settings.font (14.0f), tipText) / (float) (w - 20));
    const int h = 14 + lines * 19;
    return juce::Rectangle<int> (pos.x > parentArea.getCentreX() ? pos.x - (w + 12) : pos.x + 12,
                                 pos.y > parentArea.getCentreY() ? pos.y - (h + 12) : pos.y + 18, w, h)
        .constrainedWithin (parentArea);
}
}
