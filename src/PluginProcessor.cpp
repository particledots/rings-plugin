#include "PluginProcessor.h"
#include "PluginEditor.h"

using APF = juce::AudioParameterFloat;
using APB = juce::AudioParameterBool;
using APC = juce::AudioParameterChoice;
using API = juce::AudioParameterInt;

const juce::StringArray& RingsAudioProcessor::modelNames()
{
    static const juce::StringArray n { "Modal resonator", "Sympathetic strings", "Modulated/inharmonic string",
                                       "FM voice", "Sympathetic strings (quantized)", "String + reverb" };
    return n;
}

const juce::StringArray& RingsAudioProcessor::synthFxNames()
{
    static const juce::StringArray n { "Formant filter", "Chorus", "Reverb", "Formant filter 2", "Ensemble", "Reverb 2" };
    return n;
}

juce::AudioProcessorValueTreeState::ParameterLayout RingsAudioProcessor::createLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> p;
    auto unit = [] (const char* id, const char* name, float def)
    {
        return std::make_unique<APF> (juce::ParameterID { id, 1 }, name, juce::NormalisableRange<float> (0.f, 1.f, 0.0001f), def,
                                      juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return juce::String (v, 2); }));
    };

    p.push_back (unit ("structure",  "Structure",  0.25f));
    p.push_back (unit ("brightness", "Brightness", 0.5f));
    p.push_back (unit ("damping",    "Damping",    0.5f));
    p.push_back (unit ("position",   "Position",   0.5f));
    p.push_back (std::make_unique<API> (juce::ParameterID { "transpose", 1 }, "Transpose", -36, 36, 0,
                 juce::AudioParameterIntAttributes().withLabel ("st")));
    p.push_back (std::make_unique<APF> (juce::ParameterID { "fm", 1 }, "FM",
                 juce::NormalisableRange<float> (-48.f, 48.f, 0.01f), 0.f,
                 juce::AudioParameterFloatAttributes().withLabel ("st")));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "polyphony", 1 }, "Polyphony",
                 juce::StringArray { "1 voice", "2 voices", "4 voices" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "mode", 1 }, "Mode",
                 juce::StringArray { "Resonator", "String synth" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "model", 1 }, "Resonator Model", modelNames(), 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "synth_fx", 1 }, "String Synth FX", synthFxNames(), 4));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "exciter", 1 }, "Exciter",
                 juce::StringArray { "Internal", "Audio input" }, 0));
    p.push_back (std::make_unique<APC> (juce::ParameterID { "strum_source", 1 }, "Strum",
                 juce::StringArray { "MIDI notes", "Audio onset", "Manual button" }, 0));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "strum_btn", 1 }, "Strum Button", false));
    p.push_back (std::make_unique<APB> (juce::ParameterID { "mono_out", 1 }, "Mono Out", false));
    return { p.begin(), p.end() };
}

RingsAudioProcessor::RingsAudioProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", createLayout())
{
}

bool RingsAudioProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono())
           && l.getMainInputChannelSet() == out;
}

void RingsAudioProcessor::prepareToPlay (double sr, int maxBlock)
{
    engine.prepare (sr);
    strumFlags.assign ((size_t) maxBlock, 0);
    noteValues.assign ((size_t) maxBlock, currentNote);
    scratchL.assign ((size_t) maxBlock, 0.f);
    scratchR.assign ((size_t) maxBlock, 0.f);
    setLatencySamples (engine.latencyInHostSamples());
}

RingsEngine::Params RingsAudioProcessor::readParams() const
{
    auto v = [this] (const char* id) { return apvts.getRawParameterValue (id)->load(); };
    RingsEngine::Params p;
    p.structure = v ("structure"); p.brightness = v ("brightness"); p.damping = v ("damping"); p.position = v ("position");
    p.transpose = (int) std::lround (v ("transpose"));
    p.fm = v ("fm");
    const int polyChoice = (int) v ("polyphony");
    p.polyphony = polyChoice == 0 ? 1 : (polyChoice == 1 ? 2 : 4);
    p.stringSynth = (int) v ("mode") == 1;
    p.model = (int) v ("model");
    p.synthFx = (int) v ("synth_fx");
    p.exciter = (int) v ("exciter");
    p.strumSource = (int) v ("strum_source");
    p.manualStrumState = v ("strum_btn") > 0.5f;
    p.monoOut = v ("mono_out") > 0.5f;
    return p;
}

void RingsAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int chans = buffer.getNumChannels();

    if ((size_t) n > scratchL.size())
    {   // host exceeded its promised max block size
        scratchL.resize ((size_t) n); scratchR.resize ((size_t) n);
        strumFlags.resize ((size_t) n); noteValues.resize ((size_t) n);
    }

    const auto params = readParams();

    // MIDI note-on -> strum (sample-accurate), note number -> pitch. Note-offs are ignored: Rings has no gate.
    std::fill (strumFlags.begin(), strumFlags.begin() + n, (uint8_t) 0);
    {
        int pos = 0;
        for (const auto meta : midi)
        {
            const auto msg = meta.getMessage();
            const int at = juce::jlimit (0, n, meta.samplePosition);
            for (; pos < at; ++pos) noteValues[(size_t) pos] = currentNote;
            if (msg.isNoteOn())
            {
                currentNote = (float) msg.getNoteNumber();
                if (at < n) strumFlags[(size_t) at] = 1;
            }
        }
        for (; pos < n; ++pos) noteValues[(size_t) pos] = currentNote;
    }
    midi.clear();

    RingsEngine::Control ctl; ctl.strum = strumFlags.data(); ctl.note = noteValues.data();

    const float* inL = buffer.getReadPointer (0);
    const float* inR = buffer.getReadPointer (chans > 1 ? 1 : 0);
    engine.process (inL, inR, scratchL.data(), scratchR.data(), n, params, ctl);

    if (chans > 1)
    {
        buffer.copyFrom (0, 0, scratchL.data(), n);
        buffer.copyFrom (1, 0, scratchR.data(), n);
    }
    else
    {
        float* o = buffer.getWritePointer (0);
        for (int i = 0; i < n; ++i) o[i] = 0.7071f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
    }
}

juce::AudioProcessorEditor* RingsAudioProcessor::createEditor()
{
    return new RingsEditor (*this);
}

void RingsAudioProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void RingsAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new RingsAudioProcessor(); }
