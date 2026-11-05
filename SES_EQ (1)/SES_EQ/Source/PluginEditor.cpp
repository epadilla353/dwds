#include "PluginEditor.h"

namespace
{
    constexpr float kMinF = 20.f, kMaxF = 20000.f, kRangeDb = 24.f;
    const char* kIds[7]   = { "freq", "gain", "q", "thr", "range", "att", "rel" };
    const char* kNames[7] = { "Freq", "Gain", "Q", "Threshold", "Dyn Range", "Attack", "Release" };
    const char* kSuffix[7] = { " Hz", " dB", "", " dB", " dB", " ms", " ms" };
    juce::Colour bandColour (int i) { return juce::Colour::fromHSV ((float) i / 8.0f, 0.65f, 0.95f, 1.0f); }
}

SesEqEditor::SesEqEditor (SesEqProcessor& p)
    : AudioProcessorEditor (&p), proc (p), apvts (p.apvts)
{
    setSize (920, 600);

    for (size_t i = 0; i < knobs.size(); ++i)
    {
        auto& k = knobs[i];
        k.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
        k.slider.setTextValueSuffix (kSuffix[i]);
        k.label.setText (kNames[i], juce::dontSendNotification);
        k.label.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (k.slider);
        addAndMakeVisible (k.label);
    }

    typeBox.addItemList (bandTypeNames(), 1);
    addAndMakeVisible (typeBox);
    addAndMakeVisible (onBtn);
    addAndMakeVisible (dynBtn);

    outKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    outKnob.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 74, 18);
    outKnob.setTextValueSuffix (" dB");
    outLabel.setText ("Output", juce::dontSendNotification);
    outLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (outKnob);
    addAndMakeVisible (outLabel);
    outAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, "out_gain", outKnob);

    aiBtn.onClick = [this] { if (listening) finishScan(); else startScan(); };
    undoBtn.onClick = [this] { undoAi(); };
    intensitySlider.setSliderStyle (juce::Slider::LinearHorizontal);
    intensitySlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    intensitySlider.setRange (0.0, 100.0, 1.0);
    intensitySlider.setValue (70.0);
    intensitySlider.setPopupDisplayEnabled (true, true, this);
    intensitySlider.setTextValueSuffix (" %");
    intensityLabel.setText ("AI Intensity", juce::dontSendNotification);
    intensityLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (aiBtn);
    addAndMakeVisible (undoBtn);
    addAndMakeVisible (intensitySlider);
    addAndMakeVisible (intensityLabel);

    selectBand (0);
    startTimerHz (30);
}

SesEqEditor::~SesEqEditor() { stopTimer(); }

// ---------------------------------------------------------------- helpers
float SesEqEditor::xForFreq (float f) const
{
    return graph.getX() + graph.getWidth() * std::log (f / kMinF) / std::log (kMaxF / kMinF);
}
float SesEqEditor::freqForX (float x) const
{
    const float t = juce::jlimit (0.f, 1.f, (x - graph.getX()) / graph.getWidth());
    return kMinF * std::pow (kMaxF / kMinF, t);
}
float SesEqEditor::yForGain (float g) const { return graph.getCentreY() - g / kRangeDb * graph.getHeight() * 0.5f; }
float SesEqEditor::gainForY (float y) const { return (graph.getCentreY() - y) / (graph.getHeight() * 0.5f) * kRangeDb; }

float SesEqEditor::readParam (int band, const char* s) const
{
    return apvts.getRawParameterValue (pid (band, s))->load();
}

void SesEqEditor::setParam (const juce::String& id, float v)
{
    if (auto* p = apvts.getParameter (id)) p->setValueNotifyingHost (p->convertTo0to1 (v));
}

void SesEqEditor::selectBand (int b)
{
    selected = b;
    for (size_t i = 0; i < knobs.size(); ++i)
        knobs[i].att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (apvts, pid (b, kIds[i]), knobs[i].slider);
    typeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (apvts, pid (b, "type"), typeBox);
    onAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>   (apvts, pid (b, "on"), onBtn);
    dynAtt  = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>   (apvts, pid (b, "dyn"), dynBtn);
    for (auto& k : knobs) k.slider.setColour (juce::Slider::rotarySliderFillColourId, bandColour (b));
    repaint();
}

