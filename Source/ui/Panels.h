#pragma once

#include "RoomView.h"

namespace lt
{
    // What the panels need from the editor.
    struct EditorActions
    {
        std::function<void()> chooseModel, chooseFiles, openSettings, openHelp, useWithoutModel;
        std::function<void (Mode)> setMode;
        std::function<juce::Colour (const Corpus::Source&)> colourOf;
        std::function<int (const Corpus::Source&)> shapeOf;
        std::function<void()> coloursChanged;          // a sound's colour was changed
        std::function<juce::Component*()> calloutParent;
    };

    //==========================================================================
    class TopBar : public juce::Component
    {
    public:
        TopBar (SedimentsProcessor&, EditorActions&);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        LtButton modelStep { "Model" }, soundsStep { "Sounds" }, terrainStep { "Terrain" };
        LabeledSlider volume;
        LtButton help { "Help", "help" }, settings { "Settings", "settings" };
        int brandWidth = 250;
    };

    //==========================================================================
    class ModeBar : public juce::Component
    {
    public:
        explicit ModeBar (EditorActions&);
        void setMode (Mode m);
        void setMidiOn (bool on);
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        EditorActions& actions;
        juce::OwnedArray<LtButton> tabs;
        Mode mode = Mode::explore;
        bool midiOn = false;
    };

    //==========================================================================
    // "You're hearing: mostly forest_dawn 68% · stream_rocks 24%"
    class MixStrip : public juce::Component
    {
    public:
        MixStrip (SedimentsProcessor&, EditorActions&);
        void refresh (const std::shared_ptr<const Corpus>&);
        void paint (juce::Graphics&) override;
        std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;
        juce::String text;
    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        struct Part { int source; float share; };
        std::vector<Part> parts;
        std::shared_ptr<const Corpus> corpus;
        juce::Point<float> lastPos { -1.0f, -1.0f };
        float lastBlur = -1.0f;
        int lastDominant = -1;
    };

    //==========================================================================
    class LabeledGroup : public juce::Component
    {
    public:
        LabeledGroup (const juce::String& label, juce::Component& child);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::String label;
        juce::Component& child;
    };

    class GesturePanel : public juce::Component
    {
    public:
        GesturePanel (SedimentsProcessor&, MapView&);
        void refresh();
        void toggleRecord() { rec.triggerClick(); }
        int preferredHeight (int width);
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        SedimentsProcessor& proc;
        MapView& map;
        juce::Component buttonRow;
        LtButton rec { "Record gesture", "record", LtButton::Style::danger }, play { "Play", "play", LtButton::Style::primary },
                 clear { "Clear" };
        Segmented loop, dir;
        CheckRow follow;
        LabeledSlider speed;
        LabeledGroup gestureGroup, loopGroup, dirGroup;
        std::unique_ptr<juce::ButtonParameterAttachment> playAttach;
        std::unique_ptr<juce::ParameterAttachment> followAttach;
        std::vector<FlowItem> items();
    };

    //==========================================================================
    class KeysPanel : public juce::Component
    {
    public:
        KeysPanel (SedimentsProcessor&);
        void refresh();
        int preferredHeight (int width) const;
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        SedimentsProcessor& proc;
        juce::OwnedArray<LtButton> keys;     // 12, C..B
        CheckRow midiOn;
        LabeledSlider spray, attack, release;
        std::unique_ptr<juce::ButtonParameterAttachment> midiAttach;
        void playKey (int semitone, bool down);
    };

    //==========================================================================
    // A button that shows a choice parameter's value and opens a menu of its options.
    class ChoiceButton : public LtButton
    {
    public:
        ChoiceButton (juce::RangedAudioParameter& param, const juce::String& title);
        void update();
        std::function<juce::String (int, const juce::String&)> menuSection;   // optional header before option i
    private:
        juce::RangedAudioParameter& param;
        juce::String title;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int shown = -1;
    };

    // Space tab: output format and how grains are placed in the room.
    class SpacePanel : public juce::Component
    {
    public:
        explicit SpacePanel (SedimentsProcessor&);
        void refresh();
        int preferredHeight (int width);
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        SedimentsProcessor& proc;
        ChoiceButton output, place, height;
        Segmented ringStart;
        LabeledSlider width, scatter, rotate;
        LtButton evenly { "Spread evenly", "compass" };
        LabeledGroup outputGroup, placeGroup, heightGroup, ringGroup;
        juce::Component evenlyBox;
        juce::String note;
        bool warning = false;
        int noteHeight (int width) const;
        std::vector<FlowItem> items();
    };

