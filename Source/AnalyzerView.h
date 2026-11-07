#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "Params.h"
#include "DSP.h"

// Large "pretty EQ": live spectrum + EQ response curve + draggable band nodes.
//  - drag a node: frequency / gain
//  - mouse wheel over a node: Q
//  - double-click a node: reset gain to 0
class AnalyzerView : public juce::Component, private juce::Timer
{
public:
    explicit AnalyzerView (FiveInOneProcessor& p) : proc (p)
    {
        startTimerHz (30);
    }
    ~AnalyzerView() override { stopTimer(); }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0b0e16), 0.f, 0.f,
                                                 juce::Colour (0xff151b2e), 0.f, bounds.getHeight(), false));
        g.fillRoundedRectangle (bounds, 10.f);

        const auto plot = getPlot();
        drawGrid (g, plot);
        drawSpectrum (g, plot);
        drawCurve (g, plot);
        drawNodes (g, plot);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        const int h = hitTest (e.position);
        if (h != hoverBand) { hoverBand = h; repaint(); }
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragBand = hitTest (e.position);
        if (dragBand >= 0)
        {
            if (auto* p = proc.apvts.getParameter (Params::eqFreq (dragBand))) p->beginChangeGesture();
            if (auto* p = proc.apvts.getParameter (Params::eqGain (dragBand))) p->beginChangeGesture();
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragBand < 0) return;
        const auto plot = getPlot();
        setParam (Params::eqFreq (dragBand), xToFreq (e.position.x, plot));
        setParam (Params::eqGain (dragBand), yToGain (e.position.y, plot));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragBand >= 0)
        {
            if (auto* p = proc.apvts.getParameter (Params::eqFreq (dragBand))) p->endChangeGesture();
            if (auto* p = proc.apvts.getParameter (Params::eqGain (dragBand))) p->endChangeGesture();
            dragBand = -1;
        }
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const int b = hitTest (e.position);
        if (b >= 0)
        {
            if (auto* p = proc.apvts.getParameter (Params::eqGain (b)))
            {
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (0.f));
                p->endChangeGesture();
            }
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        const int b = hitTest (e.position);
        if (b < 0) return;
        if (auto* p = proc.apvts.getParameter (Params::eqQ (b)))
        {
            const float q = proc.apvts.getRawParameterValue (Params::eqQ (b))->load();
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (q * std::exp (w.deltaY * 0.8f)));
            p->endChangeGesture();
        }
    }

