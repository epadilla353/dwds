#pragma once
#include "PluginProcessor.h"
#include "AutoEq.h"

class SesEqEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit SesEqEditor (SesEqProcessor&);
    ~SesEqEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

private:
    void timerCallback() override;
    void updateSpectrum();
    void startScan();
    void finishScan();
    void collectAiFrame();
    void applyResult (const AutoEqResult&);
    void undoAi();
    void selectBand (int b);
    int  hitTest (juce::Point<float> p) const;
    void setParam (const juce::String& id, float realValue);
    float readParam (int band, const char* s) const;
    juce::String pid (int band, const char* s) const { return "b" + juce::String (band) + "_" + s; }

    float xForFreq (float f) const;
    float freqForX (float x) const;
    float yForGain (float g) const;
    float gainForY (float y) const;

    SesEqProcessor& proc;
    juce::AudioProcessorValueTreeState& apvts;
    juce::Rectangle<float> graph;
    int selected = 0, dragging = -1;

    juce::dsp::FFT fft { 11 };
    juce::dsp::WindowingFunction<float> window { 2048, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, 4096> fftData {};
    std::array<float, 1024> spec {};

    struct Knob
    {
        juce::Label label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };
    std::array<Knob, 7> knobs;
    juce::ComboBox typeBox;
    juce::ToggleButton onBtn { "On" }, dynBtn { "Dynamic" };
    // AI mode
    static constexpr int kAiTargetFrames = 240;   // ~8 s of active vocal
    AutoEqAnalyzer analyzer;
    std::vector<float> aiFrame = std::vector<float> ((size_t) AutoEqAnalyzer::kFftSize);
    bool listening = false;
    juce::String aiStatus = "AI mode: press AI Scan, then play your vocal.";
    juce::ValueTree prevState;
    juce::TextButton aiBtn { "AI Scan" }, undoBtn { "Undo AI" };
    juce::Slider intensitySlider;
    juce::Label intensityLabel;

    juce::Label outLabel;
    juce::Slider outKnob;

    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt, dynAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> outAtt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SesEqEditor)
};
