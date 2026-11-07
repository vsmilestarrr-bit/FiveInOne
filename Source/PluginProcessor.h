#pragma once
#include <JuceHeader.h>
#include <array>
#include <atomic>
#include "Params.h"
#include "DSP.h"

class FiveInOneProcessor : public juce::AudioProcessor
{
public:
    static constexpr int fftOrder = 11;
    static constexpr int fftSize  = 1 << fftOrder;

    FiveInOneProcessor();
    ~FiveInOneProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "FiveInOne"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // spectrum analyser feed for the editor
    bool pullFFTBlock (std::array<float, fftSize>& dest);

    juce::AudioProcessorValueTreeState apvts;

private:
    void pushSample (float s);
    float get (const char* id) const { return apvts.getRawParameterValue (id)->load(); }

    PitchCorrector pitch;
    ParametricEQ   eq;
    MultibandComp  mb;
    OutputLimiter  limiter;

    std::array<float, fftSize> fifo {}, fftBlock {};
    int fifoIndex = 0;
    std::atomic<bool> nextFFTBlockReady { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FiveInOneProcessor)
};
