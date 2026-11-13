#pragma once

#include "Widgets.h"
#include "../PluginProcessor.h"

namespace lt
{
    enum class Mode { explore = 0, gesture = 1, keys = 2, space = 3 };
    // "Front", "Front right, 45 degrees"...
    juce::String describeDirection (float degrees);

    // The cursor: a real focusable control. Arrow keys move it (Shift = fine),
    // and screen readers hear its position and what is in the mix.
    class CursorHandle : public juce::Component
    {
    public:
        explicit CursorHandle (SedimentsProcessor& p);
        void paint (juce::Graphics&) override;
        bool keyPressed (const juce::KeyPress&) override;
        bool keyStateChanged (bool) override;       // Enter held = like holding the mouse ("Clicking")
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        void moveBy (float dx, float dy);

        juce::String valueText;
    private:
        SedimentsProcessor& proc;
    };

    class MapView : public juce::Component
    {
    public:
        explicit MapView (SedimentsProcessor& p);

        void setMode (Mode m) { mode = m; repaint(); }
        Mode getMode() const { return mode; }
        void highlightSound (const juce::String& path);
        // The room drawn over the map in the Space tab (an ellipse in map space).
        juce::Point<float> roomPoint (float azDegrees, float radius01) const;
        float roomAngleAt (juce::Point<float> p) const;
        void refresh();                         // ~30 times per second
        void invalidateDots() { dotsDirty = true; repaint(); }
        void hearNearestToCursor();

        bool recordArmed = false;
        bool dragHighlight = false;
        std::function<void()> onRecordingFinished;
        // colour and marker shape of a source (custom colours live in the processor)
        std::function<juce::Colour (const Corpus::Source&)> colourOf;
        std::function<int (const Corpus::Source&)> shapeOf;

        CursorHandle cursor;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;

        std::shared_ptr<const Corpus> getCorpus() const { return corpus; }

    private:
        SedimentsProcessor& proc;
        Mode mode = Mode::explore;
        std::shared_ptr<const Corpus> corpus;
        int shownCorpus = -1;
        juce::Image dots;
        bool dotsDirty = true;
        juce::Path contours;                     // normalised 0..1 coordinates
        struct Region { juce::String name; float x, y; int source; };
        std::vector<Region> regions;
        std::shared_ptr<const std::vector<LatentEngine::PathPoint>> shownPath;
        int hover = -1;
        int hoverSource = -1, listHighlight = -1;     // sound shown on hover (map or list)
        float dotSize = 8.0f;
        juce::Point<float> hoverMouse;
        bool recording = false;
        double recordStart = 0.0;
        std::vector<LatentEngine::PathPoint> recorded;
        int heldNote = -1;
        juce::Point<float> shownCursor { 0.5f, 0.5f };

        void rebuildDots();
        void rebuildContours();
        void rebuildRegions();
        void setFromMouse (juce::Point<float> p);
        int nearestPoint (juce::Point<float> p, float maxPixels) const;
        int zoneAt (juce::Point<float> p) const;
        bool showZones() const;
        void positionCursor();
        void paintSpace (juce::Graphics&);
    };
}
