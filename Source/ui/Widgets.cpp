#include "Widgets.h"

namespace lt
{
void paintCard (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    auto& t = ui().theme();
    g.setColour (t.panel);
    g.fillRoundedRectangle (r, radius);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

float keycapWidth (const juce::String& text)
{
    return juce::GlyphArrangement::getStringWidth (ui().font (13.0f, true), text) + 14.0f;
}

void drawKeycap (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text)
{
    auto& t = ui().theme();
    g.setColour (t.raised);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
    g.setColour (t.fg);
    g.setFont (ui().font (13.0f, true));
    g.drawText (text, r, juce::Justification::centred);
}

//==============================================================================
LabeledSlider::LabeledSlider (juce::RangedAudioParameter* param, const juce::String& l,
                              const juce::String& lo, const juce::String& hi,
                              std::function<juce::String (float)> fmt, Layout lay)
    : label (l), lowEnd (lo), highEnd (hi), format (std::move (fmt)), layout (lay)
{
    slider.setSliderStyle (juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setTitle (label);
    if (lowEnd.isNotEmpty())
        slider.setDescription (lowEnd + " to " + highEnd);
    slider.setWantsKeyboardFocus (true);
    addAndMakeVisible (slider);

    if (param != nullptr)
    {
        attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider);
        slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
    }
    if (format)
    {
        auto f = format;
        slider.textFromValueFunction = [f] (double v) { return f ((float) v); };
    }
    slider.onValueChange = [this] { repaint(); };
    setTitle (label);
}

void LabeledSlider::paint (juce::Graphics& g)
{
    auto& s = ui();
    auto& t = s.theme();
    auto b = getLocalBounds();
    const float alpha = isEnabled() ? 1.0f : 0.5f;
    g.setOpacity (alpha);

    if (layout == Layout::inlineRow)
    {
        g.setColour (t.muted);
        g.setFont (s.font (13.0f));
        g.drawText (label, b.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (s.font (13.0f), label)) + 4), juce::Justification::centredLeft);
        g.setColour (t.fg);
        g.setFont (s.font (14.0f, true));
        g.drawText (valueText(), b.removeFromRight (valueWidth), juce::Justification::centredRight);
        return;
    }

    auto top = b.removeFromTop (22);
    g.setColour (t.fg);
    g.setFont (s.font (15.0f, true));
    g.drawText (label, top, juce::Justification::centredLeft);
    g.setColour (t.muted);
    g.setFont (s.font (14.0f));
    g.drawText (valueText(), top, juce::Justification::centredRight);

    if (lowEnd.isNotEmpty())
    {
        auto bottom = b.removeFromBottom (16);
        g.setFont (s.font (12.0f));
        g.drawText (lowEnd, bottom, juce::Justification::centredLeft);
        g.drawText (highEnd, bottom, juce::Justification::centredRight);
    }
}

void LabeledSlider::resized()
{
    auto b = getLocalBounds();
    if (layout == Layout::inlineRow)
    {
        auto& s = ui();
        b.removeFromLeft (juce::roundToInt (juce::GlyphArrangement::getStringWidth (s.font (13.0f), label)) + 10);
        b.removeFromRight (valueWidth + 6);
        slider.setBounds (b);
        return;
    }
    b.removeFromTop (22);
    if (lowEnd.isNotEmpty())
        b.removeFromBottom (16);
    slider.setBounds (b);
}

//==============================================================================
static int nextRadioGroup()
{
    static int id = 4200;
    return ++id;
}

Segmented::Segmented (const juce::StringArray& options, const juce::String& title)
{
    setTitle (title);
    const int group = nextRadioGroup();
    for (int i = 0; i < options.size(); ++i)
    {
        auto* b = buttons.add (new LtButton (options[i], {}, LtButton::Style::segment));
        b->setRadioGroupId (group);
        b->setClickingTogglesState (true);
        b->setTitle (title + ": " + options[i]);
        b->onClick = [this, i]
        {
            if (! buttons[i]->getToggleState())
                return;
            selected = i;
            if (attachment != nullptr)
                attachment->setValueAsCompleteGesture ((float) i);
            if (onChange)
                onChange (i);
        };
        addAndMakeVisible (b);
    }
    if (! buttons.isEmpty())
        buttons[0]->setToggleState (true, juce::dontSendNotification);
}

void Segmented::setSelected (int index, bool notify)
{
    if (! juce::isPositiveAndBelow (index, buttons.size()))
        return;
    selected = index;
    buttons[index]->setToggleState (true, juce::dontSendNotification);
    if (notify && onChange)
        onChange (index);
}

