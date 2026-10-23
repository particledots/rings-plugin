#include "PluginEditor.h"

namespace
{
    const juce::Colour kBg      (0xff1a1c20);
    const juce::Colour kPanel   (0xff23262c);
    const juce::Colour kTrack   (0xff3a3e47);
    const juce::Colour kAccent  (0xff6fc3b2);
    const juce::Colour kText    (0xffe8e6e1);
    const juce::Colour kDim     (0xff8d909a);
}

RingsLookAndFeel::RingsLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, kText);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, kDim);
    setColour (juce::ComboBox::backgroundColourId, kPanel);
    setColour (juce::ComboBox::textColourId, kText);
    setColour (juce::ComboBox::outlineColourId, kTrack);
    setColour (juce::ComboBox::arrowColourId, kAccent);
    setColour (juce::PopupMenu::backgroundColourId, kPanel);
    setColour (juce::PopupMenu::textColourId, kText);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, kAccent);
    setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::black);
}

void RingsLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                                         float a0, float a1, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float d = juce::jmin (r.getWidth(), r.getHeight());
    r = juce::Rectangle<float> (d, d).withCentre (r.getCentre());
    const float thick = juce::jmax (3.0f, d * 0.09f);
    const auto arcR = r.reduced (thick * 0.5f);

    juce::Path track;
    track.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0, a0, a1, true);
    g.setColour (kTrack);
    g.strokePath (track, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float angle = a0 + pos * (a1 - a0);
    const bool bipolar = s.getMinimum() < 0.0 && s.getMaximum() > 0.0;   // bipolar controls fill from the centre
    const float from = bipolar ? (a0 + a1) * 0.5f : a0;
    juce::Path val;
    val.addCentredArc (arcR.getCentreX(), arcR.getCentreY(), arcR.getWidth() * 0.5f, arcR.getHeight() * 0.5f, 0,
                       juce::jmin (from, angle), juce::jmax (from, angle), true);
    g.setColour (s.isEnabled() ? kAccent : kDim);
    g.strokePath (val, juce::PathStrokeType (thick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto c = r.getCentre();
    const float len = d * 0.5f - thick * 2.2f;
    juce::Line<float> line (c.getPointOnCircumference (len * 0.35f, angle),
                            c.getPointOnCircumference (len, angle));
    g.setColour (kText);
    g.drawLine (line, juce::jmax (2.0f, d * 0.045f));
}

void RingsLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool, bool)
{
    auto r = b.getLocalBounds().toFloat().reduced (2.0f);
    const bool on = b.getToggleState();
    g.setColour (on ? kAccent : kPanel);
    g.fillRoundedRectangle (r, 6.0f);
    g.setColour (on ? kAccent : kTrack);
    g.drawRoundedRectangle (r, 6.0f, 1.2f);
    g.setColour (on ? juce::Colours::black : kText);
    g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

RingsEditor::RingsEditor (RingsAudioProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&laf);

    addKnob (structure,  "structure",  "STRUCTURE");
    addKnob (brightness, "brightness", "BRIGHTNESS");
    addKnob (damping,    "damping",    "DAMPING");
    addKnob (position,   "position",   "POSITION");
    addKnob (transpose,  "transpose",  "TRANSPOSE");
    addKnob (fm,         "fm",         "FM");
    transpose.slider.setNumDecimalPlacesToDisplay (0);
    fm.slider.setNumDecimalPlacesToDisplay (1);

    addChoice (mode,      "mode",         "MODE",        { "Resonator", "String synth" });
    addChoice (model,     "model",        "MODEL",       RingsAudioProcessor::modelNames());
    addChoice (synthFx,   "synth_fx",     "SYNTH FX",    RingsAudioProcessor::synthFxNames());
    addChoice (polyphony, "polyphony",    "POLYPHONY",   { "1 voice", "2 voices", "4 voices" });
    addChoice (exciter,   "exciter",      "EXCITER",     { "Internal", "Audio input" });
    addChoice (strum,     "strum_source", "STRUM FROM",  { "MIDI notes", "Audio onset", "Manual button" });
    addChoice (pitchSnap, "pitch_snap",   "PITCH SNAP",  { "Off", "Steps", "Semitones" });
    pitchSnap.box.setTooltip ("What Transpose + FM settle on. Steps = octaves and fifths (0, 7, 12, 19, 24 ... up to 84, both directions); "
                              "Semitones = whole semitones. With Pitch Snap on, FM only matters when it tips the total over a step. "
                              "The Transpose readout shows the offset that plays.");
    // With Pitch Snap on, the Transpose readout shows the offset that actually plays (Transpose + FM, snapped).
    transpose.slider.textFromValueFunction = [this] (double v)
    {
        const int mode = (int) proc.apvts.getRawParameterValue ("pitch_snap")->load();
        if (mode == RingsEngine::SnapOff) return juce::String ((int) std::lround (v));
        const float total = (float) std::lround (v) + proc.apvts.getRawParameterValue ("fm")->load();
        const int st = (int) std::lround (RingsEngine::snapPitch (total, mode));
        return st > 0 ? "+" + juce::String (st) : juce::String (st);
    };
    snapReadoutAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("pitch_snap"),
                                                                  [this] (float) { transpose.slider.updateText(); });
    fmReadoutAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("fm"),
                                                                [this] (float) { transpose.slider.updateText(); });
    snapReadoutAtt->sendInitialUpdate();
    fmReadoutAtt->sendInitialUpdate();

    for (auto* b : { &strumBtn, &monoOut }) addAndMakeVisible (*b);
    strumBtnAtt = std::make_unique<ButtonAtt> (proc.apvts, "strum_btn", strumBtn);
    monoOutAtt  = std::make_unique<ButtonAtt> (proc.apvts, "mono_out", monoOut);

    mode.box.onChange = [this] { updateVisibility(); };
    updateVisibility();

    setSize (640, 470);
}