int SesEqEditor::hitTest (juce::Point<float> p) const
{
    for (int b = kNumBands - 1; b >= 0; --b)
    {
        if (readParam (b, "on") < 0.5f) continue;
        const int type = (int) readParam (b, "type");
        const float x = xForFreq (readParam (b, "freq"));
        const float y = typeUsesGain (type) ? yForGain (readParam (b, "gain")) : graph.getCentreY();
        if (p.getDistanceFrom ({ x, y }) < 11.f) return b;
    }
    return -1;
}

// ---------------------------------------------------------------- mouse
void SesEqEditor::mouseDown (const juce::MouseEvent& e)
{
    dragging = hitTest (e.position);
    if (dragging >= 0)
    {
        selectBand (dragging);
        for (auto s : { "freq", "gain" })
            if (auto* p = apvts.getParameter (pid (dragging, s))) p->beginChangeGesture();
    }
}

void SesEqEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragging < 0) return;
    const auto pos = e.position.constrainedWithin (graph);
    setParam (pid (dragging, "freq"), freqForX (pos.x));
    if (typeUsesGain ((int) readParam (dragging, "type")))
        setParam (pid (dragging, "gain"), juce::jlimit (-24.f, 24.f, gainForY (pos.y)));
}

void SesEqEditor::mouseUp (const juce::MouseEvent&)
{
    if (dragging >= 0)
        for (auto s : { "freq", "gain" })
            if (auto* p = apvts.getParameter (pid (dragging, s))) p->endChangeGesture();
    dragging = -1;
}

void SesEqEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitTest (e.position);
    if (hit >= 0) { setParam (pid (hit, "on"), 0.f); return; }
    if (! graph.contains (e.position)) return;

    for (int b = 0; b < kNumBands; ++b)
    {
        if (readParam (b, "on") > 0.5f) continue;
        setParam (pid (b, "type"), (float) Bell);
        setParam (pid (b, "freq"), freqForX (e.position.x));
        setParam (pid (b, "gain"), gainForY (e.position.y));
        setParam (pid (b, "q"), 1.0f);
        setParam (pid (b, "on"), 1.f);
        selectBand (b);
        return;
    }
}

void SesEqEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int hit = hitTest (e.position);
    if (hit < 0) return;
    setParam (pid (hit, "q"), readParam (hit, "q") * std::exp (w.deltaY * 2.0f));
}

// ---------------------------------------------------------------- spectrum
void SesEqEditor::updateSpectrum()
{
    const int pos = proc.scopePos.load();
    for (int i = 0; i < 2048; ++i)
        fftData[(size_t) i] = proc.scope[(size_t) ((pos - 2048 + i) & (kScopeSize - 1))];
    std::fill (fftData.begin() + 2048, fftData.end(), 0.f);
    window.multiplyWithWindowingTable (fftData.data(), 2048);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (size_t k = 0; k < spec.size(); ++k)
    {
        const float lvl = juce::Decibels::gainToDecibels (fftData[k] / 512.f, -120.f);
        spec[k] += (lvl - spec[k]) * (lvl > spec[k] ? 0.5f : 0.15f);
    }
}

void SesEqEditor::timerCallback()
{
    updateSpectrum();
    if (listening) collectAiFrame();
    repaint();
}

// ---------------------------------------------------------------- AI mode
void SesEqEditor::startScan()
{
    analyzer.reset();
    listening = true;
    aiBtn.setButtonText ("Apply now");
    aiStatus = "Listening... play the vocal (0%)";
}

void SesEqEditor::collectAiFrame()
{
    const int pos = proc.scopePos.load();
    const int n = AutoEqAnalyzer::kFftSize;
    for (int i = 0; i < n; ++i)
        aiFrame[(size_t) i] = proc.inScope[(size_t) ((pos - n + i) & (kScopeSize - 1))];
    analyzer.addFrame (aiFrame.data());

    const int frames = analyzer.numFrames();
    aiStatus = "Listening... play the vocal (" + juce::String (juce::jmin (100, frames * 100 / kAiTargetFrames)) + "%)  -  press Apply now to finish early";
    if (frames >= kAiTargetFrames) finishScan();
}

