#pragma once
#include <JuceHeader.h>
#include <array>
#include <cmath>
#include "Params.h"

//==============================================================================
// 1) AUTO-TUNE: YIN pitch detection + scale quantiser + two-tap crossfaded
//    delay-line pitch shifter. Latency = kLatency samples.
//==============================================================================
class PitchCorrector
{
public:
    static constexpr int kWin       = 2048;
    static constexpr int kLatency   = kWin / 2;
    static constexpr int kDelaySize = 4096;
    static constexpr int kMask      = kDelaySize - 1;
    static constexpr int kRing      = 4096;
    static constexpr int kRingMask  = kRing - 1;
    static constexpr int kDetWin    = 1024;
    static constexpr int kHop       = 512;

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (auto& d : delay) d.fill (0.f);
        ring.fill (0.f);
        hist.fill (60.f);
        wp = ringW = hop = histIdx = 0;
        phase = shiftSemis = targetSemis = 0.f;
    }

    void process (juce::AudioBuffer<float>& buf, bool on, int key, int scale, float speedMs, float amount)
    {
        const int nCh = juce::jmin (buf.getNumChannels(), 2);
        const int n   = buf.getNumSamples();
        if (nCh == 0) return;

        float* ch[2] = { buf.getWritePointer (0), nCh > 1 ? buf.getWritePointer (1) : nullptr };
        const float coef = speedMs < 0.5f ? 1.f : 1.f - std::exp (-1.f / (speedMs * 0.001f * (float) fs));

        for (int i = 0; i < n; ++i)
        {
            float mono = 0.f;
            for (int c = 0; c < nCh; ++c) mono += ch[c][i];
            mono /= (float) nCh;

            ring[(size_t) ringW] = mono;
            ringW = (ringW + 1) & kRingMask;

            if (++hop >= kHop)
            {
                hop = 0;
                if (on) detect (key, scale);
                else    targetSemis = 0.f;
            }

            shiftSemis += (targetSemis - shiftSemis) * coef;
            const float ratio = std::pow (2.f, shiftSemis * amount / 12.f);

            const float d1 = phase * (float) kWin;
            float p2 = phase + 0.5f;
            if (p2 >= 1.f) p2 -= 1.f;
            const float d2 = p2 * (float) kWin;
            const float w1 = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * phase);
            const float w2 = 1.f - w1;

            for (int c = 0; c < nCh; ++c)
            {
                auto& dl = delay[(size_t) c];
                dl[(size_t) wp] = ch[c][i];
                const float wet = w1 * readTap (dl, d1) + w2 * readTap (dl, d2);
                const float dry = dl[(size_t) ((wp - kLatency) & kMask)];
                ch[c][i] = on ? wet : dry;
            }

            wp = (wp + 1) & kMask;
            phase += (1.f - ratio) / (float) kWin;
            phase -= std::floor (phase);
        }
    }

private:
    float readTap (const std::array<float, kDelaySize>& dl, float d) const
    {
        const float pos = (float) wp - d;
        const int   i0  = (int) std::floor (pos);
        const float fr  = pos - (float) i0;
        const float a = dl[(size_t) (i0 & kMask)];
        const float b = dl[(size_t) ((i0 + 1) & kMask)];
        return a + fr * (b - a);
    }

    static bool isAllowed (int note, int key, int scale)
    {
        static const bool maj[12] = { 1,0,1,0,1,1,0,1,0,1,0,1 };
        static const bool mnr[12] = { 1,0,1,1,0,1,0,1,1,0,1,0 };
        const int pc = ((note - key) % 12 + 12) % 12;
        if (scale == 1) return maj[pc];
        if (scale == 2) return mnr[pc];
        return true;
    }

    void detect (int key, int scale)
    {
        const int tauMin = juce::jmax (2, (int) (fs / 1000.0));
        const int tauMax = juce::jmin ((int) (fs / 70.0), kRing - kDetWin - 2);
        const int n = kDetWin + tauMax;

        for (int i = 0; i < n; ++i)
            tmp[(size_t) i] = ring[(size_t) ((ringW - n + i) & kRingMask)];

        float e = 0.f;
        for (int i = n - kDetWin; i < n; ++i) e += tmp[(size_t) i] * tmp[(size_t) i];
        if (std::sqrt (e / (float) kDetWin) < 0.01f) { targetSemis = 0.f; return; }   // silence / unvoiced

        cmnd[0] = 1.f;
        float run = 0.f;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float d = 0.f;
            for (int j = 0; j < kDetWin; ++j)
            {
                const float df = tmp[(size_t) j] - tmp[(size_t) (j + tau)];
                d += df * df;
            }
            run += d;
            cmnd[(size_t) tau] = run > 0.f ? d * (float) tau / run : 1.f;
        }

        int best = -1;
        for (int t = tauMin; t < tauMax; ++t)
        {
            if (cmnd[(size_t) t] < 0.15f)
            {
                while (t + 1 < tauMax && cmnd[(size_t) (t + 1)] < cmnd[(size_t) t]) ++t;
                best = t;
                break;
            }
        }
        if (best < 0) { targetSemis = 0.f; return; }

        const float s0 = cmnd[(size_t) (best - 1)], s1 = cmnd[(size_t) best], s2 = cmnd[(size_t) (best + 1)];
        const float den = s0 - 2.f * s1 + s2;
        const float off = std::abs (den) > 1e-9f ? 0.5f * (s0 - s2) / den : 0.f;
        const float f0  = (float) fs / ((float) best + off);

        float midi = 69.f + 12.f * std::log2 (f0 / 440.f);

        // median of 3 to kill octave glitches
        hist[(size_t) histIdx] = midi;
        histIdx = (histIdx + 1) % 3;
        const float a = hist[0], b = hist[1], c = hist[2];
        midi = juce::jmax (juce::jmin (a, b), juce::jmin (juce::jmax (a, b), c));

        const int base = (int) std::round (midi);
        float bestDist = 1e9f;
        int bestNote = base;
        for (int k = -6; k <= 6; ++k)
        {
            const int note = base + k;
            if (! isAllowed (note, key, scale)) continue;
            const float dist = std::abs ((float) note - midi);
            if (dist < bestDist) { bestDist = dist; bestNote = note; }
        }
        targetSemis = juce::jlimit (-12.f, 12.f, (float) bestNote - midi);
    }

    double fs = 44100.0;
    std::array<std::array<float, kDelaySize>, 2> delay {};
    std::array<float, kRing> ring {}, tmp {}, cmnd {};
    std::array<float, 3> hist { 60.f, 60.f, 60.f };
    int wp = 0, ringW = 0, hop = 0, histIdx = 0;
    float phase = 0.f, shiftSemis = 0.f, targetSemis = 0.f;
};

