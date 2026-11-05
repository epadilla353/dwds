#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <atomic>
#include "Biquad.h"

static constexpr int kNumBands = 8;
static constexpr int kChunk = 32;   // coefficient update interval (samples)
static constexpr int kScopeSize = 16384;

class SesEqProcessor : public juce::AudioProcessor
{
public:
    SesEqProcessor();
    ~SesEqProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "SES EQ"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // shared with the UI
    std::array<float, kScopeSize> scope {};     // post-EQ mono (spectrum display)
    std::array<float, kScopeSize> inScope {};   // pre-EQ mono (AI analysis)
    std::atomic<int> scopePos { 0 };
    std::array<std::atomic<float>, kNumBands> liveGainDb;
    std::atomic<double> currentSampleRate { 44100.0 };

private:
    struct Band
    {
        Biquad filt[2];
        Biquad det;
        double env = 0.0;
        float smoothGain = 0.0f;
        std::atomic<float> *on = nullptr, *type = nullptr, *freq = nullptr, *gain = nullptr, *q = nullptr,
                           *dyn = nullptr, *thr = nullptr, *range = nullptr, *att = nullptr, *rel = nullptr;
    };
    std::array<Band, kNumBands> bands;
    std::atomic<float>* outGain = nullptr;
    juce::AudioBuffer<float> monoIn;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SesEqProcessor)
};