void SesEqEditor::finishScan()
{
    listening = false;
    aiBtn.setButtonText ("AI Scan");
    const auto res = analyzer.compute (proc.currentSampleRate.load(), (float) intensitySlider.getValue() / 100.f);
    if (! res.valid) { aiStatus = res.summary; return; }

    prevState = apvts.copyState();
    applyResult (res);
    aiStatus = "AI EQ applied: " + res.summary;
}

void SesEqEditor::applyResult (const AutoEqResult& r)
{
    for (int b = 0; b < kNumBands; ++b)
    {
        const auto& bd = r.bands[(size_t) b];
        setParam (pid (b, "on"), bd.on ? 1.f : 0.f);
        if (! bd.on) continue;
        setParam (pid (b, "type"), (float) bd.type);
        setParam (pid (b, "freq"), bd.freq);
        setParam (pid (b, "gain"), bd.gain);
        setParam (pid (b, "q"), bd.q);
        setParam (pid (b, "dyn"), bd.dyn ? 1.f : 0.f);
        setParam (pid (b, "thr"), bd.thr);
        setParam (pid (b, "range"), bd.range);
        setParam (pid (b, "att"), bd.att);
        setParam (pid (b, "rel"), bd.rel);
    }
    selectBand (1);
}

void SesEqEditor::undoAi()
{
    if (! prevState.isValid()) { aiStatus = "Nothing to undo."; return; }
    apvts.replaceState (prevState);
    prevState = {};
    aiStatus = "AI changes undone.";
}

// ---------------------------------------------------------------- layout
void SesEqEditor::resized()
{
    auto r = getLocalBounds().reduced (10);
    auto header = r.removeFromTop (28);
    r.removeFromTop (22);   // status strip
    aiBtn.setBounds (header.removeFromRight (110).reduced (2));
    undoBtn.setBounds (header.removeFromRight (84).reduced (2));
    intensitySlider.setBounds (header.removeFromRight (120));
    intensityLabel.setBounds (header.removeFromRight (80));
    auto panel = r.removeFromBottom (165);
    graph = r.reduced (0, 6).toFloat();

    auto left = panel.removeFromLeft (110);
    typeBox.setBounds (left.removeFromTop (30).reduced (2));
    onBtn.setBounds (left.removeFromTop (30));
    dynBtn.setBounds (left.removeFromTop (30));

    auto outArea = panel.removeFromRight (90);
    outLabel.setBounds (outArea.removeFromTop (18));
    outKnob.setBounds (outArea);

    const int w = panel.getWidth() / 7;
    for (auto& k : knobs)
    {
        auto a = panel.removeFromLeft (w);
        k.label.setBounds (a.removeFromTop (18));
        k.slider.setBounds (a);
    }
}