//==============================================================================
// 2) PARAMETRIC EQ: low shelf, 4 peaks, high shelf
//==============================================================================
class ParametricEQ
{
public:
    using Coeffs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    static Coeffs::Ptr makeCoeffs (int band, double sr, float freq, float gainDb, float q)
    {
        const float g = juce::Decibels::decibelsToGain (gainDb);
        freq = juce::jlimit (20.f, (float) (sr * 0.45), freq);
        if (band == 0)                    return Coeffs::makeLowShelf  (sr, freq, q, g);
        if (band == Params::kNumEq - 1)   return Coeffs::makeHighShelf (sr, freq, q, g);
        return Coeffs::makePeakFilter (sr, freq, q, g);
    }

    void prepare (double sampleRate)
    {
        fs = sampleRate;
        for (int c = 0; c < 2; ++c)
            for (int b = 0; b < Params::kNumEq; ++b)
            {
                filt[c][b].coefficients = makeCoeffs (b, fs, Params::kEqDefFreq[b], 0.f, Params::kEqDefQ[b]);
                filt[c][b].reset();
            }
        for (int b = 0; b < Params::kNumEq; ++b) { last[b][0] = last[b][1] = last[b][2] = -999.f; active[b] = false; }
    }

    void update (const float* f, const float* g, const float* q)
    {
        for (int b = 0; b < Params::kNumEq; ++b)
        {
            active[b] = std::abs (g[b]) > 0.05f;
            if (f[b] != last[b][0] || g[b] != last[b][1] || q[b] != last[b][2])
            {
                last[b][0] = f[b]; last[b][1] = g[b]; last[b][2] = q[b];
                auto c = makeCoeffs (b, fs, f[b], g[b], q[b]);
                for (int ch = 0; ch < 2; ++ch) filt[ch][b].coefficients = c;
            }
        }
    }

    void process (juce::AudioBuffer<float>& buf)
    {
        const int nCh = juce::jmin (buf.getNumChannels(), 2);
        const int n = buf.getNumSamples();
        for (int c = 0; c < nCh; ++c)
        {
            float* d = buf.getWritePointer (c);
            for (int b = 0; b < Params::kNumEq; ++b)
                if (active[b])
                    for (int i = 0; i < n; ++i)
                        d[i] = filt[c][b].processSample (d[i]);
        }
    }

private:
    double fs = 44100.0;
    Filter filt[2][Params::kNumEq];
    float last[Params::kNumEq][3] {};
    bool active[Params::kNumEq] {};
};

//==============================================================================
// 3) MULTIBAND COMPRESSOR: 3 bands, Linkwitz-Riley 4th order crossovers
//==============================================================================
class MultibandComp
{
public:
    struct Band
    {
        float thr = -18.f, ratio = 3.f, makeupDb = 0.f;
        float attC = 0.f, relC = 0.f, grDb = 0.f;

        void set (float t, float r, float attMs, float relMs, float mk, double fs)
        {
            thr = t; ratio = r; makeupDb = mk;
            attC = std::exp (-1.f / (attMs * 0.001f * (float) fs));
            relC = std::exp (-1.f / (relMs * 0.001f * (float) fs));
        }

