#include "RingsEngine.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <new>
#include "rings/dsp/part.h"
#include "rings/dsp/strummer.h"
#include "rings/dsp/string_synth_part.h"
#include "rings/dsp/patch.h"
#include "rings/dsp/performance_state.h"

namespace
{
    // The firmware's noise gate on the audio input (rings.cc).
    constexpr float kNoiseGateThreshold = 0.00003f;

    // Where "no V/Oct patched" lands: the module plays tonic = 12 + transpose with note = 0.
    // We add three octaves so the default is a usable pitch rather than 16 Hz.
    constexpr int kUnquantizedBase = 36;
}

namespace
{
    // calloc, not new + memset: with a memset before the constructor, GCC treats the old contents as dead and deletes it
    // (lifetime dead-store elimination), which silently brings the junk back.
    template <typename T>
    std::unique_ptr<T, ZeroedDeleter<T>> makeZeroed()
    {
        void* mem = std::calloc (1, sizeof (T));
        if (mem == nullptr) throw std::bad_alloc();
        return std::unique_ptr<T, ZeroedDeleter<T>> (new (mem) T());
    }
}

template <typename T>
void ZeroedDeleter<T>::operator() (T* p) const
{
    if (p != nullptr) { p->~T(); std::free (static_cast<void*> (p)); }
}

RingsEngine::RingsEngine()
    : part (makeZeroed<rings::Part>()), synth (makeZeroed<rings::StringSynthPart>()), strummer (makeZeroed<rings::Strummer>())
{
    reverbBuf.assign (32768, 0);
    part->Init (reverbBuf.data());
    synth->Init (reverbBuf.data());
    strummer->Init (0.01f, (float) (rings::kSampleRate / rings::kMaxBlockSize));
}

RingsEngine::~RingsEngine() = default;

void RingsEngine::prepare (double sr)
{
    hostRate = sr;
    rsIn.init (hostRate, kRate);
    rsOutL.init (kRate, hostRate);
    rsOutR.init (kRate, hostRate);
    inQueue.clear(); inQueueHead = 0;
    procOutL.clear(); procOutR.clear();
    hostQL.clear(); hostQR.clear(); hostQHead = 0;
    const size_t prime = kBlock * 2 + SincResampler::kTaps;     // keeps the output resampler from starving
    procOutL.assign (prime, 0.0f); procOutR.assign (prime, 0.0f);
    curMode = -1;
    prevManual = false; manualInit = false;
    inLevel = 0.0f;
    pendStrum = 0; pendNote = lastNote;
}

int RingsEngine::latencyInHostSamples() const
{
    const double procFrames = (double) kBlock * 2 + SincResampler::kTaps;
    if (std::abs (hostRate - kRate) < 1e-6) return (int) procFrames;
    return (int) std::ceil (procFrames * hostRate / kRate);
}

void RingsEngine::applyConfig (const Params& p)
{
    const int poly = p.polyphony >= 4 ? 4 : (p.polyphony >= 2 ? 2 : 1);
    const int mode = p.stringSynth ? 1 : 0;

    if (mode != curMode)
    {   // both parts share one reverb/FX memory (as in the firmware): wipe it when switching between them
        std::fill (reverbBuf.begin(), reverbBuf.end(), (uint16_t) 0);
        curMode = mode;
        curPoly = -1; curModel = -1; curFx = -1;
    }
    if (poly != curPoly)
    {
        part->set_polyphony (poly);
        synth->set_polyphony (poly);
        curPoly = poly;
    }
    const int model = std::clamp (p.model, 0, (int) rings::RESONATOR_MODEL_LAST - 1);
    if (model != curModel)
    {
        part->set_model ((rings::ResonatorModel) model);
        curModel = model;
    }
    const int fx = std::clamp (p.synthFx, 0, (int) rings::FX_LAST - 1);
    if (fx != curFx)
    {
        synth->set_fx ((rings::FxType) fx);
        curFx = fx;
    }
}

