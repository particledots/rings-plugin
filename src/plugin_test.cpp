// Drives the real RingsAudioProcessor (parameters -> engine, MIDI, state) without a host or a window, for Pitch Snap:
//  - Pitch Snap defaults to Off, and a project saved before it existed loads with it Off
//  - through the real "pitch_snap", "transpose" and "fm" parameters, a snapped setting sounds like the plain setting at the
//    snapped value (so the pitch is what that setting always gave), and unlike the unsnapped one
//  - the setting survives a state save and restore
// Run: build/plugin_test
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <vector>

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter();

static void setParam (RingsAudioProcessor& p, const char* id, float plainValue)
{
    auto* prm = p.apvts.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (plainValue));
}

static std::unique_ptr<RingsAudioProcessor> fresh()
{
    std::unique_ptr<juce::AudioProcessor> base (createPluginFilter());
    auto* p = static_cast<RingsAudioProcessor*> (base.release());
    p->setPlayConfigDetails (2, 2, 48000.0, 512);
    p->prepareToPlay (48000.0, 512);
    return std::unique_ptr<RingsAudioProcessor> (p);
}

// two seconds: a MIDI note-on (a strum) every half second, notes cycling; returns the left channel
static std::vector<float> play (RingsAudioProcessor& p)
{
    const int block = 512, blocks = 188;
    std::vector<float> out;
    juce::AudioBuffer<float> buf (2, block);
    for (int b = 0; b < blocks; ++b)
    {
        juce::MidiBuffer midi;
        if (b % 47 == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 48 + 3 * ((b / 47) % 4), 0.8f), 0);
        buf.clear();
        p.processBlock (buf, midi);
        out.insert (out.end(), buf.getReadPointer (0), buf.getReadPointer (0) + block);
    }
    return out;
}

static double rms (const std::vector<float>& a) { double e = 0; for (float v : a) e += (double) v * v; return std::sqrt (e / (double) std::max<size_t> (1, a.size())); }
static double relDiff (const std::vector<float>& a, const std::vector<float>& ref)
{
    double e = 0; for (size_t i = 0; i < std::min (a.size(), ref.size()); ++i) { const double d = (double) a[i] - (double) ref[i]; e += d * d; }
    return std::sqrt (e / (double) std::max<size_t> (1, a.size())) / std::max (rms (ref), 1e-9);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    int fails = 0;
    auto check = [&] (bool ok, const char* what) { if (! ok) { ++fails; std::printf ("FAIL: %s\n", what); } else std::printf ("ok:   %s\n", what); };

    {
        auto p = fresh();
        check ((int) p->apvts.getRawParameterValue ("pitch_snap")->load() == 0, "Pitch Snap defaults to Off");
        check (p->apvts.getParameter ("pitch_snap")->getCurrentValueAsText() == "Off", "Pitch Snap menu text is Off");
    }

    {
        struct C { int snap; float transpose, fm; float refTranspose, refFm; };
        const C cs[] = { { 1, 5, 0.3f, 7, 0.0f },      // Steps: 5.3 -> 7
                         { 1, -9, 0.0f, -7, 0.0f },    // Steps: -9 -> -7
                         { 1, 36, 20.0f, 36, 19.0f },  // Steps: 56 -> 55 across both controls
                         { 2, 4, 0.6f, 5, 0.0f },      // Semitones: 4.6 -> 5
                         { 2, -3, 0.4f, -3, 0.0f } };  // Semitones: -2.6 -> -3
        for (const auto& c : cs)
        {
            auto a = fresh(); setParam (*a, "pitch_snap", (float) c.snap); setParam (*a, "transpose", c.transpose); setParam (*a, "fm", c.fm);
            auto r = fresh(); setParam (*r, "transpose", c.refTranspose); setParam (*r, "fm", c.refFm);
            auto u = fresh(); setParam (*u, "transpose", c.transpose); setParam (*u, "fm", c.fm);
            const auto sa = play (*a), sr = play (*r), su = play (*u);
            const double same = relDiff (sa, sr), moved = relDiff (sa, su);
            char msg[200]; std::snprintf (msg, sizeof msg, "processor: snap=%d transpose=%+.0f fm=%+.1f sounds like plain transpose=%+.0f fm=%+.1f (diff %.5f < 0.01) and not like the unsnapped setting (diff %.3f > 0.05)",
                                          c.snap, c.transpose, c.fm, c.refTranspose, c.refFm, same, moved);
            check (rms (sr) > 1e-4 && same < 0.01 && moved > 0.05, msg);
        }
    }

    {
        auto p = fresh();
        setParam (*p, "pitch_snap", 1.0f); setParam (*p, "structure", 0.9f);
        juce::MemoryBlock state; p->getStateInformation (state);
        auto q = fresh();
        q->setStateInformation (state.getData(), (int) state.getSize());
        check ((int) q->apvts.getRawParameterValue ("pitch_snap")->load() == 1, "Pitch Snap restored from state");

        auto xml = juce::AudioProcessor::getXmlFromBinary (state.getData(), (int) state.getSize());
        bool removed = false;
        if (xml != nullptr)
            for (auto* child = xml->getFirstChildElement(); child != nullptr; child = child->getNextElement())
                if (child->getStringAttribute ("id") == "pitch_snap") { xml->removeChildElement (child, true); removed = true; break; }
        check (removed, "test setup: found pitch_snap in the saved state");
        juce::MemoryBlock old; juce::AudioProcessor::copyXmlToBinary (*xml, old);
        auto r = fresh();
        r->setStateInformation (old.getData(), (int) old.getSize());
        check ((int) r->apvts.getRawParameterValue ("pitch_snap")->load() == 0, "a state saved without Pitch Snap loads with it Off");
        check (std::fabs (r->apvts.getRawParameterValue ("structure")->load() - 0.9f) < 1e-3f, "...and its other settings still load");
    }

    std::printf ("%d failures\n", fails);
    return fails != 0;
}
