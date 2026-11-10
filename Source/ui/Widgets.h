#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "UiCore.h"

namespace lt
{
    void paintCard (juce::Graphics&, juce::Rectangle<float>, float radius = 16.0f);
    void drawKeycap (juce::Graphics&, juce::Rectangle<float>, const juce::String& text);
    float keycapWidth (const juce::String& text);

    //==========================================================================
    // A labelled slider: name and value on top, the slider, then what each end
    // means ("Sharp" ... "Blended"). Fully keyboard and screen-reader usable.
    class LabeledSlider : public juce::Component
    {
    public:
        enum class Layout { stacked, inlineRow };

        LabeledSlider (juce::RangedAudioParameter* param, const juce::String& label,
                       const juce::String& lowEnd, const juce::String& highEnd,
                       std::function<juce::String (float)> format, Layout layout = Layout::stacked);

        juce::Slider slider;
        int preferredHeight() const { return layout == Layout::inlineRow ? 44 : (lowEnd.isEmpty() ? 50 : 66); }
        juce::String valueText() const { return format ? format ((float) slider.getValue()) : juce::String (slider.getValue(), 2); }

        void paint (juce::Graphics&) override;
        void resized() override;
        void setValueWidth (int w) { valueWidth = w; resized(); }

    private:
        juce::String label, lowEnd, highEnd;
        std::function<juce::String (float)> format;
        Layout layout;
        int valueWidth = 70;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
    };

    //==========================================================================
    // A row of mutually exclusive choices (radio buttons that look like pills).
    class Segmented : public juce::Component
    {
    public:
        Segmented (const juce::StringArray& options, const juce::String& title);

        std::function<void (int)> onChange;
        void setSelected (int index, bool notify = false);
        int getSelected() const { return selected; }
        void attachToChoice (juce::RangedAudioParameter& param);
        int preferredWidth() const;
        void setOptionFont (int index, const juce::String& fontId);

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        juce::OwnedArray<LtButton> buttons;
        int selected = 0;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::RangedAudioParameter* attachedParam = nullptr;
    };

    //==========================================================================
    // A checkbox with a bold title and a one-line explanation.
    class CheckRow : public juce::ToggleButton
    {
    public:
        CheckRow (const juce::String& title, const juce::String& description);
        void paintButton (juce::Graphics&, bool over, bool down) override;
        juce::String description;
        bool boxed = true;
        int preferredHeight (int width) const;
    };

    //==========================================================================
    // Lays out components left to right, wrapping to new rows. Returns the height.
    struct FlowItem { juce::Component* c; int width; int height; int gapAfter = 12; };
    int flowLayout (std::vector<FlowItem> items, juce::Rectangle<int> area, int rowGap = 12, bool apply = true);
}