float RingsEngine::snapPitch (float semitones, int mode)
{
    if (mode == SnapSemitones) return std::round (semitones);
    if (mode != SnapSteps) return semitones;
    static const float kSteps[] = { 0.f, 7.f, 12.f, 19.f, 24.f, 31.f, 36.f, 43.f, 48.f, 55.f, 60.f, 67.f, 72.f, 79.f, 84.f };   // mirrored below zero
    const float a = std::fabs (semitones);
    float best = kSteps[0];
    for (float d : kSteps)
        if (std::fabs (a - d) < std::fabs (a - best)) best = d;
    return semitones < 0.f ? -best : best;
}

void RingsEngine::runBlocks (const Params& p)
{
    using namespace rings;
    const size_t B = kBlock;

    const bool audioStrum = p.strumSource == StrumAudioOnset;
    const bool externalExciter = p.exciter == AudioExciter || audioStrum;

    float in[kMaxBlockSize], out[kMaxBlockSize], aux[kMaxBlockSize];

    while (inQueue.size() - inQueueHead >= B)
    {
        // ---- gather this block's audio and events ----
        uint8_t anyStrum = 0;
        float note = lastNote;
        for (size_t i = 0; i < B; ++i)
        {
            const Frame& f = inQueue[inQueueHead + i];
            anyStrum |= f.strum;
            note = f.note;
            float s = externalExciter ? f.in : 0.0f;
            if (externalExciter && ! p.stringSynth)
            {
                float error = s * s - inLevel;
                inLevel += error * (error > 0.0f ? 0.1f : 0.0001f);
                const float gain = inLevel <= kNoiseGateThreshold ? (1.0f / kNoiseGateThreshold) * inLevel : 1.0f;
                s *= gain;
            }
            in[i] = s;
        }
        inQueueHead += B;
        lastNote = note;

        // ---- control smoothing, as the firmware's CV scaler does once per block ----
        auto lp = [] (float& state, float target) { state += 0.01f * (target - state); return state; };
        lp (lpStructure, p.structure); lp (lpBrightness, p.brightness);
        lp (lpDamping, p.damping);     lp (lpPosition, p.position);

        Patch patch;
        patch.structure  = std::clamp (lpStructure,  0.0f, 0.9995f);
        patch.brightness = std::clamp (lpBrightness, 0.0f, 1.0f);
        patch.damping    = std::clamp (lpDamping,    0.0f, 1.0f);
        patch.position   = std::clamp (lpPosition,   0.0f, 1.0f);

        // Pitch Snap: with it on, Transpose and FM are one offset, snapped as a whole and carried by the FM path (the firmware adds
        // tonic + note + fm, so the sum is what matters); Off is the untouched Transpose-in-tonic / FM-in-fm path.
        const bool snapOn = p.pitchSnap != SnapOff;
        const float fmTarget = snapOn ? snapPitch ((float) p.transpose + p.fm, p.pitchSnap) : p.fm;
        const int tonicOffset = snapOn ? 0 : p.transpose;
        const float fmLimit = snapOn ? 96.0f : 48.0f;

        const float fmErr = fmTarget - fmTracked;   // firmware: snap on big jumps, otherwise glide
        if (std::fabs (fmErr) >= 0.8f) fmTracked = fmTarget; else fmTracked += 0.02f * fmErr;

        PerformanceState ps;
        ps.fm = std::clamp (fmTracked, -fmLimit, fmLimit);
        ps.internal_exciter = ! externalExciter;

        switch (p.strumSource)
        {
            case StrumMidi:
                ps.internal_strum = false; ps.internal_note = false;
                ps.strum = anyStrum != 0;
                ps.note = note - 12.0f;                      // so that note + tonic = MIDI note + transpose
                ps.tonic = 12.0f + (float) tonicOffset;
                break;
            case StrumAudioOnset:
                ps.internal_strum = true; ps.internal_note = true;
                ps.strum = false;                             // Strummer fills this in from the audio onsets
                ps.note = 0.0f;
                ps.tonic = 12.0f + (float) (kUnquantizedBase + tonicOffset);
                break;
            default:   // StrumManual
                ps.internal_strum = false; ps.internal_note = true;
                ps.strum = manualInit && (p.manualStrumState != prevManual);
                ps.note = 0.0f;
                ps.tonic = 12.0f + (float) (kUnquantizedBase + tonicOffset);
                break;
        }
        prevManual = p.manualStrumState; manualInit = true;

        // chord (string-synth mode) comes from the Structure knob, with the firmware's hysteresis
        {
            float c = lpStructure * (float) (kNumChords - 1);
            const float hyst = c - (float) chord > 0.0f ? -0.1f : +0.1f;
            chord = std::clamp ((int) (c + hyst + 0.5f), 0, (int) kNumChords - 1);
            ps.chord = chord;
        }

        strummer->Process (in, B, &ps);
        if (p.stringSynth) synth->Process (ps, patch, in, out, aux, B);
        else               part->Process (ps, patch, in, out, aux, B);

        for (size_t i = 0; i < B; ++i)
        {
            float l = out[i], r = aux[i];
            if (! std::isfinite (l)) l = 0.0f;
            if (! std::isfinite (r)) r = 0.0f;
            if (p.monoOut) { l = r = 0.7071f * (l + r); }
            procOutL.push_back (std::clamp (l, -4.0f, 4.0f));
            procOutR.push_back (std::clamp (r, -4.0f, 4.0f));
        }
    }
    if (inQueueHead > 4096)
    {
        inQueue.erase (inQueue.begin(), inQueue.begin() + (long) inQueueHead);
        inQueueHead = 0;
    }
}

