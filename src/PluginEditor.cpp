#include "PluginEditor.h"

using namespace AnmStyle;
using Kind = AnmSegments::Kind;

namespace
{
    constexpr float kRing = 31.0f;                   // the gold range arc's radius

    // Knobs: centre (panel units), label. The order is the order of the value texts.
    struct KnobSpec { const char* id; const char* label; float x, y; };
    constexpr KnobSpec kKnobs[RingsPanel::kNumValues] = {
        { "structure",  "STRUCTURE",  96.0f,  258.0f }, { "brightness", "BRIGHTNESS", 259.0f, 258.0f },
        { "damping",    "DAMPING",    421.0f, 258.0f }, { "position",   "POSITION",   584.0f, 258.0f },
        { "transpose",  "TRANSPOSE",  424.0f, 102.0f }, { "fm",         "FM",         512.0f, 102.0f } };

    // Switches: parameter id, kind, bounds
    struct SegSpec { const char* id; Kind kind; juce::Rectangle<int> r; };

    // Section frames, each with its title set into the top line
    struct Frame { float x, y, w, h; const char* title; };
    constexpr Frame kFrames[] = { { 14.5f, 48.5f, 342.0f, 144.0f, "EXCITE" },
                                  { 376.5f, 48.5f, 289.0f, 144.0f, "PITCH" },
                                  { 14.5f, 204.5f, 651.0f, 299.5f, "VOICE" },
                                  { 14.5f, 516.0f, 651.0f, 44.0f, "OUTPUT" } };

    // Row titles (baseline y) and the room each has to the right
    struct RowSpec { const char* text; float x, y, room; };
    constexpr RowSpec kRows[] = { { "EXCITER", 26.0f, 74.0f, 118.0f }, { "STRUM FROM", 150.0f, 74.0f, 150.0f },
                                  { "PITCH SNAP", 562.0f, 74.0f, 100.0f }, { "MODE", 26.0f, 350.0f, 270.0f },
                                  { "POLYPHONY", 26.0f, 410.0f, 270.0f } };

    const juce::Font& capsFont()  { static const juce::Font f = font (11.0f, false, 2.0f); return f; }
    const juce::Font& rowFont()   { static const juce::Font f = font (11.0f, false, 1.5f); return f; }
    const juce::Font& listFont()  { static const juce::Font f = font (11.0f, false, 1.2f); return f; }
    const juce::Font& footFont()  { static const juce::Font f = font (11.0f, false, 1.5f); return f; }
    const char* footerText()      { return "RESONATOR \xc2\xb7 COMMUNITY PORT \xc2\xb7 NOT A MUTABLE INSTRUMENTS PRODUCT"; }

    // The labels shown for a switch: the parameter's own choices (the model and effect names as they are; the short
    // lists in capitals like the rest of the panel).
    juce::StringArray labelsFor (const juce::String& id)
    {
        if (id == "model")    return RingsAudioProcessor::modelNames();
        if (id == "synth_fx") return RingsAudioProcessor::synthFxNames();
        if (id == "strum_btn") return { "STRUM" };
        if (id == "mono_out")  return { "MONO OUT" };
        if (id == "exciter")      return { "INTERNAL", "AUDIO INPUT" };
        if (id == "strum_source") return { "MIDI NOTES", "AUDIO ONSET", "MANUAL BUTTON" };
        if (id == "pitch_snap")   return { "OFF", "STEPS", "SEMITONES" };
        if (id == "mode")         return { "RESONATOR", "STRING SYNTH" };
        if (id == "polyphony")    return { "1 VOICE", "2 VOICES", "4 VOICES" };
        jassertfalse;
        return {};
    }

    std::vector<SegSpec> segSpecs()
    {
        const int strumW = juce::roundToInt (AnmSegments::naturalWidth ("STRUM", Kind::Round));
        const int monoW  = juce::roundToInt (AnmSegments::naturalWidth ("MONO OUT", Kind::Option));
        return { { "exciter",      Kind::List,  { 26, 82, 118, 44 } },
                 { "strum_source", Kind::List,  { 150, 82, 150, 66 } },
                 { "strum_btn",    Kind::Round, { 26, 135, strumW, 34 } },
                 { "pitch_snap",   Kind::List,  { 562, 82, 100, 66 } },
                 { "mode",         Kind::Pill,  { 26, 358, 270, 22 } },
                 { "polyphony",    Kind::Pill,  { 26, 418, 270, 22 } },
                 { "model",        Kind::List,  { 340, 358, 300, 132 } },
                 { "synth_fx",     Kind::List,  { 340, 358, 300, 132 } },
                 { "mono_out",     Kind::Option, { 26, 526, monoW, 26 } } };
    }

