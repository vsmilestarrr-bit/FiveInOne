#include "PluginProcessor.h"
#include "PluginEditor.h"

FiveInOneProcessor::FiveInOneProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "STATE", Params::createLayout())
{
}

void FiveInOneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    pitch.prepare (sampleRate);
    eq.prepare (sampleRate);
    mb.prepare (sampleRate, samplesPerBlock);
    limiter.prepare (sampleRate);
    fifoIndex = 0;
    setLatencySamples (PitchCorrector::kLatency);
}

bool FiveInOneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;
    return out == layouts.getMainInputChannelSet();
}

void FiveInOneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    for (int c = getTotalNumInputChannels(); c < getTotalNumOutputChannels(); ++c)
        buffer.clear (c, 0, n);

    // 1) Auto-Tune (always runs the delay so latency stays constant)
    pitch.process (buffer, get ("at_on") > 0.5f, (int) get ("at_key"), (int) get ("at_scale"),
                   get ("at_speed"), get ("at_amt"));

    // 2) EQ
    if (get ("eq_on") > 0.5f)
    {
        float f[Params::kNumEq], g[Params::kNumEq], q[Params::kNumEq];
        for (int b = 0; b < Params::kNumEq; ++b)
        {
            f[b] = apvts.getRawParameterValue (Params::eqFreq (b))->load();
            g[b] = apvts.getRawParameterValue (Params::eqGain (b))->load();
            q[b] = apvts.getRawParameterValue (Params::eqQ (b))->load();
        }
        eq.update (f, g, q);
        eq.process (buffer);
    }

    // 3) Multiband compressor
    if (get ("mb_on") > 0.5f)
    {
        mb.setCrossover (get ("xo1"), get ("xo2"));
        for (int b = 0; b < 3; ++b)
        {
            auto v = [&] (const char* w) { return apvts.getRawParameterValue (Params::cp (b, w))->load(); };
            mb.setBand (b, v ("thr"), v ("ratio"), v ("att"), v ("rel"), v ("mk"));
        }
        mb.process (buffer);
    }

    // 4) Output gain + limiter
    limiter.process (buffer, get ("out_gain"), get ("lim_on") > 0.5f, get ("lim_ceil"));

    // 5) Feed the visual analyser (post-chain)
    const int nCh = buffer.getNumChannels();
    if (nCh > 0)
    {
        const float* l = buffer.getReadPointer (0);
        const float* r = buffer.getReadPointer (nCh > 1 ? 1 : 0);
        for (int i = 0; i < n; ++i)
            pushSample (0.5f * (l[i] + r[i]));
    }
}

void FiveInOneProcessor::pushSample (float s)
{
    if (fifoIndex == fftSize)
    {
        if (! nextFFTBlockReady.load())
        {
            fftBlock = fifo;
            nextFFTBlockReady = true;
        }
        fifoIndex = 0;
    }
    fifo[(size_t) fifoIndex++] = s;
}

bool FiveInOneProcessor::pullFFTBlock (std::array<float, fftSize>& dest)
{
    if (! nextFFTBlockReady.load()) return false;
    dest = fftBlock;
    nextFFTBlockReady = false;
    return true;
}

juce::AudioProcessorEditor* FiveInOneProcessor::createEditor() { return new FiveInOneEditor (*this); }

void FiveInOneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void FiveInOneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new FiveInOneProcessor(); }
