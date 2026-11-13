#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"
#include "ui/Panels.h"

// The editor holds one "content" component laid out at 100 % size and scaled
// by the text-size setting, so everything (text, controls, spacing) grows
// together and nothing gets cut off.
class SedimentsEditor : public juce::AudioProcessorEditor,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private juce::ChangeListener
{
public:
    explicit SedimentsEditor (SedimentsProcessor&);
    ~SedimentsEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray&, int, int) override;
    void fileDragExit (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray& files, int, int) override;

    // Also used by the screenshot tool
    void setUiMode (int mode);
    void showSettings (bool show);
    void showHelp (bool show);
    void refreshNow() { timerCallback(); }
    void openLayoutChooser() { content->layoutButton.triggerClick(); }
    void highlightSound (const juce::String& path) { content->map.highlightSound (path); }   // as when pointed at

private:
    struct Content : public juce::Component
    {
        Content (SedimentsEditor& owner, SedimentsProcessor& p);
        void resized() override;
        void paint (juce::Graphics&) override;
        bool keyPressed (const juce::KeyPress&) override;

        SedimentsEditor& owner;
        SedimentsProcessor& proc;
        lt::EditorActions actions;
        lt::TopBar top;
        lt::ModeBar modes;
        lt::MapView map;
        lt::MixStrip mix;
        lt::GesturePanel gesture;
        lt::KeysPanel keys;
        lt::SpacePanel space;
        lt::RoomView room;
        bool roomShown = false;
        bool wantsRoom() const;
        juce::Viewport sideViewport;
        juce::Component side;
        lt::SoundsCard sounds;
        lt::ShapeCard shape;
        lt::WelcomeView welcome;
        lt::Banner banner;
        lt::LtButton layoutButton { "Map: similarity", "contour" };
        int shownLayout = -1;
        lt::SettingsPanel settingsPanel;
        lt::HelpPanel help;
        bool welcomeShown = true;
        lt::Mode mode = lt::Mode::explore;
        void layoutSide();
    };

    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void chooseModel();
    void chooseFiles();
    void applyScale();

    SedimentsProcessor& proc;
    juce::SharedResourcePointer<lt::UiSettings> settings;
    lt::Look look;
    std::unique_ptr<Content> content;
    std::unique_ptr<juce::TooltipWindow> tooltips;
    std::unique_ptr<juce::FileChooser> chooser;
    float scale = 1.0f;
    juce::String lastBannerStatus;
    bool applyingScale = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SedimentsEditor)
};