    // A path with softly rounded corners through the given points.
    juce::Path run (std::initializer_list<juce::Point<float>> pts)
    {
        juce::Path p;
        std::vector<juce::Point<float>> v (pts);
        p.startNewSubPath (v[0]);
        for (size_t i = 1; i < v.size(); ++i) p.lineTo (v[i]);
        return p.createPathWithRoundedCorners (4.0f);
    }

    void stroke (juce::Graphics& g, const juce::Path& p, float w = 0.9f, bool dotted = false)
    {
        g.setColour (gold());
        if (! dotted)
        {
            g.strokePath (p, juce::PathStrokeType (w));
            return;
        }
        juce::Path d;
        const float dash[2] = { 2.0f, 2.5f };
        juce::PathStrokeType (w).createDashedStroke (d, p, dash, 2);
        g.fillPath (d);
    }

    void node (juce::Graphics& g, float x, float y)
    {
        g.setColour (panel());
        g.fillEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f);
        g.setColour (gold());
        g.drawEllipse (x - 2.6f, y - 2.6f, 5.2f, 5.2f, 0.9f);
    }
}

const char* RingsPanel::valueParamId (int i) { return kKnobs[i].id; }

void RingsPanel::RowTitle::paint (juce::Graphics& g)
{
    textIn (g, text, rowFont(), 0.0f, 12.0f, goldText(), juce::Justification::centredLeft);
}

// ================================================================================================ panel

RingsPanel::RingsPanel (RingsAudioProcessor& p) : proc (p)
{
    setLookAndFeel (&lnf);

    // The fixed artwork, drawn once into a stored picture
    backdrop.setBounds (0, 0, panelW, panelH);
    backdrop.setInterceptsMouseClicks (false, false);
    backdrop.setBufferedToImage (true);
    addAndMakeVisible (backdrop);

    for (int i = 0; i < kNumValues; ++i)
    {
        knobs.push_back (std::make_unique<AnmKnob> (proc.apvts, kKnobs[i].id));
        auto& k = *knobs.back();
        k.setBounds (juce::roundToInt (kKnobs[i].x - kRing), juce::roundToInt (kKnobs[i].y - kRing), 62, 62);
        k.setComponentID (kKnobs[i].id);
        k.textFromValueFunction = [this, i] (double v) { return valueTextFor (i, v); };   // also the drag bubble
        addAndMakeVisible (k);
    }

    for (const auto& s : segSpecs())
    {
        auto* prm = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (s.id));
        jassert (prm != nullptr);
        segments.push_back (std::make_unique<AnmSegments> (*prm, labelsFor (s.id), s.kind));
        auto& sg = *segments.back();
        sg.setBounds (s.r);
        sg.setComponentID (s.id);
        addAndMakeVisible (sg);
        if (juce::String (s.id) == "model")    modelList = &sg;
        if (juce::String (s.id) == "synth_fx") fxList = &sg;
    }

    // "Model" and "Synth FX" share one place: Mode decides which one is shown
    modelTitle.setBounds (340, 338, 200, 16);
    fxTitle.setBounds (340, 338, 200, 16);
    addAndMakeVisible (modelTitle);
    addAndMakeVisible (fxTitle);
    modeAtt = std::make_unique<juce::ParameterAttachment> (*proc.apvts.getParameter ("mode"), [this] (float v) { updateMode (v); });
    modeAtt->sendInitialUpdate();

    updateTexts();
    startTimerHz (30);
}

RingsPanel::~RingsPanel()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void RingsPanel::updateMode (float modeValue)
{
    const bool synth = modeValue > 0.5f;
    if (modelList != nullptr) modelList->setVisible (! synth);
    if (fxList != nullptr)    fxList->setVisible (synth);
    modelTitle.setVisible (! synth);
    fxTitle.setVisible (synth);
}

juce::String RingsPanel::valueTextFor (int i, double v) const
{
    auto raw = [this] (const char* id) { return proc.apvts.getRawParameterValue (id)->load(); };
    if (i < 4) return juce::String (v, 2);
    if (i == 4)
    {
        // With Pitch Snap on, the Transpose read-out shows the offset that actually plays (Transpose + FM, snapped).
        const int mode = (int) raw ("pitch_snap");
        if (mode == RingsEngine::SnapOff) return juce::String ((int) std::lround (v));
        const float total = (float) std::lround (v) + raw ("fm");
        const int st = (int) std::lround (RingsEngine::snapPitch (total, mode));
        return st > 0 ? "+" + juce::String (st) : juce::String (st);
    }
    if (std::abs (v) < 0.05) return "0.0";
    return v > 0.0 ? "+" + juce::String (v, 1) : juce::String (v, 1);
}