void Segmented::attachToChoice (juce::RangedAudioParameter& param)
{
    attachedParam = &param;
    attachment = std::make_unique<juce::ParameterAttachment> (param, [this] (float v)
    {
        setSelected (juce::roundToInt (v));
        if (onChange) onChange (selected);
    });
    attachment->sendInitialUpdate();
}

void Segmented::setOptionFont (int index, const juce::String& fontId)
{
    if (auto* b = buttons[index])
        b->setFontOverride (fontId);
}

int Segmented::preferredWidth() const
{
    int w = 6;
    for (auto* b : buttons)
        w += b->idealWidth (40.0f) + 2;
    return w;
}

void Segmented::paint (juce::Graphics& g)
{
    auto& t = ui().theme();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.raised);
    g.fillRoundedRectangle (r, 14.0f);
    g.setColour (t.line);
    g.drawRoundedRectangle (r.reduced (0.5f), 14.0f, 1.0f);
}

void Segmented::resized()
{
    auto b = getLocalBounds().reduced (3);
    const int total = juce::jmax (1, preferredWidth() - 6);
    const int avail = b.getWidth();
    int x = b.getX();
    for (int i = 0; i < buttons.size(); ++i)
    {
        const int ideal = buttons[i]->idealWidth (40.0f) + 2;
        const int w = i == buttons.size() - 1 ? b.getRight() - x : juce::roundToInt (ideal * (float) avail / (float) total);
        buttons[i]->setBounds (x, b.getY(), w - 2, b.getHeight());
        x += w;
    }
}

//==============================================================================
CheckRow::CheckRow (const juce::String& title, const juce::String& desc)
    : juce::ToggleButton (title), description (desc)
{
    setTitle (title);
    setDescription (desc);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

int CheckRow::preferredHeight (int width) const
{
    if (description.isEmpty())
        return 44;
    auto f = ui().font (14.0f);
    const float w = (float) juce::jmax (50, width - 58);
    const int lines = juce::jmax (1, (int) std::ceil (juce::GlyphArrangement::getStringWidth (f, description) / w));
    return 30 + lines * 19 + (boxed ? 10 : 0);
}

void CheckRow::paintButton (juce::Graphics& g, bool over, bool)
{
    auto& s = ui();
    auto& t = s.theme();
    auto b = getLocalBounds().toFloat();
    g.setOpacity (isEnabled() ? 1.0f : 0.45f);
    if (boxed)
    {
        g.setColour (over ? (t.dark ? t.raised.brighter (0.08f) : t.raised.darker (0.03f)) : t.raised);
        g.fillRoundedRectangle (b, 12.0f);
    }
    auto inner = b.reduced (boxed ? 12.0f : 0.0f, boxed ? 10.0f : 0.0f);
    auto box = juce::Rectangle<float> (inner.getX(), inner.getY() + (description.isEmpty() ? (inner.getHeight() - 22.0f) * 0.5f : 2.0f), 22.0f, 22.0f);
    if (getToggleState())
    {
        g.setColour (s.accent());
        g.fillRoundedRectangle (box, 6.0f);
        Icons::draw (g, "check", box.reduced (4.0f), s.onAccent(), 3.2f);
    }
    else
    {
        g.setColour (t.panel);
        g.fillRoundedRectangle (box, 6.0f);
        g.setColour (t.muted);
        g.drawRoundedRectangle (box.reduced (0.75f), 6.0f, 1.5f);
    }
    auto text = inner.withTrimmedLeft (34.0f);
    g.setColour (t.fg);
    g.setFont (s.font (15.0f, true));
    if (description.isEmpty())
    {
        g.drawText (getButtonText(), text, juce::Justification::centredLeft, true);
    }
    else
    {
        g.drawText (getButtonText(), text.removeFromTop (24.0f), juce::Justification::centredLeft, true);
        g.setColour (t.muted);
        g.setFont (s.font (14.0f));
        g.drawFittedText (description, text.toNearestInt(), juce::Justification::topLeft, 4, 1.0f);
    }
    if (hasKeyboardFocus (false))
        drawFocusRing (g, boxed ? b : box, boxed ? 12.0f : 6.0f, s);
}

//==============================================================================
int flowLayout (std::vector<FlowItem> items, juce::Rectangle<int> area, int rowGap, bool apply)
{
    int x = area.getX(), y = area.getY(), rowH = 0;
    for (auto& it : items)
    {
        if (x > area.getX() && x + it.width > area.getRight())
        {
            x = area.getX();
            y += rowH + rowGap;
            rowH = 0;
        }
        if (apply && it.c != nullptr)
            it.c->setBounds (x, y, juce::jmin (it.width, area.getWidth()), it.height);
        x += it.width + it.gapAfter;
        rowH = juce::jmax (rowH, it.height);
    }
    return y + rowH - area.getY();
}
}
