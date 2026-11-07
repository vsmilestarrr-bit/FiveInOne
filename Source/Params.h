#pragma once
#include <JuceHeader.h>

namespace Params
{
    constexpr int kNumEq = 6;

    inline const juce::StringArray kNotes  { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    inline const juce::StringArray kScales { "Chromatic", "Major", "Minor" };

    inline constexpr float kEqDefFreq[kNumEq] = { 60.f, 200.f, 700.f, 2000.f, 6000.f, 12000.f };
    inline constexpr float kEqDefQ[kNumEq]    = { 0.707f, 1.f, 1.f, 1.f, 1.f, 0.707f };

    inline juce::String eqFreq (int i) { return "eq" + juce::String (i) + "_freq"; }
    inline juce::String eqGain (int i) { return "eq" + juce::String (i) + "_gain"; }
    inline juce::String eqQ    (int i) { return "eq" + juce::String (i) + "_q"; }
    inline juce::String cp (int band, const char* what) { return "c" + juce::String (band) + "_" + what; }

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        using PID = ParameterID;
        using Range = NormalisableRange<float>;

        AudioProcessorValueTreeState::ParameterLayout l;

        // ---- Auto-Tune
        l.add (std::make_unique<AudioParameterBool>   (PID { "at_on", 1 },    "AutoTune On", false));
        l.add (std::make_unique<AudioParameterChoice> (PID { "at_key", 1 },   "AutoTune Key", kNotes, 0));
        l.add (std::make_unique<AudioParameterChoice> (PID { "at_scale", 1 }, "AutoTune Scale", kScales, 0));
        l.add (std::make_unique<AudioParameterFloat>  (PID { "at_speed", 1 }, "AutoTune Retune Speed", Range (0.f, 300.f, 0.1f, 0.5f), 20.f));
        l.add (std::make_unique<AudioParameterFloat>  (PID { "at_amt", 1 },   "AutoTune Amount", Range (0.f, 1.f, 0.01f), 1.f));

        // ---- EQ
        l.add (std::make_unique<AudioParameterBool> (PID { "eq_on", 1 }, "EQ On", true));
        for (int i = 0; i < kNumEq; ++i)
        {
            const auto n = "EQ " + String (i + 1);
            l.add (std::make_unique<AudioParameterFloat> (PID { eqFreq (i), 1 }, n + " Freq", Range (20.f, 20000.f, 0.1f, 0.3f), kEqDefFreq[i]));
            l.add (std::make_unique<AudioParameterFloat> (PID { eqGain (i), 1 }, n + " Gain", Range (-18.f, 18.f, 0.1f), 0.f));
            l.add (std::make_unique<AudioParameterFloat> (PID { eqQ (i), 1 },    n + " Q",    Range (0.2f, 10.f, 0.01f, 0.5f), kEqDefQ[i]));
        }

        // ---- Multiband compressor
        l.add (std::make_unique<AudioParameterBool>  (PID { "mb_on", 1 }, "MB Comp On", true));
        l.add (std::make_unique<AudioParameterFloat> (PID { "xo1", 1 }, "Crossover 1", Range (40.f, 1000.f, 1.f, 0.4f), 200.f));
        l.add (std::make_unique<AudioParameterFloat> (PID { "xo2", 1 }, "Crossover 2", Range (1000.f, 12000.f, 1.f, 0.4f), 2500.f));
        for (int b = 0; b < 3; ++b)
        {
            const auto n = "Band " + String (b + 1) + " ";
            l.add (std::make_unique<AudioParameterFloat> (PID { cp (b, "thr"), 1 }, n + "Threshold", Range (-60.f, 0.f, 0.1f), -18.f));
            l.add (std::make_unique<AudioParameterFloat> (PID { cp (b, "ratio"), 1 }, n + "Ratio", Range (1.f, 20.f, 0.1f, 0.5f), 3.f));
            l.add (std::make_unique<AudioParameterFloat> (PID { cp (b, "att"), 1 }, n + "Attack", Range (1.f, 100.f, 0.1f, 0.5f), 15.f));
            l.add (std::make_unique<AudioParameterFloat> (PID { cp (b, "rel"), 1 }, n + "Release", Range (20.f, 1000.f, 1.f, 0.5f), 150.f));
            l.add (std::make_unique<AudioParameterFloat> (PID { cp (b, "mk"), 1 }, n + "Makeup", Range (-12.f, 12.f, 0.1f), 0.f));
        }

        // ---- Output
        l.add (std::make_unique<AudioParameterFloat> (PID { "out_gain", 1 }, "Output Gain", Range (-24.f, 24.f, 0.1f), 0.f));
        l.add (std::make_unique<AudioParameterBool>  (PID { "lim_on", 1 }, "Limiter On", true));
        l.add (std::make_unique<AudioParameterFloat> (PID { "lim_ceil", 1 }, "Limiter Ceiling", Range (-12.f, 0.f, 0.1f), -0.3f));

        return l;
    }
}
