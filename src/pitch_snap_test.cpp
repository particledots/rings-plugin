// Pitch Snap (our addition): Transpose + FM settle on octaves and fifths (Steps) or whole semitones (Semitones).
//  1. the snapping function: exact over the whole range (Transpose +-36 plus FM +-48 = +-84); Off returns its input unchanged
//  2. through the engine: with Pitch Snap on, the sound matches Off with the plain Transpose / FM set to the snapped value
//     (so the pitch is exactly what that setting always gave), and differs from the unsnapped setting
//  3. every model, mode and strum source stays finite and audible in all three snap modes while the offset sweeps
// It says nothing about how anything sounds.
#include "RingsEngine.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <algorithm>
#include <functional>
#include <cstring>

using E = RingsEngine;

// Renders `seconds` of strummed output (L then R interleaved). A MIDI strum (or, with the Manual source, a button flip) every
// half second. `params` is called before every block so a test can sweep. Returns the left channel only.
static std::vector<float> render (double sr, double seconds, const std::function<E::Params (int block)>& params)
{
    E e; e.prepare (sr);
    const int N = 512;
    std::vector<float> in (N, 0.0f), oL (N), oR (N), note (N, 57.0f), all;
    std::vector<uint8_t> strum (N, 0);
    const int blocks = (int) (sr * seconds / N);
    const long every = (long) (sr * 0.5);
    long count = 0;
    for (int b = 0; b < blocks; ++b)
    {
        for (int i = 0; i < N; ++i, ++count)
        {
            strum[(size_t) i] = (count % every == 0) ? 1 : 0;
            note[(size_t) i] = 48.0f + (float) ((count / every) % 4) * 3.0f;
        }
        E::Params p = params (b);
        p.manualStrumState = ((count / every) & 1) != 0;
        E::Control c; c.strum = strum.data(); c.note = note.data();
        e.process (in.data(), in.data(), oL.data(), oR.data(), N, p, c);
        all.insert (all.end(), oL.begin(), oL.end());
    }
    return all;
}

static double rms (const std::vector<float>& a) { double e = 0; for (float v : a) e += (double) v * v; return std::sqrt (e / (double) std::max<size_t> (1, a.size())); }
static double rmsDiff (const std::vector<float>& a, const std::vector<float>& b)
{
    double e = 0; const size_t n = std::min (a.size(), b.size());
    for (size_t i = 0; i < n; ++i) { const double d = (double) a[i] - (double) b[i]; e += d * d; }
    return std::sqrt (e / (double) std::max<size_t> (1, n));
}

