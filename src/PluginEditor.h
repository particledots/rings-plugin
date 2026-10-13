#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

// Simple flat look for rotary knobs.
class RingsLookAndFeel : public juce::LookAndFeel_V4
{
public:
    RingsLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
};

class RingsEditor : public juce::AudioProcessorEditor
{
public:
    explicit RingsEditor (RingsAudioProcessor&);
    ~RingsEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    using SliderAtt = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAtt = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAtt  = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<SliderAtt> att;
    };

    struct Choice
    {
        juce::ComboBox box;
        juce::Label label;
        std::unique_ptr<ComboAtt> att;
    };

    void addKnob (Knob&, const juce::String& paramId, const juce::String& name);
    void addChoice (Choice&, const juce::String& paramId, const juce::String& name, const juce::StringArray& items);
    void updateVisibility();

    RingsAudioProcessor& proc;
    RingsLookAndFeel laf;

    Knob structure, brightness, damping, position, transpose, fm;
    Choice mode, model, synthFx, polyphony, exciter, strum;

    juce::ToggleButton strumBtn { "STRUM" }, monoOut { "MONO OUT" };
    std::unique_ptr<ButtonAtt> strumBtnAtt, monoOutAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingsEditor)
};
