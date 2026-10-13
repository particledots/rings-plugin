#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "RingsEngine.h"

class RingsAudioProcessor : public juce::AudioProcessor
{
public:
    RingsAudioProcessor();
    ~RingsAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    using AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 10.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    juce::AudioProcessorValueTreeState apvts;

    static const juce::StringArray& modelNames();
    static const juce::StringArray& synthFxNames();

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    RingsEngine::Params readParams() const;

    RingsEngine engine;
    std::vector<uint8_t> strumFlags;
    std::vector<float> noteValues, scratchL, scratchR;
    float currentNote = 60.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RingsAudioProcessor)
};
