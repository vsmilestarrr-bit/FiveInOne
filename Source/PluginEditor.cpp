#include "PluginEditor.h"

FiveInOneEditor::Knob* FiveInOneEditor::addKnob (const juce::String& id, const juce::String& name,
                                                 const juce::String& suffix, juce::Colour c)
{
    knobs.push_back (std::make_unique<Knob> (proc.apvts, id, name, suffix, c, *this));
    return knobs.back().get();
}

FiveInOneEditor::FiveInOneEditor (FiveInOneProcessor& p)
    : AudioProcessorEditor (&p), proc (p), analyzer (p)
{
    setLookAndFeel (&laf);
    addAndMakeVisible (analyzer);

    const juce::Colour cyan (0xff2de2e6), pink (0xffff3cac), orange (0xffffb347), violet (0xffa77bff);

    // Auto-Tune
    atSpeed = addKnob ("at_speed", "Retune Speed", " ms", pink);
    atAmt   = addKnob ("at_amt",   "Amount", "", pink);

    // Multiband compressor
    xo1 = addKnob ("xo1", "Xover 1", " Hz", orange);
    xo2 = addKnob ("xo2", "Xover 2", " Hz", orange);
    const juce::Colour bandCol[3] = { cyan, violet, pink };
    for (int b = 0; b < 3; ++b)
    {
        comp[b][0] = addKnob (Params::cp (b, "thr"),   "Thresh",  " dB", bandCol[b]);
        comp[b][1] = addKnob (Params::cp (b, "ratio"), "Ratio",   ":1",  bandCol[b]);
        comp[b][2] = addKnob (Params::cp (b, "att"),   "Attack",  " ms", bandCol[b]);
        comp[b][3] = addKnob (Params::cp (b, "rel"),   "Release", " ms", bandCol[b]);
        comp[b][4] = addKnob (Params::cp (b, "mk"),    "Gain",    " dB", bandCol[b]);
    }

    // Output
    outGain = addKnob ("out_gain", "Output", " dB", cyan);
    limCeil = addKnob ("lim_ceil", "Ceiling", " dB", cyan);

    // Toggles
    auto setupToggle = [this] (juce::ToggleButton& b, const juce::String& text, const juce::String& id,
                               std::unique_ptr<ButtonAtt>& att)
    {
        b.setButtonText (text);
        addAndMakeVisible (b);
        att = std::make_unique<ButtonAtt> (proc.apvts, id, b);
    };
    setupToggle (atOn,  "Auto-Tune", "at_on",  atOnAtt);
    setupToggle (eqOn,  "EQ",        "eq_on",  eqOnAtt);
    setupToggle (mbOn,  "MB Comp",   "mb_on",  mbOnAtt);
    setupToggle (limOn, "Limiter",   "lim_on", limOnAtt);

    // Key / scale
    keyBox.addItemList (Params::kNotes, 1);
    scaleBox.addItemList (Params::kScales, 1);
    addAndMakeVisible (keyBox);
    addAndMakeVisible (scaleBox);
    keyAtt   = std::make_unique<ComboAtt> (proc.apvts, "at_key", keyBox);
    scaleAtt = std::make_unique<ComboAtt> (proc.apvts, "at_scale", scaleBox);

    setSize (1100, 700);
}

FiveInOneEditor::~FiveInOneEditor()
{
    setLookAndFeel (nullptr);
}

void FiveInOneEditor::paint (juce::Graphics& g)
{
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff090b11), 0.f, 0.f,
                                             juce::Colour (0xff12151f), 0.f, (float) getHeight(), false));
    g.fillAll();

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (22.f, juce::Font::bold));
    g.drawText ("FIVE-IN-ONE", 14, 10, 200, 30, juce::Justification::left);
    g.setColour (juce::Colour (0xff2de2e6));
    g.setFont (11.f);
    g.drawText ("AUTO-TUNE  /  EQ  /  VISUAL EQ  /  MULTIBAND COMP  /  LIMITER", 14, 34, 420, 12, juce::Justification::left);

    auto drawPanel = [&] (juce::Rectangle<int> r, const juce::String& title, juce::Colour c)
    {
        g.setColour (juce::Colour (0xff151922));
        g.fillRoundedRectangle (r.toFloat(), 10.f);
        g.setColour (c.withAlpha (0.35f));
        g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 10.f, 1.f);
        g.setColour (c);
        g.setFont (juce::Font (13.f, juce::Font::bold));
        g.drawText (title, r.reduced (12, 6).removeFromTop (18), juce::Justification::left);
    };

    drawPanel (panelAT,   "AUTO-TUNE",           juce::Colour (0xffff3cac));
    drawPanel (panelComp, "MULTIBAND COMPRESSOR", juce::Colour (0xffa77bff));
    drawPanel (panelOut,  "OUTPUT / LIMITER",     juce::Colour (0xff2de2e6));

    const char* names[3] = { "LOW", "MID", "HIGH" };
    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::Font (12.f, juce::Font::bold));
    for (int b = 0; b < 3; ++b)
        g.drawText (names[b], bandHeader[b], juce::Justification::centred);
}

void FiveInOneEditor::resized()
{
    auto r = getLocalBounds().reduced (10);

    auto header = r.removeFromTop (34);
    for (auto* t : { &limOn, &mbOn, &eqOn, &atOn })
        t->setBounds (header.removeFromRight (118));

    analyzer.setBounds (r.removeFromTop (300));
    r.removeFromTop (8);

    panelAT = r.removeFromLeft (250);
    r.removeFromLeft (8);
    panelOut = r.removeFromRight (230);
    r.removeFromRight (8);
    panelComp = r;

    // Auto-Tune panel
    {
        auto a = panelAT.reduced (10);
        a.removeFromTop (22);
        auto combos = a.removeFromTop (26);
        keyBox.setBounds (combos.removeFromLeft (100));
        combos.removeFromLeft (8);
        scaleBox.setBounds (combos);
        a.removeFromTop (16);
        atSpeed->place (a.removeFromLeft (105).withHeight (120));
        atAmt->place   (a.removeFromLeft (105).withHeight (120));
    }

    // Output panel
    {
        auto o = panelOut.reduced (10);
        o.removeFromTop (22);
        o.removeFromTop (16);
        outGain->place (o.removeFromLeft (100).withHeight (120));
        o.removeFromLeft (10);
        limCeil->place (o.removeFromLeft (100).withHeight (120));
    }

    // Multiband panel
    {
        auto c = panelComp.reduced (10);
        c.removeFromTop (22);
        auto xo = c.removeFromTop (92);
        xo1->place (xo.removeFromLeft (70));
        xo2->place (xo.removeFromLeft (70));

        const int colW = c.getWidth() / 3;
        for (int b = 0; b < 3; ++b)
        {
            auto col = c.removeFromLeft (colW);
            bandHeader[b] = col.removeFromTop (16);
            const int cw = col.getWidth() / 3;
            auto row1 = col.removeFromTop (92);
            auto row2 = col.removeFromTop (92);
            comp[b][0]->place (row1.removeFromLeft (cw));
            comp[b][1]->place (row1.removeFromLeft (cw));
            comp[b][2]->place (row1.removeFromLeft (cw));
            comp[b][3]->place (row2.removeFromLeft (cw));
            comp[b][4]->place (row2.removeFromLeft (cw));
        }
    }
}
