// Offline sanity test of RingsEngine: every model / polyphony / mode, at several host sample rates.
// Checks only that output is finite, bounded and not silent. It says nothing about how it sounds.
#include "RingsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <random>

struct Result { bool ok; double energy, peak; };

static Result run (double sr, RingsEngine::Params p, bool midiStrums, bool noiseIn, double seconds = 4.0)
{
    RingsEngine e; e.prepare (sr);
    const int N = 512;
    std::vector<float> in (N), outL (N), outR (N), note (N, 57.0f);
    std::vector<uint8_t> strum (N, 0);
    std::mt19937 rng (1234);
    std::uniform_real_distribution<float> dist (-1.f, 1.f);
    const int blocks = (int) (sr * seconds / N);
    const int strumEvery = (int) (sr * 0.5);            // a strum every half second
    long sampleCount = 0; bool bad = false; double peak = 0, energy = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < N; ++i, ++sampleCount)
        {
            const bool burst = noiseIn && (sampleCount % strumEvery) < (long) (sr * 0.01);
            in[(size_t) i] = burst ? 0.8f * dist (rng) : 0.0f;
            strum[(size_t) i] = (midiStrums && sampleCount % strumEvery == 0) ? 1 : 0;   // exactly ONE host sample wide
            note[(size_t) i] = 48.0f + (float) ((sampleCount / strumEvery) % 4) * 3.0f;
        }
        RingsEngine::Control c; c.strum = strum.data(); c.note = note.data();
        p.manualStrumState = ((sampleCount / strumEvery) & 1) != 0;
        e.process (in.data(), in.data(), outL.data(), outR.data(), N, p, c);
        for (int i = 0; i < N; ++i)
        {
            const float l = outL[(size_t) i], r = outR[(size_t) i];
            if (! std::isfinite (l) || ! std::isfinite (r)) bad = true;
            peak = std::max (peak, (double) std::max (std::fabs (l), std::fabs (r)));
            energy += l * l + r * r;
        }
    }
    return { ! bad && energy > 0.05 && peak < 4.0, energy, peak };
}

int main()
{
    int fails = 0, runs = 0;
    auto report = [&] (const char* what, double sr, const Result& r)
    {
        ++runs; fails += ! r.ok;
        printf ("%-44s sr=%6.0f energy=%10.2f peak=%5.2f  %s\n", what, sr, r.energy, r.peak, r.ok ? "OK" : "FAIL");
    };

    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        // resonator models x polyphony, MIDI strum (single-sample note-on) with the internal exciter
        for (int model = 0; model < 6; ++model)
            for (int poly : { 1, 2, 4 })
            {
                RingsEngine::Params p; p.model = model; p.polyphony = poly; p.stringSynth = false;
                char name[96]; snprintf (name, sizeof name, "resonator model=%d poly=%d midi/internal", model, poly);
                report (name, sr, run (sr, p, true, false));
            }
        // audio exciter + MIDI strum
        { RingsEngine::Params p; p.exciter = RingsEngine::AudioExciter;
          report ("resonator midi + audio exciter", sr, run (sr, p, true, true)); }
        // strum from audio onsets
        { RingsEngine::Params p; p.strumSource = RingsEngine::StrumAudioOnset;
          report ("resonator audio-onset strum", sr, run (sr, p, false, true)); }
        // manual strum button
        { RingsEngine::Params p; p.strumSource = RingsEngine::StrumManual;
          report ("resonator manual strum", sr, run (sr, p, false, false)); }
        // string synth, every effect
        for (int fx = 0; fx < 6; ++fx)
        {
            RingsEngine::Params p; p.stringSynth = true; p.synthFx = fx; p.polyphony = 2;
            char name[96]; snprintf (name, sizeof name, "string synth fx=%d poly=2 midi", fx);
            report (name, sr, run (sr, p, true, false));
        }
        // extremes of the knobs
        for (float v : { 0.0f, 1.0f })
        {
            RingsEngine::Params p; p.structure = v; p.brightness = v; p.damping = v; p.position = v; p.fm = 48.f * (v * 2 - 1);
            char name[96]; snprintf (name, sizeof name, "all knobs at %.0f, fm extreme", v);
            report (name, sr, run (sr, p, true, false));
        }
    }
    printf ("%d runs, %d failed\n", runs, fails);
    return fails;
}
