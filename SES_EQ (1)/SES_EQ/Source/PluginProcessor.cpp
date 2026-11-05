#include "PluginProcessor.h"
#include "PluginEditor.h"

static const int   kDefType[kNumBands] = { LowCut, LowShelf, Bell, Bell, Bell, Bell, HighShelf, HighCut };
static const float kDefFreq[kNumBands] = { 40.f, 120.f, 300.f, 800.f, 2000.f, 5000.f, 9000.f, 16000.f };

juce::AudioProcessorValueTreeState::ParameterLayout SesEqProcessor::createLayout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> p;

    for (int i = 0; i < kNumBands; ++i)
    {
        auto id = [i] (const char* s) { return ParameterID { "b" + String (i) + "_" + s, 1 }; };
        const String nm = "Band " + String (i + 1) + " ";

        NormalisableRange<float> fr (20.f, 20000.f, 0.01f);   fr.setSkewForCentre (1000.f);
        NormalisableRange<float> qr (0.1f, 18.f, 0.01f);      qr.setSkewForCentre (1.f);
        NormalisableRange<float> ar (0.1f, 500.f, 0.01f);     ar.setSkewForCentre (20.f);
        NormalisableRange<float> rr (5.f, 2000.f, 0.01f);     rr.setSkewForCentre (100.f);

        const float defQ = (kDefType[i] == Bell || kDefType[i] == Notch) ? 1.0f : 0.71f;

        p.push_back (std::make_unique<AudioParameterBool>  (id ("on"),   nm + "On", false));
        p.push_back (std::make_unique<AudioParameterChoice>(id ("type"), nm + "Type", bandTypeNames(), kDefType[i]));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("freq"), nm + "Freq", fr, kDefFreq[i]));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("gain"), nm + "Gain", NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.f));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("q"),    nm + "Q", qr, defQ));
        p.push_back (std::make_unique<AudioParameterBool>  (id ("dyn"),  nm + "Dynamic", false));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("thr"),  nm + "Threshold", NormalisableRange<float> (-60.f, 0.f, 0.1f), -30.f));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("range"),nm + "Range", NormalisableRange<float> (-24.f, 24.f, 0.1f), -6.f));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("att"),  nm + "Attack", ar, 10.f));
        p.push_back (std::make_unique<AudioParameterFloat> (id ("rel"),  nm + "Release", rr, 100.f));
    }
    p.push_back (std::make_unique<AudioParameterFloat> (ParameterID { "out_gain", 1 }, "Output Gain",
                                                        NormalisableRange<float> (-24.f, 24.f, 0.01f), 0.f));
    return { p.begin(), p.end() };
}

SesEqProcessor::SesEqProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                       .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SES_EQ", createLayout())
{
    for (int b = 0; b < kNumBands; ++b)
    {
        auto g = [&] (const char* s) { return apvts.getRawParameterValue ("b" + juce::String (b) + "_" + s); };
        auto& bd = bands[(size_t) b];
        bd.on = g ("on"); bd.type = g ("type"); bd.freq = g ("freq"); bd.gain = g ("gain"); bd.q = g ("q");
        bd.dyn = g ("dyn"); bd.thr = g ("thr"); bd.range = g ("range"); bd.att = g ("att"); bd.rel = g ("rel");
        liveGainDb[(size_t) b].store (0.f);
    }
    outGain = apvts.getRawParameterValue ("out_gain");
}

bool SesEqProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    return l.getMainInputChannelSet() == juce::AudioChannelSet::stereo()
        && l.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void SesEqProcessor::prepareToPlay (double sr, int maxBlock)
{
    currentSampleRate.store (sr);
    monoIn.setSize (1, juce::jmax (maxBlock, 512));
    for (auto& b : bands)
    {
        b.filt[0].reset(); b.filt[1].reset(); b.det.reset();
        b.env = 0.0; b.smoothGain = 0.f;
    }
}

void SesEqProcessor::processBlock (juce::AudioBuffer<float>& buf, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int nCh = juce::jmin (buf.getNumChannels(), 2);
    const int n = buf.getNumSamples();
    const double fs = currentSampleRate.load();

    if (monoIn.getNumSamples() < n) monoIn.setSize (1, n, false, false, true);
    float* mono = monoIn.getWritePointer (0);
    const int pos = scopePos.load();
    for (int i = 0; i < n; ++i)
    {
        float s = 0.f;
        for (int c = 0; c < nCh; ++c) s += buf.getReadPointer (c)[i];
        mono[i] = s / (float) juce::jmax (1, nCh);
        inScope[(size_t) ((pos + i) & (kScopeSize - 1))] = mono[i];
    }

    for (int start = 0; start < n; start += kChunk)
    {
        const int len = juce::jmin (kChunk, n - start);

        for (int b = 0; b < kNumBands; ++b)
        {
            auto& bd = bands[(size_t) b];
            if (bd.on->load() < 0.5f) { liveGainDb[(size_t) b].store (0.f); continue; }

            const int type = (int) bd.type->load();
            const float freq = bd.freq->load(), gain = bd.gain->load(), q = bd.q->load();
            float target = typeUsesGain (type) ? gain : 0.f;

            // ---- dynamic section: band-limited detector -> envelope -> gain offset
            if (bd.dyn->load() > 0.5f && typeCanBeDynamic (type))
            {
                int detType = BandPass;
                if (type == LowShelf)  detType = HighCut;   // low-pass detector
                if (type == HighShelf) detType = LowCut;    // high-pass detector
                bd.det.c = makeCoeffs (detType, fs, freq, 0.0, juce::jmax (q, 0.7f));

                const double attC = std::exp (-1.0 / (juce::jmax (0.05f, bd.att->load()) * 0.001 * fs));
                const double relC = std::exp (-1.0 / (juce::jmax (1.0f, bd.rel->load()) * 0.001 * fs));
                for (int i = 0; i < len; ++i)
                {
                    const double r = std::abs ((double) bd.det.process (mono[start + i]));
                    const double coef = r > bd.env ? attC : relC;
                    bd.env = coef * bd.env + (1.0 - coef) * r;
                }
                const float levelDb = juce::Decibels::gainToDecibels ((float) bd.env, -120.f);
                const float t = juce::jlimit (0.f, 1.f, (levelDb - bd.thr->load()) / 12.f);
                target += bd.range->load() * t;
            }

            bd.smoothGain += 0.3f * (target - bd.smoothGain);
            liveGainDb[(size_t) b].store (bd.smoothGain);

            const auto c = makeCoeffs (type, fs, freq, bd.smoothGain, q);
            for (int ch = 0; ch < nCh; ++ch)
            {
                bd.filt[ch].c = c;
                float* d = buf.getWritePointer (ch) + start;
                for (int i = 0; i < len; ++i) d[i] = bd.filt[ch].process (d[i]);
            }
        }
    }

    buf.applyGain (juce::Decibels::decibelsToGain (outGain->load()));

    for (int i = 0; i < n; ++i)
    {
        float s = 0.f;
        for (int c = 0; c < nCh; ++c) s += buf.getReadPointer (c)[i];
        scope[(size_t) ((pos + i) & (kScopeSize - 1))] = s / (float) juce::jmax (1, nCh);
    }
    scopePos.store ((pos + n) & (kScopeSize - 1));
}

juce::AudioProcessorEditor* SesEqProcessor::createEditor() { return new SesEqEditor (*this); }

void SesEqProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    if (auto xml = apvts.copyState().createXml()) copyXmlToBinary (*xml, dest);
}

void SesEqProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new SesEqProcessor(); }