juce::Array<juce::Rectangle<int>> RingsPanel::liveAreas()
{
    juce::Array<juce::Rectangle<int>> a;
    for (const auto& k : kKnobs)
        a.add ({ juce::roundToInt (k.x) - 40, juce::roundToInt (k.y) + 56, 80, 17 });   // the value under each knob
    return a;
}

void RingsPanel::updateTexts()
{
    const auto areas = liveAreas();
    for (int i = 0; i < kNumValues; ++i)
    {
        const auto s = valueTextFor (i, (double) proc.apvts.getRawParameterValue (kKnobs[i].id)->load());
        if (s != values[(size_t) i])
        {
            values[(size_t) i] = s;
            repaint (areas[i]);
        }
    }
}

std::vector<RingsFitItem> RingsPanel::fitItems()
{
    std::vector<RingsFitItem> v;
    for (const auto& f : kFrames)
        v.push_back ({ juce::String ("frame title ") + f.title, f.title, capsFont(), f.x + 18.0f, f.x + f.w - 18.0f });
    for (int i = 0; i < kNumValues; ++i)
    {
        const bool voice = i < 4;
        const float half = voice ? 78.0f : 40.0f;      // the room between neighbouring knobs
        v.push_back ({ juce::String ("knob label ") + kKnobs[i].label, kKnobs[i].label, capsFont(), kKnobs[i].x - half, kKnobs[i].x + half });
        v.push_back ({ juce::String ("value room ") + kKnobs[i].id, "-36.0", font (11.0f), kKnobs[i].x - 40.0f, kKnobs[i].x + 40.0f });
    }
    for (const auto& r : kRows)
        v.push_back ({ juce::String ("row title ") + r.text, r.text, rowFont(), r.x, r.x + r.room });
    v.push_back ({ "row title MODEL", "MODEL", rowFont(), 340.0f, 640.0f });
    v.push_back ({ "row title SYNTH FX", "SYNTH FX", rowFont(), 340.0f, 640.0f });
    for (const auto& s : segSpecs())
    {
        const auto labels = labelsFor (s.id);
        const float x = (float) s.r.getX(), w = (float) s.r.getWidth();
        if (s.kind == Kind::List)
        {
            // the room is the list's width, and also the frame's inner edge (12 units in)
            float frameRight = 356.5f;
            for (const auto& f : kFrames)
                if (x + 1.0f >= f.x && x + 1.0f <= f.x + f.w && (float) s.r.getY() + 1.0f >= f.y && (float) s.r.getY() + 1.0f <= f.y + f.h)
                    frameRight = f.x + f.w;
            for (const auto& l : labels)
                v.push_back ({ juce::String ("list ") + s.id + ": " + l, l, listFont(), x + 18.0f, juce::jmin (x + w, frameRight - 8.0f) });
        }
        else if (s.kind == Kind::Pill)
        {
            const float cell = (w - 1.0f) / (float) labels.size();
            for (int i = 0; i < labels.size(); ++i)
            {
                const float cx = x + 0.5f + cell * ((float) i + 0.5f);
                v.push_back ({ juce::String ("pill ") + s.id + ": " + labels[i], labels[i], listFont(), cx - cell * 0.5f + 14.0f, cx + cell * 0.5f - 4.0f });
            }
        }
        else if (s.kind == Kind::Round)
            v.push_back ({ juce::String ("button ") + s.id, labels[0], font (AnmDraw::labelSize, false, AnmDraw::labelSpacing),
                           x + 20.0f + AnmDraw::roundR + AnmDraw::roundTextGap, x + w });
        else
            v.push_back ({ juce::String ("option ") + s.id, labels[0], font (AnmDraw::labelSize, false, 1.3f), x + 17.0f, x + w });
    }
    v.push_back ({ "footer", footerText(), footFont(), 20.0f, 660.0f });
    return v;
}

// ------------------------------------------------------------------------------------------------ fixed artwork

