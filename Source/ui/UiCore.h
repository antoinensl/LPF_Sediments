#pragma once

// Shared look of the interface: themes, sound colour palettes, fonts, user
// display settings (global, saved for every project), icons and the
// LookAndFeel. Everything here runs on the message thread.

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace lt
{
    //==========================================================================
    struct Theme
    {
        juce::String id, name;
        bool dark = true;
        juce::Colour bg, panel, raised, line, fg, muted, accent, onAccent, map, contour, focus, rec, keyWhite, keyBlack, keyInk;
    };

    const juce::StringArray& themeIds();
    const Theme& themeById (const juce::String& id);

    struct SoundPalette
    {
        juce::String id, name;
        std::vector<juce::Colour> colours;
    };
    const juce::StringArray& paletteIds();
    const SoundPalette& paletteById (const juce::String& id);

    // WCAG contrast ratio, 1..21
    double contrastRatio (juce::Colour a, juce::Colour b);
    // Nudges the lightness of `c` until it reaches `ratio` against `bg`.
    juce::Colour ensureContrast (juce::Colour c, juce::Colour bg, double ratio);
    // Black or near-white, whichever reads better on `bg`.
    juce::Colour textOn (juce::Colour bg);

    //==========================================================================
    // Display settings, global (shared by every instance of the plugin).
    class UiSettings : public juce::ChangeBroadcaster
    {
    public:
        UiSettings();
        ~UiSettings() override;

        juce::String themeId   { "dusk" };
        juce::String paletteId { "standard" };
        juce::Colour customAccent;          // transparent = theme default
        juce::String fontId    { "atkinson" };
        float textScale = 1.0f;             // 1, 1.25, 1.5
        float spacing   = 0.0f;             // extra letter spacing (kerning factor)
        bool shapes = true, calm = false, announce = false;

        void save();                        // and notify listeners
        void resetToDefaults();

        // Resolved values for drawing
        const Theme& theme() const          { return themeById (themeId); }
        juce::Colour accent() const;
        juce::Colour onAccent() const       { return textOn (accent()); }
        juce::Colour soundColour (int index, juce::Colour customOverride = {}) const;
        juce::Font font (float size, bool bold = false) const;
        juce::Font fontFor (const juce::String& fontIdToUse, float size, bool bold) const;

    private:
        std::unique_ptr<juce::PropertiesFile> file;
        juce::Typeface::Ptr atkRegular, atkBold, odRegular, odBold;
        void load();
    };

    // The settings shared by every open editor (valid while one exists).
    UiSettings& ui();

    //==========================================================================
    // Icons are 24-unit SVG paths, drawn as strokes.
    namespace Icons
    {
        juce::Path get (const juce::String& name);
        void draw (juce::Graphics&, const juce::String& name, juce::Rectangle<float> area, juce::Colour, float strokeWidth = 2.0f, bool filled = false);
    }

    // Marker shapes for sounds: circle, square, triangle, diamond, down-triangle, ring.
    void drawGlyph (juce::Graphics&, int shapeIndex, juce::Point<float> centre, float size, juce::Colour);
    inline int shapeFor (int colourIndex, bool shapesOn) { return shapesOn ? (colourIndex % 6) : 0; }

    void drawFocusRing (juce::Graphics&, juce::Rectangle<float> r, float radius, const UiSettings&);

    //==========================================================================
    // Buttons: one class, several looks.
    class LtButton : public juce::Button
    {
    public:
        enum class Style { secondary, primary, ghost, segment, step, danger, keyWhite, keyBlack, swatch };

        LtButton (const juce::String& text = {}, const juce::String& icon = {}, Style s = Style::secondary);
        void setIcon (const juce::String& icon)     { iconName = icon; repaint(); }
        void setStyle (Style s)                     { style = s; repaint(); }
        void setIconOnly (bool b)                   { iconOnly = b; repaint(); }
        void setSubText (const juce::String& s)     { subText = s; repaint(); }
        void setHighlight (bool b)                  { if (highlight != b) { highlight = b; repaint(); } }
        void setSwatch (juce::Colour c)             { swatch = c; repaint(); }
        void setFontOverride (const juce::String& id) { fontOverride = id; repaint(); }

        // step chips: 0 = upcoming, 1 = current, 2 = done, 3 = in progress (with `progress`)
        int stepState = 0;
        float progress = 0.0f;
        juce::String stepNumber;

        void paintButton (juce::Graphics&, bool over, bool down) override;
        int idealWidth (float height) const;

    private:
        juce::String iconName, subText, fontOverride;
        Style style;
        bool iconOnly = false, highlight = false;
        juce::Colour swatch;
    };

    //==========================================================================
    class Look : public juce::LookAndFeel_V4
    {
    public:
        explicit Look (UiSettings& s);
        void refreshColours();

        void drawLinearSlider (juce::Graphics&, int x, int y, int w, int h, float pos, float minPos, float maxPos,
                               juce::Slider::SliderStyle, juce::Slider&) override;
        int getSliderThumbRadius (juce::Slider&) override { return 11; }
        void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool over, bool down) override;
        juce::Font getLabelFont (juce::Label&) override;
        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics&, const juce::Path&, juce::Image&) override;
        void drawScrollbar (juce::Graphics&, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                            int thumbStart, int thumbSize, bool over, bool down) override;
        juce::Font getPopupMenuFont() override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int w, int h) override;
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;

    private:
        UiSettings& settings;
    };
}
