// The Wrongs panel. Laid out at 680 x 596 units (the "gold sections" look of the Anamnesis plugin), no zoom menu.
//
// Drawing is split in two so the panel stays light to draw:
//   - the fixed artwork (frames, titles, labels, knob arcs, flow lines) is a child component drawn once into a stored
//     picture (redrawn only when the window opens or the display scale changes);
//   - 30 times a second only the live areas are redrawn: the six value texts under the knobs, and only when one changed.
// Nothing here touches the sound or the control list: the controls are the same parameters as before.

#pragma once

#include "PluginProcessor.h"
#include "Controls.h"

// A text on the panel and the room it has (panel units, x only), for the check program.
struct RingsFitItem
{
    juce::String what, text;
    juce::Font font;
    float x0, x1;
};

class RingsPanel : public juce::Component, private juce::Timer
{
public:
    explicit RingsPanel (RingsAudioProcessor&);
    ~RingsPanel() override;

    void paintOverChildren (juce::Graphics&) override;

    // For the check program
    juce::Component& getBackdrop() { return backdrop; }
    static juce::Array<juce::Rectangle<int>> liveAreas();     // the areas redrawn 30 times a second (panel units)
    static std::vector<RingsFitItem> fitItems();               // every text and the room it has
    static constexpr int kNumValues = 6;                       // Structure, Brightness, Damping, Position, Transpose, FM
    static const char* valueParamId (int i);
    const juce::String& getValueText (int i) const { return values[(size_t) i]; }
    void refreshTexts() { updateTexts(); }

private:
    struct Backdrop : public juce::Component
    {
        explicit Backdrop (RingsPanel& p) : owner (p) {}
        void paint (juce::Graphics& g) override { owner.paintBackdrop (g); }
        RingsPanel& owner;
    };
    struct RowTitle : public juce::Component
    {
        explicit RowTitle (juce::String t) : text (std::move (t)) { setInterceptsMouseClicks (false, false); }
        void paint (juce::Graphics&) override;
        juce::String text;
    };

    void timerCallback() override { updateTexts(); }
    juce::String valueTextFor (int i, double v) const;
    void updateTexts();
    void updateMode (float modeValue);
    void paintBackdrop (juce::Graphics&);

    RingsAudioProcessor& proc;
    AnmLookAndFeel lnf;
    Backdrop backdrop { *this };
    std::vector<std::unique_ptr<AnmKnob>> knobs;
    std::vector<std::unique_ptr<AnmSegments>> segments;
    RowTitle modelTitle { "MODEL" }, fxTitle { "SYNTH FX" };
    AnmSegments* modelList = nullptr;
    AnmSegments* fxList = nullptr;
    std::unique_ptr<juce::ParameterAttachment> modeAtt;
    std::array<juce::String, kNumValues> values;
};

class RingsEditor : public juce::AudioProcessorEditor
{
public:
    explicit RingsEditor (RingsAudioProcessor&);
    void paint (juce::Graphics&) override;
    RingsPanel& getPanel() { return panel; }

private:
    RingsPanel panel;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingsEditor)
};