    //==========================================================================
    class SoundRow : public juce::Component
    {
    public:
        SoundRow (SedimentsProcessor&, EditorActions&, const juce::String& path);
        void update (const Corpus* corpus, int frames, const juce::String& auditioning);
        void paint (juce::Graphics&) override;
        void resized() override;
        juce::String path, name, meta;
        juce::Colour colour;
        int shape = 0;
    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        LtButton swatch { "Change colour", {}, LtButton::Style::ghost }, preview { "Preview", "play", LtButton::Style::ghost },
                 remove { "Remove", "x", LtButton::Style::ghost };
        void openColourChooser();
    };

    class SoundsCard : public juce::Component
    {
    public:
        SoundsCard (SedimentsProcessor&, EditorActions&);
        void refresh (const std::shared_ptr<const Corpus>&);
        void paint (juce::Graphics&) override;
        void resized() override;
        void rebuildColours();
    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        LtButton add { "Add", "plus" };
        juce::Viewport viewport;
        juce::Component list;
        juce::OwnedArray<SoundRow> rows;
        int shownFiles = -1, shownCorpus = -1;
        juce::String summary, busyText;
        void layoutRows();
    };

    //==========================================================================
    class ShapeCard : public juce::Component
    {
    public:
        ShapeCard (SedimentsProcessor&);
        int preferredHeight() const;
        void refresh();                              // shows the controls that apply
        std::function<void()> onLayoutChange;
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        SedimentsProcessor& proc;
        LabeledSlider source, focus, terrain, glide, flow, grain, flowSpeed, dry;
        bool hasModel = true, usesOriginals = false;
        std::vector<LabeledSlider*> visibleSliders() const;
        LtButton more { "More", "chevronDown", LtButton::Style::ghost };
        bool expanded = false;
    };

    //==========================================================================
    class WelcomeView : public juce::Component
    {
    public:
        WelcomeView (SedimentsProcessor&, EditorActions&);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        bool dragHighlight = false;
    private:
        SedimentsProcessor& proc;
        EditorActions& actions;
        LtButton chooseModel { "Choose a model...", "folder", LtButton::Style::primary };
        LtButton noModel { "Play without a model", "wave" };
        juce::HyperlinkButton findModels { "Where to find free models", juce::URL ("https://huggingface.co/Intelligent-Instruments-Lab/rave-models") };
        LtButton addFiles { "Add files...", "plus" };
        juce::Rectangle<int> card, illus, step1, step2, step3, drop;
        bool hasModel = false, withoutModel = false, initialised = false;
        juce::String status;
    };

    //==========================================================================
    // How the map is laid out: similarity, or descriptor axes (pitch, brightness...).
    class LayoutChooser : public juce::Component
    {
    public:
        explicit LayoutChooser (SedimentsProcessor&);
        void paint (juce::Graphics&) override;
        void resized() override;
        static juce::String describe (int x, int y);
    private:
        SedimentsProcessor& proc;
        Segmented xAxis, yAxis;
        void apply();
    };

    //==========================================================================
    class SettingsPanel : public juce::Component
    {
    public:
        SettingsPanel (SedimentsProcessor&);
        std::function<void()> onClose;
        void syncFromSettings();
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
    private:
        struct Content : public juce::Component
        {
            Content();
            void paint (juce::Graphics&) override;
            int layout (int width);
            void resized() override { layout (getWidth()); }
            Segmented theme, palette, font, size, spacing;
            juce::OwnedArray<LtButton> accents;
            LtButton customAccent { "Custom...", "palette" }, fixContrast { "Make it readable" };
            CheckRow shapes, calm, announce;
            std::map<juce::String, int> headingY;
            int paletteY = 0, accentNoteY = 0, fontNoteY = 0, sizeNoteY = 0, keysY = 0;
            void apply();
        };
        SedimentsProcessor& proc;
        juce::Rectangle<int> sheet;
        LtButton close { "Close settings", "x" }, reset { "Reset to defaults" }, done { "Done", {}, LtButton::Style::primary };
        juce::Viewport viewport;
        Content content;
    };

    class HelpPanel : public juce::Component
    {
    public:
        HelpPanel();
        std::function<void()> onClose;
        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
    private:
        juce::Rectangle<int> card;
        LtButton close { "Close help", "x" }, done { "Got it", {}, LtButton::Style::primary };
    };

    //==========================================================================
    class Banner : public juce::Component
    {
    public:
        Banner();
        void show (const juce::String& message);
        juce::String message;
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        LtButton dismiss { "Dismiss message", "x", LtButton::Style::ghost };
    };
}
