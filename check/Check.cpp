// RingsCheck: runs the plugin layer without a host and without Logic.
//
//   1. The list of controls is exactly the one of 0.2.0 (same ids, order, versions, ranges, defaults, menu lists), so
//      saved projects and automation keep working.
//   2. Saved projects: a state saved by 0.2.0 loads with every control as saved (also into an open panel), a state from
//      before Pitch Snap existed loads, and this version's state comes back exactly.
//   3. The panel: opens at 680 x 596; "WRONGS" fits its title area (measured, and checked in the drawn pixels at 100%
//      and 200%); every control of the plugin is on the panel and live; every text fits its place (measured, and nothing
//      is drawn outside the frames or cut off); clicks reach the controls; Model / Synth FX follow Mode; the value texts.
//   4. Drawing cost: the fixed artwork is a stored picture, and the 30-times-a-second update is much lighter than drawing
//      the whole panel every time.
//
// The build workflow runs it on every push and fails if any check fails.
// "--snapshot <folder>" writes pictures of the panel. "--write-golden" writes the golden files (from the 0.2.0 code).
// Compiled with RINGS_NO_PANEL it only does 1 and 2 (this is how the golden files were made from the old panel's code).

#include "PluginProcessor.h"
#ifndef RINGS_NO_PANEL
 #include "PluginEditor.h"
#endif

#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>

static int checks = 0, failures = 0;

#define CHECK(cond, ...)                                                   \
    do {                                                                   \
        ++checks;                                                          \
        if (! (cond)) {                                                    \
            std::printf ("FAIL line %d: %s   ", __LINE__, #cond);          \
            std::printf (__VA_ARGS__);                                     \
            std::printf ("\n");                                            \
            ++failures;                                                    \
        }                                                                  \
    } while (0)

static std::vector<juce::RangedAudioParameter*> allParams (RingsAudioProcessor& proc)
{
    std::vector<juce::RangedAudioParameter*> v;
    for (auto* base : proc.getParameters())
        if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (base))
            v.push_back (p);
    return v;
}
static juce::RangedAudioParameter* param (RingsAudioProcessor& proc, const char* id)
{
    auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.apvts.getParameter (id));
    jassert (p != nullptr);
    return p;
}
static float realValue (juce::RangedAudioParameter* p) { return p->convertFrom0to1 (p->getValue()); }
static void setReal (juce::RangedAudioParameter* p, float v) { p->setValueNotifyingHost (p->convertTo0to1 (v)); }

// ------------------------------------------------------------------------------------------------ 1. the control list

static juce::StringArray dumpLayout (RingsAudioProcessor& proc)
{
    juce::StringArray out;
    for (auto* p : allParams (proc))
    {
        const char* type = dynamic_cast<juce::AudioParameterChoice*> (p) ? "choice"
                         : dynamic_cast<juce::AudioParameterBool*> (p)   ? "bool"
                         : dynamic_cast<juce::AudioParameterInt*> (p)    ? "int" : "float";
        const auto range = p->getNormalisableRange();
        juce::String line;
        line << p->paramID << "|" << p->getName (100) << "|v" << p->getVersionHint() << "|" << type
             << "|" << juce::String::formatted ("%.6g %.6g %.6g %.6g", range.start + 0.0f, range.end + 0.0f, range.interval + 0.0f, range.skew + 0.0f)
             << "|default " << juce::String::formatted ("%.6g", p->convertFrom0to1 (p->getDefaultValue()) + 0.0f);
        if (auto* c = dynamic_cast<juce::AudioParameterChoice*> (p))
            line << "|" << c->choices.joinIntoString (";");
        out.add (line);
    }
    return out;
}

static juce::File goldenLayoutFile (const juce::File& dir) { return dir.getChildFile ("layout-0.2.0.txt"); }
static juce::File goldenStateFile (const juce::File& dir)  { return dir.getChildFile ("state-0.2.0.xml"); }

