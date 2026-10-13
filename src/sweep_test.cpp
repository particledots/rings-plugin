// Regression test for the "uninitialised DSP memory" bug: switches resonator model / polyphony / string-synth effect while
// strumming, with fresh heap blocks filled with a junk byte (env JUNK=0xNN). The module firmware gets zeroed memory for free
// (its DSP objects are globals); a plug-in has to ask for it. A segment that is silent, stuck at DC or non-finite is a failure.
// Global operator new override: fills fresh heap blocks with a junk byte (env JUNK=0xNN), as a long-running DAW process may hand back.
#include <cstdlib>
#include <cstring>
#include <new>
#include <cstdio>
#include <cmath>
static int g_junk = -1;
void* operator new (size_t n) { void* p = malloc (n); if (! p) throw std::bad_alloc(); if (g_junk >= 0) memset (p, g_junk, n); return p; }
void* operator new[] (size_t n) { void* p = malloc (n); if (! p) throw std::bad_alloc(); if (g_junk >= 0) memset (p, g_junk, n); return p; }
void operator delete (void* p) noexcept { free (p); }
void operator delete (void* p, size_t) noexcept { free (p); }
void operator delete[] (void* p) noexcept { free (p); }
void operator delete[] (void* p, size_t) noexcept { free (p); }
struct SegStat { double energy = 0, sum = 0; long cnt = 0; bool nonfinite = false;
  void add (float l, float r) { if (! std::isfinite (l) || ! std::isfinite (r)) { nonfinite = true; return; } energy += (double) l * l + (double) r * r; sum += l; ++cnt; }
  const char* verdict (double& dc) const { dc = cnt ? sum / (double) cnt : 0; double var = cnt ? energy / (2.0 * (double) cnt) - dc * dc : 0;
     if (nonfinite) return "NON-FINITE"; if (energy < 0.001) return "SILENT"; if (var < 1e-7) return "STUCK-DC"; return nullptr; } };
#include "RingsEngine.h"
#include <vector>
int main (int argc, char** argv)
{
    if (const char* j = getenv ("JUNK")) g_junk = (int) strtol (j, nullptr, 0);
    const double sr = argc > 1 ? atof (argv[1]) : 48000.0;
    struct Cfg { int model, poly; bool synth; int fx; };
    std::vector<Cfg> order;
    for (int m = 0; m < 6; ++m) order.push_back ({ m, 1, false, 4 });
    for (int m = 5; m >= 0; --m) order.push_back ({ m, 1, false, 4 });
    for (int fx = 0; fx < 6; ++fx) order.push_back ({ 0, 2, true, fx });
    for (int m = 0; m < 6; ++m) { order.push_back ({ m, 4, false, 4 }); order.push_back ({ m, 2, true, m }); }
    for (int k = 0; k < 4; ++k) { order.push_back ({ 3, 1, false, 4 }); order.push_back ({ 3, 4, true, 2 }); order.push_back ({ 1, 2, false, 4 }); }
    int bad = 0;
    RingsEngine e; e.prepare (sr);
    const int N = 512;
    std::vector<float> in (N, 0.f), L (N), R (N), note (N, 57.f); std::vector<uint8_t> strum (N, 0);
    long count = 0; const long seg = (long) (sr * 0.8), every = (long) (sr * 0.2);
    for (size_t s = 0; s < order.size(); ++s)
    {
        RingsEngine::Params p; p.model = order[s].model; p.polyphony = order[s].poly; p.stringSynth = order[s].synth; p.synthFx = order[s].fx;
        SegStat st; long start = count;
        while (count - start < seg)
        {
            for (int i = 0; i < N; ++i, ++count) { strum[i] = (count % every) == 0; note[i] = 48.f + (float) (((count / every) % 4) * 3); }
            RingsEngine::Control c; c.strum = strum.data(); c.note = note.data();
            e.process (in.data(), in.data(), L.data(), R.data(), N, p, c);
            if (count - start > seg / 4) for (int i = 0; i < N; ++i) st.add (L[i], R[i]);
        }
        double dc; if (const char* v = st.verdict (dc)) { ++bad; printf ("step=%2zu model=%d poly=%d synth=%d fx=%d  %s energy=%.4f dc=%.3f\n", s, order[s].model, order[s].poly, order[s].synth, order[s].fx, v, st.energy, dc); }
    }
    printf ("RINGS sr=%.0f junk=%d : %d bad segments of %zu\n", sr, g_junk, bad, order.size());
    return bad != 0;
}
