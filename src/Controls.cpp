#include "Controls.h"
#include <AnmFontData.h>

using namespace AnmStyle;

// ------------------------------------------------------------------------------------------------ fonts

namespace
{
    juce::Typeface::Ptr load (const char* data, int size)
    {
        return juce::Typeface::createSystemTypefaceFor (data, (size_t) size);
    }
    struct Faces
    {
        juce::Typeface::Ptr regular = load (AnmFontData::SpaceGroteskRegular_ttf, AnmFontData::SpaceGroteskRegular_ttfSize);
        juce::Typeface::Ptr medium  = load (AnmFontData::SpaceGroteskMedium_ttf, AnmFontData::SpaceGroteskMedium_ttfSize);
        juce::Typeface::Ptr title   = load (AnmFontData::MarcellusRegular_ttf, AnmFontData::MarcellusRegular_ttfSize);
    };
    Faces& faces() { static Faces f; return f; }

    juce::Font make (juce::Typeface::Ptr face, float size, float spacing)
    {
        // If a font could not be read for any reason, fall back to the system font rather than draw nothing.
        juce::FontOptions o = face != nullptr ? juce::FontOptions (face) : juce::FontOptions();
        juce::Font f (o.withPointHeight (size));
        if (spacing > 0.0f)
            f = f.withExtraKerningFactor (spacing / f.getHeight());
        return f;
    }
}

juce::Font AnmStyle::font (float size, bool medium, float spacing)
{
    return make (medium ? faces().medium : faces().regular, size, spacing);
}

juce::Font AnmStyle::titleFont()
{
    return make (faces().title, titleSize, 8.0f);
}

// ------------------------------------------------------------------------------------------------ look

AnmLookAndFeel::AnmLookAndFeel()
{
    setColour (juce::BubbleComponent::backgroundColourId, menu());
    setColour (juce::BubbleComponent::outlineColourId, gold());
    setColour (juce::TooltipWindow::textColourId, text());
    setColour (juce::Label::textColourId, text());
}

void AnmLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                       juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
    const float u = juce::jmin (r.getWidth(), r.getHeight()) / 62.0f;     // one panel unit
    const auto c = r.getCentre();
    auto disc = [&] (float radius, juce::Colour fill, juce::Colour line, float lw)
    {
        g.setColour (fill);
        g.fillEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (line);
        g.drawEllipse (c.x - radius, c.y - radius, radius * 2.0f, radius * 2.0f, lw);
    };
    disc (22.0f * u, skirt(), skirtLine(), 1.2f * u);
    disc (16.0f * u, cap(), capLine(), 0.8f * u);

    const float a = start + pos * (end - start);
    const float sx = std::sin (a), sy = -std::cos (a);
    juce::Path p;
    p.startNewSubPath (c.x + 5.0f * u * sx, c.y + 5.0f * u * sy);
    p.lineTo (c.x + 15.0f * u * sx, c.y + 15.0f * u * sy);
    g.setColour (pointer());
    g.strokePath (p, juce::PathStrokeType (2.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

// ------------------------------------------------------------------------------------------------ knob

AnmKnob::AnmKnob (juce::AudioProcessorValueTreeState& state, const char* paramId)
    : juce::Slider (juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox)
{
    setRotaryParameters (juce::degreesToRadians (-135.0f), juce::degreesToRadians (135.0f), true);
    setPopupDisplayEnabled (true, true, nullptr);
    setVelocityModeParameters (0.6, 1, 0.0, true, juce::ModifierKeys::shiftModifier);   // hold Shift for fine moves
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, paramId, *this);
    if (auto* p = state.getParameter (paramId))
        setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));   // double-click: back to default
}

// ------------------------------------------------------------------------------------------------ round button