// A state with every control set away from its default (so loading it shows every control)
static void setEverything (RingsAudioProcessor& p)
{
    setReal (param (p, "structure"), 0.71f);  setReal (param (p, "brightness"), 0.33f);
    setReal (param (p, "damping"), 0.62f);    setReal (param (p, "position"), 0.18f);
    setReal (param (p, "transpose"), -7.0f);  setReal (param (p, "pitch_snap"), 2.0f);
    setReal (param (p, "fm"), 13.37f);        setReal (param (p, "polyphony"), 2.0f);
    setReal (param (p, "mode"), 1.0f);        setReal (param (p, "model"), 3.0f);
    setReal (param (p, "synth_fx"), 2.0f);    setReal (param (p, "exciter"), 1.0f);
    setReal (param (p, "strum_source"), 2.0f); setReal (param (p, "strum_btn"), 1.0f);
    setReal (param (p, "mono_out"), 1.0f);
}

static void writeGoldenFiles (const juce::File& dir)
{
    dir.createDirectory();
    RingsAudioProcessor proc;
    goldenLayoutFile (dir).replaceWithText (dumpLayout (proc).joinIntoString ("\n") + "\n");
    setEverything (proc);
    juce::MemoryBlock mb;
    proc.getStateInformation (mb);
    if (auto xml = proc.getXmlFromBinary (mb.getData(), (int) mb.getSize()))
        goldenStateFile (dir).replaceWithText (xml->toString());
    std::printf ("golden files written to %s\n", dir.getFullPathName().toRawUTF8());
}

static void testControlList (const juce::File& goldenDir)
{
    RingsAudioProcessor proc;
    const auto now = dumpLayout (proc);
    juce::StringArray golden;
    golden.addLines (goldenLayoutFile (goldenDir).loadFileAsString());
    golden.removeEmptyStrings();
    CHECK (golden.size() == 15, "the golden control list has %d controls (15 expected)", golden.size());
    CHECK (now.size() == golden.size(), "the plugin has %d controls, the 0.2.0 list has %d", now.size(), golden.size());
    int same = 0;
    for (int i = 0; i < juce::jmin (now.size(), golden.size()); ++i)
    {
        if (now[i] == golden[i]) ++same;
        else { std::printf ("  control %d differs:\n    now:    %s\n    golden: %s\n", i, now[i].toRawUTF8(), golden[i].toRawUTF8());
               CHECK (false, "control %d now [%s] golden [%s]", i, now[i].toRawUTF8(), golden[i].toRawUTF8()); }
    }
    CHECK (same == golden.size(), "every control is exactly as in 0.2.0 (same id, order, version, range, default, menu); %d of %d", same, golden.size());
    std::printf ("control list: %d of %d controls identical to 0.2.0\n", same, golden.size());
}

// ------------------------------------------------------------------------------------------------ 2. saved projects

static void loadXml (RingsAudioProcessor& proc, const juce::XmlElement& xml)
{
    juce::MemoryBlock mb;
    juce::AudioProcessor::copyXmlToBinary (xml, mb);
    proc.setStateInformation (mb.getData(), (int) mb.getSize());
}

static void testSavedProjects (const juce::File& goldenDir)
{
    auto xml = juce::XmlDocument::parse (goldenStateFile (goldenDir));
    CHECK (xml != nullptr, "the golden 0.2.0 state can be read");
    if (xml == nullptr) return;
    {
        RingsAudioProcessor proc;
        loadXml (proc, *xml);
        int n = 0, ok = 0;
        for (auto* c : xml->getChildIterator())
        {
            if (! c->hasTagName ("PARAM")) continue;
            ++n;
            const auto id = c->getStringAttribute ("id");
            const float expect = (float) c->getDoubleAttribute ("value");
            const float got = proc.apvts.getRawParameterValue (id)->load();
            if (std::fabs (got - expect) < 1e-4f) ++ok;
            else std::printf ("  %s: saved %.5f, loaded %.5f\n", id.toRawUTF8(), expect, got);
        }
        CHECK (n == 15 && ok == n, "a project saved by 0.2.0 loads with every control as saved (%d of %d)", ok, n);
        std::printf ("saved project 0.2.0: %d of %d controls loaded as saved\n", ok, n);
    }
    {
        // a project from before Pitch Snap existed (0.1.0): no pitch_snap entry; everything else loads, Pitch Snap is Off
        auto old = std::make_unique<juce::XmlElement> (*xml);
        for (auto* c : old->getChildIterator())
            if (c->getStringAttribute ("id") == "pitch_snap") { old->removeChildElement (c, true); break; }
        RingsAudioProcessor proc;
        loadXml (proc, *old);
        CHECK (proc.apvts.getRawParameterValue ("pitch_snap")->load() == 0.0f, "a project from before Pitch Snap loads with Pitch Snap Off");
        CHECK (std::fabs (proc.apvts.getRawParameterValue ("structure")->load() - 0.71f) < 1e-3f, "... and its other controls as saved");
    }
    {
        // this version's own state round trip
        RingsAudioProcessor a, b;
        setEverything (a);
        juce::MemoryBlock mb;
        a.getStateInformation (mb);
        b.setStateInformation (mb.getData(), (int) mb.getSize());
        int same = 0;
        for (auto* p : allParams (a)) if (std::fabs (realValue (p) - realValue (param (b, p->paramID.toRawUTF8()))) < 1e-4f) ++same;
        CHECK (same == 15, "this version's saved state comes back exactly (%d of 15)", same);
    }
}