        float process (float lvl)
        {
            const float lvlDb = 20.f * std::log10 (juce::jmax (lvl, 1e-6f));
            const float over = lvlDb - thr;
            const float knee = 6.f;
            const float slope = 1.f - 1.f / ratio;
            float gr = 0.f;
            if (2.f * over < -knee)               gr = 0.f;
            else if (2.f * std::abs (over) <= knee) gr = slope * (over + knee * 0.5f) * (over + knee * 0.5f) / (2.f * knee);
            else                                  gr = slope * over;

            const float c = gr > grDb ? attC : relC;
            grDb = c * grDb + (1.f - c) * gr;
            return juce::Decibels::decibelsToGain (-grDb + makeupDb);
        }
    };

    void prepare (double sampleRate, int maxBlock)
    {
        fs = sampleRate;
        juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, maxBlock), 2 };
        xo1.prepare (spec); xo2.prepare (spec); ap.prepare (spec);
        xo1.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        xo2.setType (juce::dsp::LinkwitzRileyFilterType::lowpass);
        ap.setType  (juce::dsp::LinkwitzRileyFilterType::lowpass);
        xo1.reset(); xo2.reset(); ap.reset();
        lastF1 = lastF2 = -1.f;
        for (auto& b : band) b.grDb = 0.f;
    }

    void setCrossover (float f1, float f2)
    {
        f1 = juce::jlimit (20.f, (float) (fs * 0.3), f1);
        f2 = juce::jlimit (f1 * 1.5f, (float) (fs * 0.45), f2);
        if (f1 != lastF1) { xo1.setCutoffFrequency (f1); lastF1 = f1; }
        if (f2 != lastF2) { xo2.setCutoffFrequency (f2); ap.setCutoffFrequency (f2); lastF2 = f2; }
    }

    void setBand (int i, float thr, float ratio, float att, float rel, float mk)
    {
        band[i].set (thr, ratio, att, rel, mk, fs);
    }

    void process (juce::AudioBuffer<float>& buf)
    {
        const int nCh = juce::jmin (buf.getNumChannels(), 2);
        const int n = buf.getNumSamples();
        if (nCh == 0) return;
        float* d[2] = { buf.getWritePointer (0), nCh > 1 ? buf.getWritePointer (1) : nullptr };

        for (int i = 0; i < n; ++i)
        {
            float sig[3][2] = {};
            for (int c = 0; c < nCh; ++c)
            {
                float low, rest, mid, high, a, b;
                xo1.processSample (c, d[c][i], low, rest);
                xo2.processSample (c, rest, mid, high);
                ap.processSample  (c, low, a, b);        // all-pass compensation for the low band
                sig[0][c] = a + b;
                sig[1][c] = mid;
                sig[2][c] = high;
            }

            float out[2] = { 0.f, 0.f };
            for (int k = 0; k < 3; ++k)
            {
                float lvl = 0.f;
                for (int c = 0; c < nCh; ++c) lvl = juce::jmax (lvl, std::abs (sig[k][c]));
                const float g = band[k].process (lvl);
                for (int c = 0; c < nCh; ++c) out[c] += sig[k][c] * g;
            }
            for (int c = 0; c < nCh; ++c) d[c][i] = out[c];
        }
    }

    Band band[3];

private:
    double fs = 44100.0;
    float lastF1 = -1.f, lastF2 = -1.f;
    juce::dsp::LinkwitzRileyFilter<float> xo1, xo2, ap;
};

//==============================================================================
// 4) OUTPUT GAIN + LIMITER
//==============================================================================
class OutputLimiter
{
public:
    void prepare (double sr) { relC = std::exp (-1.f / (0.12f * (float) sr)); g = 1.f; }

    void process (juce::AudioBuffer<float>& buf, float gainDb, bool limitOn, float ceilDb)
    {
        const int nCh = juce::jmin (buf.getNumChannels(), 2);
        const int n = buf.getNumSamples();
        if (nCh == 0) return;
        const float gain = juce::Decibels::decibelsToGain (gainDb);
        const float ceil = juce::Decibels::decibelsToGain (ceilDb);
        float* d[2] = { buf.getWritePointer (0), nCh > 1 ? buf.getWritePointer (1) : nullptr };

        for (int i = 0; i < n; ++i)
        {
            float peak = 0.f;
            for (int c = 0; c < nCh; ++c) { d[c][i] *= gain; peak = juce::jmax (peak, std::abs (d[c][i])); }
            if (! limitOn) continue;

            const float target = peak > ceil ? ceil / peak : 1.f;
            if (target < g) g = target;
            else            g = relC * g + (1.f - relC) * target;
            for (int c = 0; c < nCh; ++c) d[c][i] = juce::jlimit (-ceil, ceil, d[c][i] * g);
        }
    }

private:
    float g = 1.f, relC = 0.999f;
};
