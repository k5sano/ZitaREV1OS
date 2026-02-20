#pragma once

#include <JuceHeader.h>
#include "zita/reverb.h"

class ZitaRev1OSProcessor : public juce::AudioProcessor,
                            public juce::AudioProcessorValueTreeState::Listener
{
public:
    ZitaRev1OSProcessor();
    ~ZitaRev1OSProcessor() override;

    //==============================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==============================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "ZitaRev1OS"; }
    bool   acceptsMidi() const override { return false; }
    bool   producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 8.0; }

    int  getNumPrograms() override { return 1; }
    int  getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& dest) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void parameterChanged (const juce::String& paramID, float newValue) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();
    void syncAllParams();

    // -----------------------------------------------------------------------
    // zita-rev1 エンジン（::Reverb で juce::Reverb との衝突を回避）
    // -----------------------------------------------------------------------
    ::Reverb _reverb;
    bool     _reverbReady { false };

    // -----------------------------------------------------------------------
    // 2x オーバーサンプラー (IIR ポリフェーズ、ステレオ 2ch)
    // 2^1 = 2x、filterHalfBandPolyphaseIIR = 高品質かつ低レイテンシ
    // -----------------------------------------------------------------------
    juce::dsp::Oversampling<float> _oversampler {
        2,
        1,
        juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
        true,   // isMaximumQuality
        false   // interleavedProcessing
    };

    // dry コピー用バッファ（OS後サイズで確保）
    juce::AudioBuffer<float> _dryBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ZitaRev1OSProcessor)
};
