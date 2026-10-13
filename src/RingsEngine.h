// JUCE-free wrapper around the Mutable Instruments Rings DSP (MIT licensed, Emilie Gillet).
#pragma once
#include <memory>
#include <vector>
#include <cstdint>
#include "Resampler.h"

namespace rings { class Part; class StringSynthPart; class Strummer; }

// The module firmware keeps its DSP objects in globals, i.e. zeroed memory, and some of them rely on that (members that are
// never explicitly initialised). A plain `new` returns whatever junk the heap held, which can crash or silence the plug-in
// depending on what the process happened to allocate earlier. These objects are therefore always built in zero-filled memory.
template <typename T> struct ZeroedDeleter { void operator() (T*) const; };

class RingsEngine
{
public:
    enum Exciter { InternalExciter = 0, AudioExciter = 1 };
    enum StrumSource { StrumMidi = 0, StrumAudioOnset = 1, StrumManual = 2 };

    struct Params
    {
        // The four main knobs (0..1), same meaning as the module.
        float structure = 0.25f, brightness = 0.5f, damping = 0.5f, position = 0.5f;
        int   transpose = 0;            // semitones, added to the played pitch
        float fm = 0.0f;                // semitones, -48..48 (the module's FM input after its attenuverter)
        int   polyphony = 1;            // 1, 2 or 4
        int   model = 0;                // resonator model, 0..5 (see README)
        bool  stringSynth = false;      // false = resonator, true = the firmware's string-synth mode
        int   synthFx = 4;              // string-synth effect, 0..5
        int   exciter = InternalExciter;
        int   strumSource = StrumMidi;
        bool  manualStrumState = false; // any change of this flag strums in Manual mode
        bool  monoOut = false;          // sum odd/even outputs to both channels
    };

    // Per-host-sample MIDI-derived control. Both pointers may be null.
    //   strum[i] != 0 : a note-on happened at sample i
    //   note[i]       : MIDI note number (float) in effect at sample i
    struct Control { const uint8_t* strum = nullptr; const float* note = nullptr; };

    RingsEngine();
    ~RingsEngine();

    void prepare (double hostSampleRate);

    void process (const float* inL, const float* inR, float* outL, float* outR,
                  int numSamples, const Params& p, const Control& c);

    int latencyInHostSamples() const;

    static constexpr double kRate = 48000.0;
    static constexpr size_t kBlock = 24;

private:
    struct Frame { float in; uint8_t strum; float note; };

    void applyConfig (const Params& p);
    void runBlocks (const Params& p);

    std::unique_ptr<rings::Part, ZeroedDeleter<rings::Part>> part;
    std::unique_ptr<rings::StringSynthPart, ZeroedDeleter<rings::StringSynthPart>> synth;
    std::unique_ptr<rings::Strummer, ZeroedDeleter<rings::Strummer>> strummer;
    std::vector<uint16_t> reverbBuf;

    double hostRate = 48000.0;
    int curMode = -1, curPoly = -1, curModel = -1, curFx = -1;
    uint8_t pendStrum = 0;         // events waiting for the next 48 kHz sample
    float pendNote = 60.0f;
    std::vector<float> monoIn;

    // firmware-style control smoothing (per 24-sample block)
    float lpStructure = 0.25f, lpBrightness = 0.5f, lpDamping = 0.5f, lpPosition = 0.5f;
    float fmTracked = 0.0f;
    int chord = 0;
    float inLevel = 0.0f;          // noise-gate state
    bool prevManual = false, manualInit = false;
    float lastNote = 60.0f;

    SincResampler rsIn, rsOutL, rsOutR;
    std::vector<float> tmpIn;
    std::vector<float> hostQL, hostQR;
    size_t hostQHead = 0;
    std::vector<Frame> inQueue;
    size_t inQueueHead = 0;
    std::vector<float> procOutL, procOutR;
};
