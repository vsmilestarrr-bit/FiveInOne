#pragma once
#include <JuceHeader.h>
#include <memory>
#include <vector>
#include "PluginProcessor.h"
#include "AnalyzerView.h"

class NeonLookAndFeel : public juce::LookAndFeel_V4
{
public:
    NeonLookAndFeel()
    {
        setColour (juce::ComboBox::backgroundColourId, juce::Colour (0xff1d2230));
        setColour (juce::ComboBox::textColourId, juce::Colours::white);
        setColour (juce::ComboBox::outlineColourId, juce::Colour (0xff2f3648));
        setColour (juce::ComboBox::arrowColourId, juce::Colour (0xff2de2e6));
        setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1d2230));
        setColour (juce::PopupMenu::textColourId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, juce::Colours::white);
        setColour (juce::ToggleButton::tickColourId, juce::Colour (0xff2de2e6));
        setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.75f));
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider& s) override
    {
        auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (5.f);
        const float r = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto c = bounds.getCentre();
        const float angle = startAngle + pos * (endAngle - startAngle);
        const auto accent = s.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, endAngle, true);
        g.setColour (juce::Colour (0xff2a2f3d));
        g.strokePath (track, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.f, startAngle, angle, true);
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (val, juce::PathStrokeType (8.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (accent);
        g.strokePath (val, juce::PathStrokeType (4.f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        const float br = r * 0.68f;
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b3144), c.x, c.y - br,
                                                 juce::Colour (0xff14171f), c.x, c.y + br, false));
        g.fillEllipse (c.x - br, c.y - br, 2.f * br, 2.f * br);

        juce::Path ptr;
        ptr.addRoundedRectangle (-1.5f, -br, 3.f, br * 0.5f, 1.5f);
        g.setColour (juce::Colours::white);
        g.fillPath (ptr, juce::AffineTransform::rotation (angle).translated (c.x, c.y));
    }
};

class FiveInOneEditor : public juce::AudioProcessorEditor
{
public:
    explicit FiveInOneEditor (FiveInOneProcessor&);
    ~FiveInOneEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        Knob (juce::AudioProcessorValueTreeState& s, const juce::String& id, const juce::String& name,
              const juce::String& suffix, juce::Colour colour, juce::Component& parent)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 60, 14);
            slider.setTextValueSuffix (suffix);
            slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
            slider.setColour (juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha (0.85f));
            slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            label.setText (name, juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (juce::Font (11.f));
            attachment = std::make_unique<SliderAtt> (s, id, slider);
            parent.addAndMakeVisible (slider);
            parent.addAndMakeVisible (label);
        }

        void place (juce::Rectangle<int> r)
        {
            label.setBounds (r.removeFromTop (14));
            slider.setBounds (r);
        }

        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> attachment;
    };

    Knob* addKnob (const juce::String& id, const juce::String& name, const juce::String& suffix, juce::Colour c);

    FiveInOneProcessor& proc;
    NeonLookAndFeel laf;
    AnalyzerView analyzer;

    std::vector<std::unique_ptr<Knob>> knobs;
    Knob *atSpeed = nullptr, *atAmt = nullptr, *xo1 = nullptr, *xo2 = nullptr,
         *outGain = nullptr, *limCeil = nullptr;
    Knob* comp[3][5] {};

    juce::ToggleButton atOn, eqOn, mbOn, limOn;
    juce::ComboBox keyBox, scaleBox;
    std::unique_ptr<ButtonAtt> atOnAtt, eqOnAtt, mbOnAtt, limOnAtt;
    std::unique_ptr<ComboAtt> keyAtt, scaleAtt;

    juce::Rectangle<int> panelAT, panelComp, panelOut, bandHeader[3];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FiveInOneEditor)
};