#ifndef RINGS_NO_PANEL

// ------------------------------------------------------------------------------------------------ 3. the panel

static juce::Component* findById (juce::Component& c, const juce::String& id)
{
    if (c.getComponentID() == id) return &c;
    for (auto* child : c.getChildren())
        if (auto* f = findById (*child, id)) return f;
    return nullptr;
}

static void click (juce::Component& c, float x, float y)
{
    const juce::Point<float> p (x, y);
    const juce::MouseEvent e (juce::Desktop::getInstance().getMainMouseSource(), p, juce::ModifierKeys(), 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                              &c, &c, juce::Time::getCurrentTime(), p, juce::Time::getCurrentTime(), 1, false);
    c.mouseDown (e);
}

static float brightnessAt (const juce::Image& img, float s, float x, float y)
{
    return img.getPixelAt (juce::jlimit (0, img.getWidth() - 1, (int) (x * s)), juce::jlimit (0, img.getHeight() - 1, (int) (y * s))).getBrightness();
}

static void testPanel()
{
    using namespace AnmStyle;
    const float w = titleWidth();
    CHECK (w > 60.0f && w <= titleRight - titleLeft, "title width %.1f fits the title area (%.0f)", w, titleRight - titleLeft);
    std::printf ("title \"%s\": %.1f of %.0f panel units\n", titleString(), w, titleRight - titleLeft);

    // every control of the plugin is on the panel, live, and its labels are the parameter's own choices
    {
        RingsAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
        auto* ed = dynamic_cast<RingsEditor*> (edBase.get());
        CHECK (ed != nullptr && ed->getWidth() == panelW && ed->getHeight() == panelH, "the window is %d x %d", ed ? ed->getWidth() : 0, ed ? ed->getHeight() : 0);
        if (ed == nullptr) return;
        auto& panel = ed->getPanel();
        int found = 0;
        for (auto* p : allParams (proc))
        {
            auto* c = findById (panel, p->paramID);
            const bool ok = c != nullptr && c->isEnabled();
            CHECK (ok, "control %s is on the panel and live", p->paramID.toRawUTF8());
            if (ok) ++found;
            if (auto* seg = dynamic_cast<AnmSegments*> (c))
                if (auto* ch = dynamic_cast<juce::AudioParameterChoice*> (p))
                {
                    bool same = seg->getLabels().size() == ch->choices.size();
                    for (int i = 0; same && i < ch->choices.size(); ++i)
                        same = seg->getLabels()[i].equalsIgnoreCase (ch->choices[i]);
                    CHECK (same, "%s: the switch shows the parameter's own choices", p->paramID.toRawUTF8());
                }
        }
        std::printf ("panel controls: %d of %d on the panel\n", found, (int) allParams (proc).size());
        CHECK (panel.getBackdrop().getCachedComponentImage() != nullptr, "the panel's fixed artwork is stored as a picture");

        // Mode decides whether Model or Synth FX is shown (never both)
        auto* model = findById (panel, "model");
        auto* fx = findById (panel, "synth_fx");
        CHECK (model->isVisible() && ! fx->isVisible(), "Mode Resonator: Model is shown, Synth FX is not");
        setReal (param (proc, "mode"), 1.0f);
        CHECK (! model->isVisible() && fx->isVisible(), "Mode String synth: Synth FX is shown, Model is not");
        setReal (param (proc, "mode"), 0.0f);
        CHECK (model->isVisible() && ! fx->isVisible(), "... and back");

        // clicks reach the controls
        auto list = [&] (const char* id, int rows, int row, const char* what)
        {
            auto* c = findById (panel, id);
            click (*c, 20.0f, (float) c->getHeight() / (float) rows * ((float) row + 0.5f));
            CHECK ((int) realValue (param (proc, id)) == row, "clicking row %d of %s sets it (%s)", row, id, what);
        };
        list ("exciter", 2, 1, "Audio input");
        list ("exciter", 2, 0, "Internal");
        for (int r = 0; r < 3; ++r) list ("strum_source", 3, r, "Strum from");
        for (int r = 0; r < 3; ++r) list ("pitch_snap", 3, r, "Pitch Snap");
        for (int r = 0; r < 6; ++r) list ("model", 6, r, "Model");
        setReal (param (proc, "mode"), 1.0f);
        for (int r = 0; r < 6; ++r) list ("synth_fx", 6, r, "Synth FX");
        auto pill = [&] (const char* id, int cells, int cell)
        {
            auto* c = findById (panel, id);
            click (*c, (float) c->getWidth() / (float) cells * ((float) cell + 0.5f), 10.0f);
            CHECK ((int) realValue (param (proc, id)) == cell, "clicking cell %d of %s sets it", cell, id);
        };
        for (int i = 0; i < 2; ++i) pill ("mode", 2, i);
        for (int i = 0; i < 3; ++i) pill ("polyphony", 3, i);
        for (const char* id : { "strum_btn", "mono_out" })
        {
            auto* c = findById (panel, id);
            click (*c, 20.0f, (float) c->getHeight() * 0.5f);
            CHECK (realValue (param (proc, id)) == 1.0f, "a click switches %s on", id);
            click (*c, 20.0f, (float) c->getHeight() * 0.5f);
            CHECK (realValue (param (proc, id)) == 0.0f, "a second click switches %s off", id);
        }

        // the value texts (Transpose shows the snapped total when Pitch Snap is on, as before)
        panel.refreshTexts();
        CHECK (panel.getValueText (0) == "0.25" && panel.getValueText (1) == "0.50" && panel.getValueText (4) == "0" && panel.getValueText (5) == "0.0",
               "default value texts: %s %s %s %s", panel.getValueText (0).toRawUTF8(), panel.getValueText (1).toRawUTF8(),
               panel.getValueText (4).toRawUTF8(), panel.getValueText (5).toRawUTF8());
        setReal (param (proc, "pitch_snap"), 1.0f);
        setReal (param (proc, "transpose"), 5.0f);
        setReal (param (proc, "fm"), 3.0f);
        panel.refreshTexts();
        const int expect = (int) std::lround (RingsEngine::snapPitch (8.0f, 1));
        CHECK (panel.getValueText (4) == (expect > 0 ? "+" + juce::String (expect) : juce::String (expect)),
               "Transpose shows the snapped total (Steps, 5 + 3): %s", panel.getValueText (4).toRawUTF8());
        setReal (param (proc, "pitch_snap"), 0.0f);
        panel.refreshTexts();
        CHECK (panel.getValueText (4) == "5" && panel.getValueText (5) == "+3.0", "Pitch Snap Off: Transpose 5, FM +3.0 (%s, %s)",
               panel.getValueText (4).toRawUTF8(), panel.getValueText (5).toRawUTF8());
        auto* k = dynamic_cast<AnmKnob*> (findById (panel, "fm"));
        CHECK (k != nullptr && k->getTextFromValue (3.0) == panel.getValueText (5), "the drag bubble shows the same text as the panel");
    }

    // a project saved by 0.2.0 loaded while the panel is open: the panel follows (Synth FX is chosen: mode was String synth)
    {
        RingsAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
        auto& panel = dynamic_cast<RingsEditor*> (edBase.get())->getPanel();
        juce::File dir (RINGS_GOLDEN_DIR);
        auto xml = juce::XmlDocument::parse (goldenStateFile (dir));
        loadXml (proc, *xml);
        panel.refreshTexts();
        CHECK (findById (panel, "synth_fx")->isVisible() && ! findById (panel, "model")->isVisible(), "loading a project saved in String synth mode shows Synth FX");
        CHECK (panel.getValueText (0) == "0.71" && panel.getValueText (3) == "0.18", "... and its knob values (%s, %s)",
               panel.getValueText (0).toRawUTF8(), panel.getValueText (3).toRawUTF8());
    }

    // every text fits its place (measured)
    {
        int tight = 0;
        float worst = 1.0e9f; juce::String worstWhat;
        for (const auto& it : RingsPanel::fitItems())
        {
            const float tw = widthOf (it.text, it.font), room = it.x1 - it.x0;
            CHECK (tw <= room, "text fits: %s needs %.1f of %.1f units", it.what.toRawUTF8(), tw, room);
            if (room - tw < worst) { worst = room - tw; worstWhat = it.what; }
            if (room - tw < 4.0f) ++tight;
        }
        std::printf ("texts: %d measured, tightest: %s with %.1f units to spare\n", (int) RingsPanel::fitItems().size(), worstWhat.toRawUTF8(), worst);
        (void) tight;
    }

    // in the drawn pixels, at 100% and 200%
    for (float s : { 1.0f, 2.0f })
    {
        RingsAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        const auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, s);
        CHECK (img.isValid() && img.getWidth() == juce::roundToInt (panelW * s), "scale %.0f%%: the panel draws (%d wide)", s * 100.0f, img.getWidth());
        // the title's gold stays inside its area, and nothing bright sits in the gaps between the title area and the ornament lines
        int spill = 0, inside = 0;
        for (int y = (int) (10 * s); y < (int) (40 * s); ++y)
        {
            for (int x = (int) std::ceil (titleLeft * s); x < (int) (titleRight * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++inside;
            for (int x = (int) std::ceil ((ornamentLeftEnd + 1.5f) * s); x < (int) ((titleLeft - 0.5f) * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
            for (int x = (int) std::ceil ((titleRight + 0.5f) * s); x < (int) ((ornamentRightStart - 1.5f) * s); ++x)
                if (img.getPixelAt (x, y).getBrightness() > 0.6f) ++spill;
        }
        CHECK (inside > 60 && spill == 0, "scale %.0f%%: title drawn inside its area (%d lit pixels inside, %d outside)", s * 100.0f, inside, spill);
        // nothing is drawn outside the frames (left and right of them), and the footer stays inside the window
        int outside = 0;
        for (int y = (int) (48 * s); y < (int) (575 * s); ++y)
        {
            for (int x = 0; x < (int) (12.0f * s); ++x)          if (img.getPixelAt (x, y).getBrightness() > 0.35f) ++outside;
            for (int x = (int) (668.0f * s); x < img.getWidth(); ++x) if (img.getPixelAt (x, y).getBrightness() > 0.35f) ++outside;
        }
        CHECK (outside == 0, "scale %.0f%%: nothing drawn outside the frames (%d lit pixels)", s * 100.0f, outside);
        // no text is cut off at the edge of its switch (lists, option, round button)
        int cut = 0;
        auto& panel = dynamic_cast<RingsEditor*> (ed.get())->getPanel();
        for (auto* c : panel.getChildren())
            if (auto* seg = dynamic_cast<AnmSegments*> (c))
            {
                if (! seg->isVisible() || seg->getKind() == AnmSegments::Kind::Pill) continue;
                const auto b = seg->getBounds();
                for (int y = b.getY(); y < b.getBottom(); ++y)
                    for (int dx = 1; dx <= 2; ++dx)
                    {
                        const float xr = (float) b.getRight() - (float) dx * 0.5f;
                        if (brightnessAt (img, s, xr, (float) y) > 0.35f) { ++cut; }
                    }
            }
        CHECK (cut == 0, "scale %.0f%%: no switch text is cut off at its edge (%d lit pixels)", s * 100.0f, cut);
    }
}

// ------------------------------------------------------------------------------------------------ 4. drawing cost

static void testDrawingCost()
{
    for (float s : { 1.0f, 2.0f })
    {
        RingsAudioProcessor proc;
        std::unique_ptr<juce::AudioProcessorEditor> edBase (proc.createEditor());
        auto* ed = dynamic_cast<RingsEditor*> (edBase.get());
        if (ed == nullptr) { CHECK (false, "the panel opens"); return; }
        juce::Image img (juce::Image::ARGB, juce::roundToInt (ed->getWidth() * s), juce::roundToInt (ed->getHeight() * s), true);
        juce::RectangleList<int> live;
        for (const auto& a : RingsPanel::liveAreas())
            live.add (a.toFloat().transformedBy (juce::AffineTransform::scale (s)).getSmallestIntegerContainer());
        auto timeIt = [&] (bool liveOnly, int n)
        {
            const auto t0 = std::chrono::steady_clock::now();
            for (int i = 0; i < n; ++i)
            {
                juce::Graphics g (img);
                if (liveOnly) g.reduceClipRegion (live);
                g.addTransform (juce::AffineTransform::scale (s));
                ed->paintEntireComponent (g, true);
            }
            return std::chrono::duration<double, std::milli> (std::chrono::steady_clock::now() - t0).count() / n;
        };
        auto& backdrop = ed->getPanel().getBackdrop();
        backdrop.setBufferedToImage (false);
        timeIt (false, 3);
        const double whole = timeIt (false, 20);
        backdrop.setBufferedToImage (true);
        timeIt (true, 3);
        const double tick = timeIt (true, 60);
        std::printf ("drawing at %.0f%%: live update %.2f ms, whole panel every time %.2f ms\n", s * 100.0f, tick, whole);
        CHECK (tick < 0.6 * whole, "scale %.0f%%: the live update (%.2f ms) is much lighter than drawing the whole panel (%.2f ms)", s * 100.0f, tick, whole);
        CHECK (tick < 6.0, "scale %.0f%%: the live update takes %.2f ms (limit 6 ms; 30 updates a second)", s * 100.0f, tick);
    }
}

// ------------------------------------------------------------------------------------------------ pictures of the panel

static void writeSnapshots (const juce::File& dir)
{
    dir.createDirectory();
    auto shoot = [&] (RingsAudioProcessor& proc, const char* name, float scale)
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
        const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, scale);
        juce::PNGImageFormat png;
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0);
        out.truncate();
        png.writeImageToStream (image, out);
    };
    { RingsAudioProcessor p; shoot (p, "panel-first-load-100.png", 1.0f); }
    { RingsAudioProcessor p; shoot (p, "panel-first-load-200.png", 2.0f); }
    {
        RingsAudioProcessor p;                                   // String synth mode, Pitch Snap on, buttons on
        setReal (param (p, "mode"), 1.0f);   setReal (param (p, "synth_fx"), 1.0f);
        setReal (param (p, "pitch_snap"), 1.0f); setReal (param (p, "transpose"), 7.0f); setReal (param (p, "fm"), 12.5f);
        setReal (param (p, "polyphony"), 2.0f);  setReal (param (p, "exciter"), 1.0f);
        setReal (param (p, "strum_source"), 2.0f); setReal (param (p, "strum_btn"), 1.0f); setReal (param (p, "mono_out"), 1.0f);
        shoot (p, "panel-string-synth-everything-on-100.png", 1.0f);
    }
    {
        RingsAudioProcessor p;                                   // Resonator, the longest model name, knobs moved
        setReal (param (p, "model"), 4.0f);
        setReal (param (p, "structure"), 0.9f); setReal (param (p, "brightness"), 0.1f);
        setReal (param (p, "damping"), 0.0f);   setReal (param (p, "position"), 1.0f);
        setReal (param (p, "transpose"), -36.0f); setReal (param (p, "fm"), -48.0f);
        shoot (p, "panel-resonator-longest-name-extremes-100.png", 1.0f);
    }
}

#endif // RINGS_NO_PANEL

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;

    juce::File goldenDir (RINGS_GOLDEN_DIR);
    for (int i = 1; i < argc; ++i)
    {
        if (juce::String (argv[i]) == "--write-golden") { writeGoldenFiles (goldenDir); return 0; }
#ifndef RINGS_NO_PANEL
        if (juce::String (argv[i]) == "--snapshot" && i + 1 < argc) { writeSnapshots (juce::File (argv[i + 1])); return 0; }
#endif
    }

    testControlList (goldenDir);
    testSavedProjects (goldenDir);
#ifndef RINGS_NO_PANEL
    testPanel();
    testDrawingCost();
#endif

    std::printf ("plugin checks: %d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