// ---------------------------------------------------------------- paint
void SesEqEditor::paint (juce::Graphics& g)
{
    using namespace juce;
    g.fillAll (Colour (0xff14161a));
    g.setColour (Colours::white);
    g.setFont (22.0f);
    g.drawText ("SES EQ", 14, 6, 200, 28, Justification::left);
    g.setFont (12.0f);
    g.setColour (listening ? Colours::orange : Colours::white.withAlpha (0.65f));
    g.drawText (aiStatus, 14, 36, getWidth() - 28, 18, Justification::left);
    g.setFont (22.0f);

    g.setColour (Colour (0xff1b1e24));
    g.fillRoundedRectangle (graph, 6.f);

    Graphics::ScopedSaveState ss (g);
    g.reduceClipRegion (graph.toNearestInt());

    // grid
    g.setFont (11.0f);
    const float fl[] = { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (float f : fl)
    {
        const float x = xForFreq (f);
        g.setColour (Colours::white.withAlpha (0.07f));
        g.drawVerticalLine ((int) x, graph.getY(), graph.getBottom());
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawText (f >= 1000 ? String (f / 1000, f == 1000 || f == 2000 || f == 5000 || f == 10000 || f == 20000 ? 0 : 1) + "k" : String ((int) f),
                    (int) x + 2, (int) graph.getBottom() - 16, 40, 14, Justification::left);
    }
    for (int db = -24; db <= 24; db += 6)
    {
        const float y = yForGain ((float) db);
        g.setColour (Colours::white.withAlpha (db == 0 ? 0.2f : 0.07f));
        g.drawHorizontalLine ((int) y, graph.getX(), graph.getRight());
        g.setColour (Colours::white.withAlpha (0.35f));
        g.drawText (String (db), (int) graph.getX() + 3, (int) y - 12, 30, 12, Justification::left);
    }

    // spectrum
    const double fs = proc.currentSampleRate.load();
    Path sp;
    bool started = false;
    for (size_t k = 2; k < spec.size(); ++k)
    {
        const double f = (double) k * fs / 2048.0;
        if (f < kMinF || f > kMaxF) continue;
        const float x = xForFreq ((float) f);
        const float y = jmap (spec[k], -100.f, 0.f, graph.getBottom(), graph.getY());
        if (! started) { sp.startNewSubPath (x, graph.getBottom()); sp.lineTo (x, y); started = true; }
        else sp.lineTo (x, y);
    }
    if (started)
    {
        sp.lineTo (graph.getRight(), graph.getBottom());
        sp.closeSubPath();
        g.setColour (Colour (0xff4aa3ff).withAlpha (0.22f));
        g.fillPath (sp);
    }

    // snapshot bands + response curve
    BiquadCoeffs co[kNumBands];
    bool on[kNumBands];
    for (int b = 0; b < kNumBands; ++b)
    {
        on[b] = readParam (b, "on") > 0.5f;
        const int type = (int) readParam (b, "type");
        co[b] = makeCoeffs (type, fs, readParam (b, "freq"), typeUsesGain (type) ? proc.liveGainDb[(size_t) b].load() : 0.0, readParam (b, "q"));
    }
    Path curve;
    for (float px = 0; px <= graph.getWidth(); px += 2.f)
    {
        const float x = graph.getX() + px;
        const float f = freqForX (x);
        double db = 0.0;
        for (int b = 0; b < kNumBands; ++b) if (on[b]) db += magnitudeDb (co[b], fs, f);
        const float y = yForGain (jlimit (-kRangeDb, kRangeDb, (float) db));
        if (px == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
    }
    g.setColour (Colours::white.withAlpha (0.9f));
    g.strokePath (curve, PathStrokeType (2.0f));

    // nodes
    for (int b = 0; b < kNumBands; ++b)
    {
        if (! on[b]) continue;
        const int type = (int) readParam (b, "type");
        const float x = xForFreq (readParam (b, "freq"));
        const float yBase = typeUsesGain (type) ? yForGain (readParam (b, "gain")) : graph.getCentreY();
        const auto col = bandColour (b);

        if (readParam (b, "dyn") > 0.5f && typeCanBeDynamic (type))
        {
            const float yEnd = yForGain (readParam (b, "gain") + readParam (b, "range"));
            g.setColour (col.withAlpha (0.6f));
            g.drawLine (x, yBase, x, yEnd, 2.f);
            g.drawEllipse (x - 5, yEnd - 5, 10, 10, 1.5f);
            g.setColour (Colours::white);
            g.fillEllipse (x - 3, yForGain (proc.liveGainDb[(size_t) b].load()) - 3, 6, 6);
        }
        g.setColour (col);
        g.fillEllipse (x - 7, yBase - 7, 14, 14);
        if (b == selected) { g.setColour (Colours::white); g.drawEllipse (x - 10, yBase - 10, 20, 20, 2.f); }
        g.setColour (Colours::black);
        g.setFont (10.0f);
        g.drawText (String (b + 1), (int) x - 7, (int) yBase - 7, 14, 14, Justification::centred);
    }
}