void RingsEngine::process (const float* inL, const float* inR, float* outL, float* outR,
                           int n, const Params& p, const Control& c)
{
    applyConfig (p);

    // 1. host rate -> 48 kHz. Events are OR-ed over the span of host samples each 48 kHz sample covers,
    //    so a note-on is never dropped when the host runs faster than 48 kHz.
    monoIn.resize ((size_t) n);
    for (int i = 0; i < n; ++i) monoIn[(size_t) i] = 0.5f * (inL[i] + inR[i]);
    tmpIn.clear();
    rsIn.process (monoIn.data(), (size_t) n, tmpIn);
    const size_t produced = tmpIn.size();
    size_t j = 0;
    for (size_t i = 0; i < produced; ++i)
    {
        const size_t end = (i + 1 == produced) ? (size_t) n : (i + 1) * (size_t) n / produced;
        for (; j < end; ++j)
        {
            if (c.strum && c.strum[j]) pendStrum = 1;
            if (c.note) pendNote = c.note[j];
        }
        inQueue.push_back ({ tmpIn[i], pendStrum, pendNote });
        pendStrum = 0;
    }
    for (; j < (size_t) n; ++j)       // nothing produced yet this call: keep events for the next one
    {
        if (c.strum && c.strum[j]) pendStrum = 1;
        if (c.note) pendNote = c.note[j];
    }

    // 2. run the module DSP
    runBlocks (p);

    // 3. 48 kHz -> host rate
    rsOutL.process (procOutL.data(), procOutL.size(), hostQL);
    rsOutR.process (procOutR.data(), procOutR.size(), hostQR);
    procOutL.clear(); procOutR.clear();

    const size_t avail = std::min (hostQL.size(), hostQR.size()) - hostQHead;
    const size_t take = std::min (avail, (size_t) n);
    for (size_t i = 0; i < take; ++i) { outL[i] = hostQL[hostQHead + i]; outR[i] = hostQR[hostQHead + i]; }
    for (size_t i = take; i < (size_t) n; ++i) { outL[i] = 0.0f; outR[i] = 0.0f; }
    hostQHead += take;
    if (hostQHead > 8192)
    {
        hostQL.erase (hostQL.begin(), hostQL.begin() + (long) hostQHead);
        hostQR.erase (hostQR.begin(), hostQR.begin() + (long) hostQHead);
        hostQHead = 0;
    }
}
