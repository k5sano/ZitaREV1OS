#include "PluginProcessor.h"
#include "PluginEditor.h"

//==============================================================================
static const juce::String pID_Delay  = "delay";
static const juce::String pID_RTMid  = "rtmid";
static const juce::String pID_RTLow  = "rtlow";
static const juce::String pID_Damp   = "damp";
static const juce::String pID_Mix    = "mix";

//==============================================================================
ZitaRev1OSProcessor::ZitaRev1OSProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "Parameters", createParameterLayout())
{
    apvts.addParameterListener (pID_Delay, this);
    apvts.addParameterListener (pID_RTMid, this);
    apvts.addParameterListener (pID_RTLow, this);
    apvts.addParameterListener (pID_Damp,  this);
    apvts.addParameterListener (pID_Mix,   this);
}

ZitaRev1OSProcessor::~ZitaRev1OSProcessor()
{
    apvts.removeParameterListener (pID_Delay, this);
    apvts.removeParameterListener (pID_RTMid, this);
    apvts.removeParameterListener (pID_RTLow, this);
    apvts.removeParameterListener (pID_Damp,  this);
    apvts.removeParameterListener (pID_Mix,   this);

    if (_reverbReady)
        _reverb.fini();
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout
ZitaRev1OSProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pID_Delay, 1 }, "Delay",
        juce::NormalisableRange<float> (0.02f, 0.1f, 0.001f),
        0.04f, juce::AudioParameterFloatAttributes().withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pID_RTMid, 1 }, "RT Mid",
        juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f),
        2.0f, juce::AudioParameterFloatAttributes().withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pID_RTLow, 1 }, "RT Low",
        juce::NormalisableRange<float> (0.1f, 8.0f, 0.01f),
        3.0f, juce::AudioParameterFloatAttributes().withLabel ("s")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pID_Damp, 1 }, "Damping",
        juce::NormalisableRange<float> (1000.0f, 20000.0f, 1.0f, 0.4f),
        6000.0f, juce::AudioParameterFloatAttributes().withLabel ("Hz")));

    layout.add (std::make_unique<juce::AudioParameterFloat> (
        juce::ParameterID { pID_Mix, 1 }, "Mix",
        juce::NormalisableRange<float> (0.0f, 1.0f, 0.001f),
        0.8f));  // 0.5f は wet が小さすぎるため 0.8f に設定

    return layout;
}

//==============================================================================
void ZitaRev1OSProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // ---------- オーバーサンプラー初期化 ----------
    juce::dsp::ProcessSpec spec;
    spec.sampleRate       = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32> (samplesPerBlock);
    spec.numChannels      = 2;
    _oversampler.initProcessing (static_cast<size_t> (samplesPerBlock));

    const int osFactor  = static_cast<int> (_oversampler.getOversamplingFactor());
    const double osRate = sampleRate * osFactor;         // 2x fs
    const int    osBlock = samplesPerBlock * osFactor;   // 2x ブロックサイズ

    // ---------- Reverb を OS サンプルレートで初期化 ----------
    if (_reverbReady)
        _reverb.fini();

    _reverb.init (static_cast<float> (osRate), /*ambis=*/false);
    _reverbReady = true;

    // APVTS 現在値を反映（内部カウンタに差分を発生させる）
    syncAllParams();

    // dryBuffer を osBlock サイズで確保
    _dryBuffer.setSize (2, osBlock, false, true, false);

    // ※ prepare()/warmup はここで呼ばない
    //    _d0/_d1 は process() 後もリセットされないため、prepare() 単体で呼ぶとゲインがオーバーシュートする
    //    processBlock 内で毎回 syncAllParams() → prepare() → process() と呼ぶことで
    //    _d0 = (target - current_g0) / nfram が正しく再計算され、1ブロックで収束する
}

void ZitaRev1OSProcessor::releaseResources()
{
    if (_reverbReady)
    {
        _reverb.fini();
        _reverbReady = false;
    }
    _oversampler.reset();
}