void RingsPanel::paintBackdrop (juce::Graphics& g)
{
    g.setColour (panel());
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 12.0f);

    // Header: the title between two ornament lines
    textIn (g, titleString(), titleFont(), 340.0f, titleBaseline, goldText(), juce::Justification::centred);
    g.setColour (gold());
    g.fillRect (132.0f, 24.6f, ornamentLeftEnd - 132.0f, 0.8f);
    g.fillRect (ornamentRightStart, 24.6f, 548.0f - ornamentRightStart, 0.8f);
    g.fillEllipse (130.2f, 23.2f, 3.6f, 3.6f);
    g.fillEllipse (546.2f, 23.2f, 3.6f, 3.6f);
    textAt (g, "PARTICLEDOTS", 22.0f, 29.0f, muted(), 11.0f, juce::Justification::centredLeft, false, 2.5f);

    // Section frames, each with its title set into the top line
    for (const auto& f : kFrames)
    {
        g.setColour (gold());
        g.drawRoundedRectangle (f.x, f.y, f.w, f.h, 10.0f, 0.9f);
        const juce::String t (f.title);
        const float tw = widthOf (t, capsFont());
        const float tx = f.x + 18.0f;
        g.setColour (panel());
        g.fillRect (tx - 6.0f, f.y - 5.0f, tw + 12.0f, 10.0f);
        textIn (g, t, capsFont(), tx, f.y + 4.0f, goldText(), juce::Justification::centredLeft);
    }

    // Flow lines (each one true: see the README). Where a line ends on a control it ends in a node.
    //  - Transpose and FM both run into Pitch Snap (it acts on their sum)
    stroke (g, run ({ { 424.0f, 71.0f }, { 424.0f, 62.0f }, { 553.0f, 62.0f }, { 553.0f, 115.0f }, { 557.0f, 115.0f } }));
    stroke (g, run ({ { 512.0f, 71.0f }, { 512.0f, 62.0f } }));
    node (g, 512.0f, 62.0f);
    node (g, 557.0f, 115.0f);
    //  - the Strum button feeds "Manual button" (dotted: it only counts when Strum from is Manual button)
    stroke (g, run ({ { 46.0f, 165.0f }, { 46.0f, 172.0f }, { 138.0f, 172.0f }, { 138.0f, 137.0f }, { 146.0f, 137.0f } }), 0.9f, true);
    node (g, 148.0f, 137.0f);
    //  - Excite and Pitch feed the Voice, the Voice feeds the Output
    for (float x : { 185.0f, 521.0f })
    {
        stroke (g, run ({ { x, 192.5f }, { x, 204.5f } }));
        node (g, x, 192.5f);
        node (g, x, 204.5f);
    }
    stroke (g, run ({ { 340.0f, 504.0f }, { 340.0f, 516.0f } }));
    node (g, 340.0f, 504.0f);
    node (g, 340.0f, 516.0f);

    // The knobs' range arcs (7:30 round to 4:30) with a dot at each end, and their labels
    for (const auto& k : kKnobs)
    {
        juce::Path arc;
        arc.addCentredArc (k.x, k.y, kRing, kRing, 0.0f, juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
        g.setColour (gold());
        g.strokePath (arc, juce::PathStrokeType (0.9f));
        const float d = kRing * 0.70710678f;
        g.fillEllipse (k.x - d - 1.8f, k.y + d - 1.8f, 3.6f, 3.6f);
        g.fillEllipse (k.x + d - 1.8f, k.y + d - 1.8f, 3.6f, 3.6f);
        textIn (g, k.label, capsFont(), k.x, k.y + 52.0f, text(), juce::Justification::centred);
    }

    // Row titles
    for (const auto& r : kRows)
        textIn (g, r.text, rowFont(), r.x, r.y, goldText(), juce::Justification::centredLeft);

    textIn (g, juce::String::fromUTF8 (footerText()), footFont(), 340.0f, 582.0f, footer(), juce::Justification::centred);
}

// ------------------------------------------------------------------------------------------------ live drawing

void RingsPanel::paintOverChildren (juce::Graphics& g)
{
    // Values under the knobs
    for (int i = 0; i < kNumValues; ++i)
        textAt (g, values[(size_t) i], kKnobs[i].x, kKnobs[i].y + 67.0f, muted(), 11.0f);
}

// ================================================================================================ editor

RingsEditor::RingsEditor (RingsAudioProcessor& p) : juce::AudioProcessorEditor (p), panel (p)
{
    addAndMakeVisible (panel);
    panel.setBounds (0, 0, panelW, panelH);
    setResizable (false, false);
    setSize (panelW, panelH);
}

void RingsEditor::paint (juce::Graphics& g)
{
    g.fillAll (window());
}
