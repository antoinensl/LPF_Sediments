#pragma once

#include "MapView.h"

namespace lt
{
    class RoomView;

    // "Each sound": one handle per sound on the edge of the room.
    // Drag it around, or focus it and use the arrow keys.
    class SoundDirHandle : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        SoundDirHandle (SedimentsProcessor&, RoomView&, int source, const juce::String& path, const juce::String& name);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override   { repaint(); }
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        float direction() const;
        void setDirection (float degrees);
        int source;
        juce::String path, name;
        juce::Colour colour;
        int shape = 0;
    private:
        SedimentsProcessor& proc;
        RoomView& room;
    };

    // Space tab, when grains are not placed by the map: the room on its own.
    //  - speakers: seen from above, the listener in the middle
    //  - stereo: left to right, newest grains at the top, sliding down as they age
    //  - Ambisonics: the whole sphere in 3D (drag to turn it)
    // Each grain appears where it is heard, pops, then fades in 1.5 s.
    class RoomView : public juce::Component
    {
    public:
        explicit RoomView (SedimentsProcessor&);
        void refresh();                          // ~30 times per second
        std::function<juce::Colour (const Corpus::Source&)> colourOf;
        std::function<int (const Corpus::Source&)> shapeOf;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;

        // for the handles
        juce::Point<float> handlePoint (float azDegrees) const;
        float angleAt (juce::Point<float>) const;
        std::shared_ptr<const Corpus> getCorpus() const { return corpus; }

    private:
        SedimentsProcessor& proc;
        std::shared_ptr<const Corpus> corpus;
        int shownCorpus = -1;
        juce::OwnedArray<SoundDirHandle> handles;
        Spatial::Format fmt;
        juce::Rectangle<float> area;             // where the room is drawn
        float yaw = -20.0f, tilt = 24.0f, dragYaw = 0.0f, dragTilt = 0.0f;

        struct P3 { float x, y, depth; };
        P3 project (float az, float el, float r) const;   // 3D view
        juce::Point<float> topPoint (float az, float r) const;
        float radius() const;
        void rebuildHandles();
        juce::String describe() const;
    };

    // One grain on a display: pops when it starts, then shrinks and fades.
    void drawGrainMark (juce::Graphics&, int shape, juce::Point<float> at, float ageMs, juce::Colour, float scale = 1.0f);
    constexpr float grainMarkLifeMs = 1500.0f;
}