void AnmDraw::roundButton (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& label, bool lit)
{
    const float cx = r.getX() + 20.0f, cy = r.getCentreY();
    if (lit)
    {
        g.setColour (lamp().withAlpha (0.18f));
        g.fillEllipse (cx - 16.0f, cy - 16.0f, 32.0f, 32.0f);
        g.setColour (lampFill());
        g.fillEllipse (cx - roundR, cy - roundR, roundR * 2.0f, roundR * 2.0f);
        g.setColour (lamp());
        g.drawEllipse (cx - roundR, cy - roundR, roundR * 2.0f, roundR * 2.0f, 1.4f);
    }
    else
    {
        g.setColour (skirt());
        g.fillEllipse (cx - roundR, cy - roundR, roundR * 2.0f, roundR * 2.0f);
        g.setColour (gold());
        g.drawEllipse (cx - roundR, cy - roundR, roundR * 2.0f, roundR * 2.0f, 1.1f);
    }
    const auto f = font (labelSize, false, labelSpacing);
    textIn (g, label, f, cx + roundR + roundTextGap, cy + 4.0f, text(), juce::Justification::centredLeft);
}

// ------------------------------------------------------------------------------------------------ switches

AnmSegments::AnmSegments (juce::RangedAudioParameter& p, juce::StringArray l, Kind k)
    : param (p), labels (std::move (l)), kind (k),
      attachment (p, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr)
{
    attachment.sendInitialUpdate();
}

float AnmSegments::naturalWidth (const juce::String& label, Kind k)
{
    const float t = textWidth (label, AnmDraw::labelSize, false, AnmDraw::labelSpacing);
    if (k == Kind::Round) return 20.0f + AnmDraw::roundR + AnmDraw::roundTextGap + t + 4.0f;
    return 17.0f + t + 4.0f;                                           // Option: lamp, gap, label
}

void AnmSegments::paint (juce::Graphics& g)
{
    const auto b = getLocalBounds().toFloat();
    const int n = labels.size();
    const auto f = font (11.0f, false, 1.2f);

    switch (kind)
    {
        case Kind::Round:
            AnmDraw::roundButton (g, b, labels[0], selected == 1);
            return;

        case Kind::Option:
        {
            const bool on = selected == 1;
            drawLamp (g, b.getX() + 8.0f, b.getCentreY(), on, on ? 3.2f : 3.0f);
            textIn (g, labels[0], font (AnmDraw::labelSize, false, 1.3f), b.getX() + 17.0f, b.getCentreY() + 4.0f,
                    on ? text() : soft(), juce::Justification::centredLeft);
            return;
        }

        case Kind::List:
        {
            const float rowH = b.getHeight() / (float) n;
            for (int i = 0; i < n; ++i)
            {
                const float cy = b.getY() + rowH * ((float) i + 0.5f);
                const bool on = selected == i;
                drawLamp (g, b.getX() + 9.0f, cy, on, on ? 3.2f : 3.0f);
                textIn (g, labels[i], f, b.getX() + 18.0f, cy + 4.0f, on ? text() : muted(), juce::Justification::centredLeft);
            }
            return;
        }

        case Kind::Pill:
        {
            const auto r = b.reduced (0.5f);
            g.setColour (gold());
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 0.7f);
            const float cw = r.getWidth() / (float) n;
            for (int i = 1; i < n; ++i)
                g.fillRect (r.getX() + cw * (float) i - 0.35f, r.getY() + 0.5f, 0.7f, r.getHeight() - 1.0f);
            for (int i = 0; i < n; ++i)
            {
                const bool on = selected == i;
                const float cx = r.getX() + cw * ((float) i + 0.5f), cy = r.getCentreY();
                textIn (g, labels[i], f, cx, cy + 4.0f, on ? text() : muted(), juce::Justification::centred);
                if (on)
                {
                    const float tw = widthOf (labels[i], f);
                    drawLamp (g, cx - tw * 0.5f - 7.0f, cy, true, 2.2f);
                }
            }
            return;
        }
    }
}

void AnmSegments::mouseDown (const juce::MouseEvent& e)
{
    if (! isEnabled()) return;
    if (isToggle())
    {
        attachment.setValueAsCompleteGesture (selected == 1 ? 0.0f : 1.0f);   // a click switches it on or off
        return;
    }
    const int n = labels.size();
    int i = 0;
    if (kind == Kind::List) i = juce::jlimit (0, n - 1, (int) ((float) e.y / (float) getHeight() * (float) n));
    else                    i = juce::jlimit (0, n - 1, (int) ((float) e.x / (float) getWidth() * (float) n));
    attachment.setValueAsCompleteGesture ((float) i);
}