private:
    static constexpr int N = FiveInOneProcessor::fftSize;
    static constexpr float kDbRange = 18.f;      // EQ display +-18 dB

    struct BandVals { float f, g, q; };

    static juce::Colour bandColour (int i)
    {
        static const juce::uint32 c[6] = { 0xff2de2e6, 0xff4facfe, 0xff7f7fff, 0xffc471f5, 0xffff3cac, 0xffffb347 };
        return juce::Colour (c[i]);
    }

    BandVals getBand (int b) const
    {
        auto get = [this] (const juce::String& id) { return proc.apvts.getRawParameterValue (id)->load(); };
        return { get (Params::eqFreq (b)), get (Params::eqGain (b)), get (Params::eqQ (b)) };
    }

    double getSR() const { return juce::jmax (8000.0, proc.getSampleRate()); }

    juce::Rectangle<float> getPlot() const
    {
        return getLocalBounds().toFloat().reduced (12.f, 10.f).withTrimmedLeft (26.f).withTrimmedBottom (14.f);
    }

    static float freqToX (float f, juce::Rectangle<float> r)
    {
        return r.getX() + r.getWidth() * std::log (juce::jmax (20.f, f) / 20.f) / std::log (1000.f);
    }
    static float xToFreq (float x, juce::Rectangle<float> r)
    {
        const float t = juce::jlimit (0.f, 1.f, (x - r.getX()) / r.getWidth());
        return 20.f * std::pow (1000.f, t);
    }
    static float gainToY (float db, juce::Rectangle<float> r)
    {
        db = juce::jlimit (-kDbRange, kDbRange, db);
        return r.getY() + r.getHeight() * (0.5f - db / (2.f * kDbRange));
    }
    static float yToGain (float y, juce::Rectangle<float> r)
    {
        const float t = juce::jlimit (0.f, 1.f, (y - r.getY()) / r.getHeight());
        return (0.5f - t) * 2.f * kDbRange;
    }

    int hitTest (juce::Point<float> p) const
    {
        const auto plot = getPlot();
        for (int b = 0; b < Params::kNumEq; ++b)
        {
            const auto v = getBand (b);
            const juce::Point<float> pos (freqToX (v.f, plot), gainToY (v.g, plot));
            if (pos.getDistanceFrom (p) < 12.f) return b;
        }
        return -1;
    }

    void setParam (const juce::String& id, float value)
    {
        if (auto* p = proc.apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    //==========================================================================
    void timerCallback() override
    {
        if (proc.pullFFTBlock (block))
        {
            std::copy (block.begin(), block.end(), fftData.begin());
            std::fill (fftData.begin() + N, fftData.end(), 0.f);
            window.multiplyWithWindowingTable (fftData.data(), (size_t) N);
            fft.performFrequencyOnlyForwardTransform (fftData.data());
            updateSpectrum();
        }
        repaint();
    }

    void updateSpectrum()
    {
        const auto plot = getPlot();
        const int w = (int) plot.getWidth();
        if (w <= 0) return;
        if ((int) spec.size() != w) spec.assign ((size_t) w, -100.f);

        const float sr = (float) getSR();
        const float norm = juce::Decibels::gainToDecibels ((float) N);

        for (int x = 0; x < w; ++x)
        {
            const float f0 = 20.f * std::pow (1000.f, (float) x / (float) w);
            const float f1 = 20.f * std::pow (1000.f, (float) (x + 1) / (float) w);
            const float b0 = f0 * (float) N / sr, b1 = f1 * (float) N / sr;

            float mag = 0.f;
            if (b1 - b0 < 1.f)
            {
                const float bc = 0.5f * (b0 + b1);
                const int i0 = juce::jlimit (0, N / 2 - 2, (int) bc);
                const float fr = bc - (float) i0;
                mag = fftData[(size_t) i0] + fr * (fftData[(size_t) i0 + 1] - fftData[(size_t) i0]);
            }
            else
            {
                const int i0 = juce::jlimit (0, N / 2 - 1, (int) std::ceil (b0));
                const int i1 = juce::jlimit (0, N / 2 - 1, (int) std::floor (b1));
                for (int i = i0; i <= i1; ++i) mag = juce::jmax (mag, fftData[(size_t) i]);
            }

            float db = juce::Decibels::gainToDecibels (mag) - norm;
            db += 3.f * std::log2 (std::sqrt (f0 * f1) / 1000.f);      // +3 dB/oct tilt: pink noise looks flat

            float& s = spec[(size_t) x];
            s += (db - s) * (db > s ? 0.6f : 0.12f);
        }
    }

    //==========================================================================
    void drawGrid (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        g.setFont (11.f);
        const float freqs[] = { 50, 100, 200, 500, 1000, 2000, 5000, 10000 };
        for (float f : freqs)
        {
            const float x = freqToX (f, plot);
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawVerticalLine ((int) x, plot.getY(), plot.getBottom());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            const juce::String t = f >= 1000.f ? juce::String ((int) (f / 1000.f)) + "k" : juce::String ((int) f);
            g.drawText (t, juce::Rectangle<int> ((int) x - 20, (int) plot.getBottom() + 1, 40, 12), juce::Justification::centred);
        }
        for (int db = -18; db <= 18; db += 6)
        {
            const float y = gainToY ((float) db, plot);
            g.setColour (juce::Colours::white.withAlpha (db == 0 ? 0.18f : 0.06f));
            g.drawHorizontalLine ((int) y, plot.getX(), plot.getRight());
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawText (juce::String (db), juce::Rectangle<int> (2, (int) y - 6, 24, 12), juce::Justification::centredRight);
        }
    }

    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        if (spec.empty()) return;
        auto dbToY = [&] (float db) { return plot.getBottom() - plot.getHeight() * juce::jlimit (0.f, 1.f, (db + 85.f) / 80.f); };

        juce::Path line;
        for (size_t i = 0; i < spec.size(); ++i)
        {
            const float x = plot.getX() + (float) i;
            const float y = dbToY (spec[i]);
            if (i == 0) line.startNewSubPath (x, y); else line.lineTo (x, y);
        }
        juce::Path fill (line);
        fill.lineTo (plot.getX() + (float) spec.size() - 1.f, plot.getBottom());
        fill.lineTo (plot.getX(), plot.getBottom());
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xffff3cac).withAlpha (0.55f), 0.f, plot.getY(),
                                                 juce::Colour (0xff2de2e6).withAlpha (0.04f), 0.f, plot.getBottom(), false));
        g.fillPath (fill);
        g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2de2e6), plot.getX(), 0.f,
                                                 juce::Colour (0xffff3cac), plot.getRight(), 0.f, false));
        g.strokePath (line, juce::PathStrokeType (1.5f));
    }

    void drawCurve (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        const double sr = getSR();
        const bool on = proc.apvts.getRawParameterValue ("eq_on")->load() > 0.5f;

        ParametricEQ::Coeffs::Ptr co[Params::kNumEq];
        for (int b = 0; b < Params::kNumEq; ++b)
        {
            const auto v = getBand (b);
            co[b] = ParametricEQ::makeCoeffs (b, sr, v.f, v.g, v.q);
        }

        juce::Graphics::ScopedSaveState ss (g);
        g.reduceClipRegion (plot.toNearestInt());

        juce::Path p;
        for (float x = 0.f; x <= plot.getWidth(); x += 2.f)
        {
            const float f = xToFreq (plot.getX() + x, plot);
            float db = 0.f;
            for (int b = 0; b < Params::kNumEq; ++b)
                db += (float) juce::Decibels::gainToDecibels (co[b]->getMagnitudeForFrequency ((double) f, sr));
            const float y = gainToY (db, plot);
            if (x == 0.f) p.startNewSubPath (plot.getX(), y); else p.lineTo (plot.getX() + x, y);
        }

        const float alpha = on ? 1.f : 0.25f;
        juce::Path fill (p);
        const float cy = gainToY (0.f, plot);
        fill.lineTo (plot.getRight(), cy);
        fill.lineTo (plot.getX(), cy);
        fill.closeSubPath();
        g.setColour (juce::Colour (0xff4facfe).withAlpha (0.12f * alpha));
        g.fillPath (fill);

        g.setColour (juce::Colour (0xff4facfe).withAlpha (0.25f * alpha));
        g.strokePath (p, juce::PathStrokeType (6.f));
        g.setColour (juce::Colours::white.withAlpha (0.95f * alpha));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }

    void drawNodes (juce::Graphics& g, juce::Rectangle<float> plot)
    {
        for (int b = 0; b < Params::kNumEq; ++b)
        {
            const auto v = getBand (b);
            const float x = freqToX (v.f, plot), y = gainToY (v.g, plot);
            const bool sel = (b == hoverBand || b == dragBand);
            const float r = sel ? 9.f : 7.f;

            g.setColour (bandColour (b).withAlpha (0.25f));
            g.fillEllipse (x - r - 4.f, y - r - 4.f, 2.f * (r + 4.f), 2.f * (r + 4.f));
            g.setColour (bandColour (b));
            g.fillEllipse (x - r, y - r, 2.f * r, 2.f * r);
            g.setColour (sel ? juce::Colours::white : juce::Colour (0xff0b0e16));
            g.drawEllipse (x - r, y - r, 2.f * r, 2.f * r, 1.5f);
            g.setColour (juce::Colour (0xff0b0e16));
            g.setFont (juce::Font (10.f, juce::Font::bold));
            g.drawText (juce::String (b + 1), juce::Rectangle<float> (x - r, y - r, 2.f * r, 2.f * r), juce::Justification::centred);
        }

        const int info = dragBand >= 0 ? dragBand : hoverBand;
        if (info >= 0)
        {
            const auto v = getBand (info);
            const juce::String fs = v.f >= 1000.f ? juce::String (v.f / 1000.f, 2) + " kHz" : juce::String ((int) v.f) + " Hz";
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.setFont (13.f);
            g.drawText (fs + "   " + juce::String (v.g, 1) + " dB   Q " + juce::String (v.q, 2),
                        juce::Rectangle<int> ((int) plot.getX() + 6, (int) plot.getY() + 4, 300, 16), juce::Justification::left);
        }
    }

    //==========================================================================
    FiveInOneProcessor& proc;
    juce::dsp::FFT fft { FiveInOneProcessor::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) N, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, N> block {};
    std::array<float, 2 * N> fftData {};
    std::vector<float> spec;
    int dragBand = -1, hoverBand = -1;
};