int main()
{
    int fails = 0;

    // 1. the function
    {
        static const float steps[] = { 0, 7, 12, 19, 24, 31, 36, 43, 48, 55, 60, 67, 72, 79, 84 };
        bool ok = true; float prev = -1000.f; int distinct = 0; float last = 1e9f;
        for (int i = -8500; i <= 8500; ++i)
        {
            const float k = (float) i * 0.01f;
            const float st = E::snapPitch (k, E::SnapSteps);
            const float off = E::snapPitch (k, E::SnapOff);
            float bestDist = 1e9f; for (float s : steps) bestDist = std::min (bestDist, std::fabs (std::fabs (k) - s));
            bool onStep = false; for (float s : steps) onStep = onStep || std::fabs (std::fabs (st) - s) < 1e-6f;
            ok = ok && onStep && st >= prev - 1e-6f && std::fabs (std::fabs (st) - std::fabs (k)) <= bestDist + 1e-4f
                    && (st == 0.f || (st < 0.f) == (k < 0.f))
                    && std::fabs (E::snapPitch (k, E::SnapSemitones) - std::round (k)) < 1e-6f
                    && std::memcmp (&k, &off, sizeof (float)) == 0;
            prev = st; if (st != last) { ++distinct; last = st; }
        }
        ok = ok && distinct == 29;
        printf ("snap function over -85..+85: Steps visit exactly 29 values (%d seen), the nearest one, never backwards; Semitones round; Off returns the same bits  %s\n",
                distinct, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 2. through the engine: snapped setting == plain Transpose / FM at the snapped value
    {
        struct C { int transpose; float fm; int snap; int refTranspose; float refFm; };
        const C cs[] = {
            {  7,  0.30f, E::SnapSteps,     7,  0.0f },    //  7.3 -> 7
            {  5,  0.00f, E::SnapSteps,     7,  0.0f },    //  5   -> 7 (nearest of 0, 7)
            { -9,  0.00f, E::SnapSteps,    -7,  0.0f },    // -9   -> -7
            { 36, 20.00f, E::SnapSteps,    36, 19.0f },    // 56   -> 55, across both controls
            { 12, 30.00f, E::SnapSteps,    12, 31.0f },    // 42   -> 43
            { -3,  0.40f, E::SnapSemitones, -3, 0.0f },    // -2.6 -> -3
            {  4,  0.60f, E::SnapSemitones,  5, 0.0f },    //  4.6 -> 5
            {  0, -7.40f, E::SnapSemitones,  0, -7.0f },
        };
        bool ok = true; double worstSame = 0, leastMoved = 1e9;
        for (int src : { (int) E::StrumMidi, (int) E::StrumManual })
            for (int mode : { 0, 1 })                     // resonator, string synth
                for (const auto& c : cs)
                {
                    auto base = [&] (int t, float f, int snap)
                    {
                        return [=] (int) { E::Params p; p.strumSource = src; p.stringSynth = mode == 1; p.polyphony = 2;
                                           p.transpose = t; p.fm = f; p.pitchSnap = snap; return p; };
                    };
                    const auto a = render (48000.0, 2.0, base (c.transpose, c.fm, c.snap));            // snap on
                    const auto r = render (48000.0, 2.0, base (c.refTranspose, c.refFm, E::SnapOff));  // plain, at the snapped value
                    const auto u = render (48000.0, 2.0, base (c.transpose, c.fm, E::SnapOff));        // plain, unsnapped
                    const double level = rms (r);
                    const double dSame = rmsDiff (a, r) / std::max (level, 1e-9);
                    const double dUnsnapped = rmsDiff (a, u) / std::max (level, 1e-9);
                    worstSame = std::max (worstSame, dSame); leastMoved = std::min (leastMoved, dUnsnapped);
                    const bool same = level > 1e-4 && dSame < 1e-2;
                    const bool moved = dUnsnapped > 0.05;
                    if (! same || ! moved)
                    {
                        ok = false;
                        printf ("  src %d mode %d: transpose %+d fm %+.2f snap %d: level %.5f, vs snapped-plain %.5f (want < 0.01), vs unsnapped-plain %.5f (want > 0.05)\n",
                                src, mode, c.transpose, c.fm, c.snap, level, dSame, dUnsnapped);
                    }
                }
        printf ("pitch snap through the engine: 8 settings x MIDI/manual strum x resonator/string synth match the plain setting at the snapped value (worst relative difference %.6f, limit 0.01) and differ from the unsnapped one (least %.3f, needs > 0.05)  %s\n", worstSame, leastMoved, ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 2b. a setting already on the grid with snap on is the same pitch as Off
    {
        bool ok = true;
        for (int snap : { (int) E::SnapSteps, (int) E::SnapSemitones })
        {
            auto make = [&] (int s) { return [=] (int) { E::Params p; p.transpose = 12; p.fm = 0.0f; p.pitchSnap = s; return p; }; };
            const auto a = render (48000.0, 2.0, make (snap));
            const auto r = render (48000.0, 2.0, make (E::SnapOff));
            const double d = rmsDiff (a, r) / std::max (rms (r), 1e-9);
            if (! (d < 1e-3)) { ok = false; printf ("  on-grid snap %d: difference %.5f\n", snap, d); }
        }
        printf ("on-grid settings sound the same with Pitch Snap on or off  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    // 3. every model / mode / strum source, every snap mode, offset sweeping both controls
    {
        bool ok = true;
        for (int snap : { (int) E::SnapOff, (int) E::SnapSteps, (int) E::SnapSemitones })
            for (int src : { (int) E::StrumMidi, (int) E::StrumManual })
                for (int variant = 0; variant < 12; ++variant)          // 6 resonator models, 6 string-synth effects
                {
                    const auto a = render (48000.0, 2.0, [=] (int b)
                    {
                        E::Params p; p.strumSource = src; p.pitchSnap = snap; p.polyphony = (variant % 3 == 0) ? 1 : (variant % 3 == 1 ? 2 : 4);
                        if (variant < 6) { p.stringSynth = false; p.model = variant; } else { p.stringSynth = true; p.synthFx = variant - 6; }
                        p.transpose = (int) std::lround (36.0 * std::sin ((double) b * 0.11));
                        p.fm = 48.0f * (float) std::sin ((double) b * 0.23);
                        return p;
                    });
                    bool fin = true; double e = 0, pk = 0;
                    for (float v : a) { fin = fin && std::isfinite (v); e += (double) v * v; pk = std::max (pk, (double) std::fabs (v)); }
                    if (! (fin && e > 0.05 && pk < 4.0)) { ok = false; printf ("  snap %d src %d variant %d: finite=%d energy=%.2f peak=%.2f\n", snap, src, variant, (int) fin, e, pk); }
                }
        printf ("all models x string synth effects x 2 strum sources x 3 snap modes stay finite and audible while Transpose and FM sweep  %s\n", ok ? "OK" : "FAIL");
        fails += ! ok;
    }

    printf ("%d failed\n", fails);
    return fails;
}
