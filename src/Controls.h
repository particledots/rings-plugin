// The panel's own controls: knobs and switches (pill rows, lamp lists, option lamps, round buttons), and the look of the
// value bubble. Reused from the Anamnesis plugin's panel (0.3.1).

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Style.h"

class AnmLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AnmLookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Font getSliderPopupFont (juce::Slider&) override { return AnmStyle::font (13.0f); }
};

// A knob bound to one parameter: a black skirt with a cream cap and a dark pointer line. Its gold range arc is part of
// the panel's fixed artwork. The bounds are the arc's square (62 x 62 units), so the whole ring can be grabbed.
class AnmKnob : public juce::Slider
{
public:
    AnmKnob (juce::AudioProcessorValueTreeState&, const char* paramId);
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// A switch bound to one parameter, drawn in one of four ways:
//   Pill:   a row of choices in a gold pill (Mode, Polyphony); the chosen one is lit by a blue lamp.
//   List:   choices stacked with a lamp each (Exciter, Strum from, Pitch Snap, Model, Synth FX).
//   Option: one on/off lamp with its label (Mono Out).
//   Round:  one on/off round button with its label (Strum).
class AnmSegments : public juce::Component
{
public:
    enum class Kind { Pill, List, Option, Round };
    AnmSegments (juce::RangedAudioParameter&, juce::StringArray labels, Kind kind);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    int selectedIndex() const { return selected; }
    const juce::StringArray& getLabels() const { return labels; }
    Kind getKind() const { return kind; }
    bool isToggle() const { return kind == Kind::Option || kind == Kind::Round; }

    // The width this switch needs for its labels (Option and Round), in panel units.
    static float naturalWidth (const juce::String& label, Kind kind);

private:
    juce::RangedAudioParameter& param;
    juce::StringArray labels;
    Kind kind;
    int selected = 0;
    juce::ParameterAttachment attachment;
};

namespace AnmDraw
{
    // A round button (Strum) with its label, inside the area r.
    void roundButton (juce::Graphics&, juce::Rectangle<float> r, const juce::String& text, bool lit);
    inline constexpr float roundR = 13.0f;      // the button's radius
    inline constexpr float roundTextGap = 9.0f; // from the button's edge to its label
    inline constexpr float labelSize = 11.0f, labelSpacing = 1.6f;
}