//==============================================================================
void ZitaRev1OSProcessor::processBlock (juce::AudioBuffer<float>& buffer,
                                        juce::MidiBuffer& /*midiMessages*/)
{
    juce::ScopedNoDenormals noDenormals;

    if (!_reverbReady)
        return;

    const int numSamples  = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numSamples == 0 || numChannels == 0)
        return;

    // モノ入力対策
    if (numChannels == 1)
        buffer.copyFrom (1, 0, buffer, 0, 0, numSamples);

    // ① アップサンプル
    juce::dsp::AudioBlock<float> inputBlock (buffer);
    auto osBlock = _oversampler.processSamplesUp (inputBlock);

    const int osN = static_cast<int> (osBlock.getNumSamples());

    // ★ バグ③修正：実際の osN に合わせて dryBuffer を動的確保
    if (_dryBuffer.getNumSamples() < osN)
        _dryBuffer.setSize (2, osN, false, true, false);

    // ② ★ バグ②修正：dry コピーを osBlock から取る
    for (int ch = 0; ch < 2; ++ch)
        _dryBuffer.copyFrom (ch, 0,
                             osBlock.getChannelPointer (ch), osN);

    // ③ inp / out ポインタ配列（4 要素必須）
    float* inp[4] = {
        _dryBuffer.getWritePointer (0),
        _dryBuffer.getWritePointer (1),
        nullptr, nullptr
    };

    // out は osBlock のバッファへ直接書き込む
    float* out[4] = {
        osBlock.getChannelPointer (0),
        osBlock.getChannelPointer (1),
        nullptr, nullptr
    };

    // ④ パラメータ更新反映（毎ブロック必須）
    //    syncAllParams() でカウンタ不一致を強制し、prepare() が _d0/_d1 を
    //    (target - current_g0) / nfram として再計算 → 1ブロックで目標値に収束
    syncAllParams();
    _reverb.prepare (osN);

    // ⑤ リバーブ処理本体
    _reverb.process (osN, inp, out);

#ifdef JUCE_DEBUG
    {
        // 最初の出力サンプルが 0 でないか確認
        float outL = osBlock.getChannelPointer(0)[0];
        float outR = osBlock.getChannelPointer(1)[0];
        DBG ("ZitaRev1OS out[0] L=" + juce::String(outL)
             + "  R=" + juce::String(outR));
    }
#endif

    // ⑥ ダウンサンプル → buffer へ書き戻し
    _oversampler.processSamplesDown (inputBlock);

    // ⑦ 出力ゲイン補正 (+6 dB)
    buffer.applyGain (2.0f);
}

//==============================================================================
void ZitaRev1OSProcessor::parameterChanged (const juce::String& paramID, float newValue)
{
    if (!_reverbReady)
        return;

    if      (paramID == pID_Delay) _reverb.set_delay (newValue);
    else if (paramID == pID_RTMid) _reverb.set_rtmid (newValue);
    else if (paramID == pID_RTLow) _reverb.set_rtlow (newValue);
    else if (paramID == pID_Damp)  _reverb.set_fdamp (newValue);
    else if (paramID == pID_Mix)   _reverb.set_opmix (newValue);
}

void ZitaRev1OSProcessor::syncAllParams()
{
    _reverb.set_delay (apvts.getRawParameterValue (pID_Delay)->load());
    _reverb.set_rtmid (apvts.getRawParameterValue (pID_RTMid)->load());
    _reverb.set_rtlow (apvts.getRawParameterValue (pID_RTLow)->load());
    _reverb.set_fdamp (apvts.getRawParameterValue (pID_Damp)->load());
    _reverb.set_opmix (apvts.getRawParameterValue (pID_Mix)->load());
    _reverb.set_xover (200.0f);
}

//==============================================================================
juce::AudioProcessorEditor* ZitaRev1OSProcessor::createEditor()
{
    return new ZitaRev1OSEditor (*this);
}

void ZitaRev1OSProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, dest);
}

void ZitaRev1OSProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml != nullptr && xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));
}

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ZitaRev1OSProcessor();
}