RingsEditor::~RingsEditor() { setLookAndFeel (nullptr); }

void RingsEditor::addKnob (Knob& k, const juce::String& id, const juce::String& name)
{
    k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k.slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    k.slider.setDoubleClickReturnValue (true, proc.apvts.getParameter (id)->convertFrom0to1 (proc.apvts.getParameter (id)->getDefaultValue()));
    k.slider.setPopupDisplayEnabled (false, false, nullptr);
    k.slider.setNumDecimalPlacesToDisplay (2);
    k.slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k.slider.setColour (juce::Slider::textBoxTextColourId, kText);
    k.label.setText (name, juce::dontSendNotification);
    k.label.setJustificationType (juce::Justification::centred);
    k.label.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)));
    addAndMakeVisible (k.slider);
    addAndMakeVisible (k.label);
    k.att = std::make_unique<SliderAtt> (proc.apvts, id, k.slider);
}

void RingsEditor::addChoice (Choice& c, const juce::String& id, const juce::String& name, const juce::StringArray& items)
{
    c.box.addItemList (items, 1);
    c.label.setText (name, juce::dontSendNotification);
    c.label.setJustificationType (juce::Justification::centredLeft);
    c.label.setFont (juce::Font (juce::FontOptions (11.0f, juce::Font::bold)));
    addAndMakeVisible (c.box);
    addAndMakeVisible (c.label);
    c.att = std::make_unique<ComboAtt> (proc.apvts, id, c.box);
}

void RingsEditor::updateVisibility()
{
    const bool synth = mode.box.getSelectedId() == 2;
    model.box.setVisible (! synth);   model.label.setVisible (! synth);
    synthFx.box.setVisible (synth);   synthFx.label.setVisible (synth);
}

void RingsEditor::paint (juce::Graphics& g)
{
    g.fillAll (kBg);
    g.setColour (kText);
    g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
    g.drawText ("UNOFFICIAL RINGS", 20, 10, 400, 26, juce::Justification::centredLeft);
    g.setColour (kDim);
    g.setFont (juce::Font (juce::FontOptions (11.0f)));
    g.drawText ("resonator - community port, not a Mutable Instruments product", 20, 34, 500, 16, juce::Justification::centredLeft);

    g.setColour (kPanel);
    g.fillRoundedRectangle (12.0f, 58.0f, (float) getWidth() - 24.0f, 150.0f, 10.0f);   // four main knobs
    g.fillRoundedRectangle (12.0f, 216.0f, (float) getWidth() - 24.0f, 120.0f, 10.0f);  // transpose / fm / choices
    g.fillRoundedRectangle (12.0f, 344.0f, (float) getWidth() - 24.0f, 120.0f, 10.0f);  // more choices + buttons
}

void RingsEditor::resized()
{
    const int W = getWidth();
    const int colW = (W - 24) / 4;

    auto place = [&] (Knob& k, juce::Rectangle<int> r)
    {
        k.label.setBounds (r.removeFromTop (16));
        k.slider.setBounds (r);
    };
    auto placeChoice = [&] (Choice& c, juce::Rectangle<int> r)
    {
        c.label.setBounds (r.removeFromTop (16));
        c.box.setBounds (r.removeFromTop (28));
    };

    Knob* mainRow[4] = { &structure, &brightness, &damping, &position };
    for (int i = 0; i < 4; ++i)
        place (*mainRow[i], juce::Rectangle<int> (12 + i * colW, 62, colW, 142).reduced (10, 0));

    // Row 2: transpose + fm knobs, then mode / model-or-fx
    place (transpose, juce::Rectangle<int> (12, 220, colW, 112).reduced (14, 0));
    place (fm,        juce::Rectangle<int> (12 + colW, 220, colW, 112).reduced (14, 0));
    placeChoice (mode, juce::Rectangle<int> (12 + 2 * colW + 8, 232, colW - 16, 48));
    placeChoice (model,   juce::Rectangle<int> (12 + 2 * colW + 8, 284, 2 * colW - 24, 48));   // model & fx share a slot
    placeChoice (synthFx, juce::Rectangle<int> (12 + 2 * colW + 8, 284, 2 * colW - 24, 48));
    // note: "mode" sits above the shared model/fx slot

    // Row 3: polyphony, exciter, strum source, buttons
    placeChoice (polyphony, juce::Rectangle<int> (24, 356, 130, 48));
    placeChoice (exciter,   juce::Rectangle<int> (166, 356, 130, 48));
    placeChoice (strum,     juce::Rectangle<int> (308, 356, 150, 48));
    placeChoice (pitchSnap, juce::Rectangle<int> (470, 356, 150, 48));
    strumBtn.setBounds (24, 416, 100, 36);
    monoOut.setBounds  (134, 416, 100, 36);
}
