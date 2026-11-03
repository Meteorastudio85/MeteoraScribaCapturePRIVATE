#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>

#include "CaptureEngine.h"
#include "Lang.h"
#include "DynEngine.h"

class ScribaSweepAudioProcessor : public juce::AudioProcessor,
                                  private juce::AsyncUpdater
{
public:
    enum class CaptureState : int { Idle = 0, Running, Recorded, Analysing };
    enum class CaptureKind  : int { Sweep = 0, Nam, Dyn };

    ScribaSweepAudioProcessor();
    ~ScribaSweepAudioProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                 { return true; }

    const juce::String getName() const override     { return JucePlugin_Name; }
    bool acceptsMidi() const override               { return false; }
    bool producesMidi() const override              { return false; }
    bool isMidiEffect() const override              { return false; }
    double getTailLengthSeconds() const override;

    int getNumPrograms() override                   { return 1; }
    int getCurrentProgram() override                { return 0; }
    void setCurrentProgram (int) override           {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::AudioProcessorValueTreeState apvts;

    // --- cattura ---
    bool startCapture (juce::String& error);
    void cancelCapture();
    CaptureState getCaptureState() const    { return (CaptureState) captureState.load(); }
    float getCaptureProgress() const;
    float getAndResetInputPeak()            { return inputPeak.exchange (0.0f); }

    juce::String getStatusText() const;
    void setStatusText (const juce::String& text);

    std::shared_ptr<const CaptureResult> getCapture() const;
    int getCaptureVersion() const           { return captureVersion.load(); }

    bool loadCaptureFile (const juce::File& file, juce::String& error);
    bool saveCaptureFile (const juce::File& file, juce::String& error, juce::String& savedDescription) const;
    bool exportImpulseWav (const juce::File& file, juce::String& error) const;

    // --- cattura NAM (file di addestramento per Neural Amp Modeler / TONE3000) ---
    int  getModeIndex() const;   // 0 = riproduzione, 1 = sweep, 2 = NAM, 3 = DYN
    bool loadNamInput (const juce::File& file, juce::String& error);
    void useBuiltInNamInput();
    bool isUsingBuiltInNam() const          { return usingBuiltInNam; }
    bool startNamCapture (juce::String& error);
    bool saveNamOutput (const juce::File& file, juce::String& error);
    bool hasNamInput() const;
    bool isNamOutputReady() const           { return namOutputReady.load(); }
    juce::String getNamInputDescription() const;

    // --- cattura DYN (curva di compressione, attacco e rilascio) ---
    bool startDynCapture (juce::String& error);
    std::shared_ptr<const DynResult> getDynResult() const;
    int  getDynVersion() const              { return dynVersion.load(); }
    bool saveDynFiles (const juce::File& file, juce::String& error, juce::String& savedDescription) const;
    DynSettings getCurrentDynSettings() const;

private:
    struct DspParams
    {
        bool  valid = false;
        float a2 = 0.0f, a3 = 0.0f;
        bool  hasDynamics = false;
        std::array<float, ScribaConst::numSteps> curve {};
        float attackMs = 10.0f, releaseMs = 100.0f;
    };

    class AnalysisThread : public juce::Thread
    {
    public:
        explicit AnalysisThread (ScribaSweepAudioProcessor& p) : juce::Thread ("Scriba Capture analysis"), owner (p) {}
        void run() override;
    private:
        ScribaSweepAudioProcessor& owner;
    };

    void runAnalysis();
    void finishNamCapture();
    void runDynAnalysis();
    static void applyDynToCapture (CaptureResult& c, const DynResult& d);
    bool loadBuiltInNamInput (juce::String& error);
    bool setNamInputFromReader (juce::AudioFormatReader& reader, const juce::String& name, juce::String& error);
    bool hasDecodedNamInput() const;
    void installCapture (std::shared_ptr<CaptureResult> c);
    void updateLatency();
    void handleAsyncUpdate() override;

    static std::unique_ptr<juce::XmlElement> captureToXml (const CaptureResult& c);
    static std::shared_ptr<CaptureResult> captureFromXml (const juce::XmlElement& x);

    // parametri
    std::atomic<float>* modeParam   = nullptr;
    std::atomic<float>* inputParam  = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* mixParam    = nullptr;
    std::atomic<float>* harmParam   = nullptr;
    std::atomic<float>* dynParam    = nullptr;
    std::atomic<float>* levelParam  = nullptr;
    std::atomic<float>* sweepParam  = nullptr;
    std::atomic<float>* tailParam   = nullptr;
    std::atomic<float>* dynTestParam = nullptr;
    std::atomic<float>* saveRawParam = nullptr;
    std::atomic<float>* dynLevelParam = nullptr;
    std::atomic<float>* dynSpeedParam = nullptr;
    std::atomic<float>* dynApplyParam = nullptr;

    double currentSampleRate = 48000.0;

    // stato della cattura
    std::atomic<int>   captureState { (int) CaptureState::Idle };
    std::atomic<int>   capturePos { 0 };
    std::atomic<int>   captureKind { (int) CaptureKind::Sweep };
    int captureTotal = 0;
    std::atomic<bool>  cancelRequested { false };
    std::atomic<float> inputPeak { 0.0f };
    std::vector<float> stimulus;
    juce::AudioBuffer<float> recording;
    CaptureSettings pendingSettings;
    StimulusLayout  pendingLayout;
    DynSettings     pendingDynSettings;
    DynLayout       pendingDynLayout;

    mutable juce::CriticalSection captureLock;
    std::shared_ptr<const CaptureResult> capture;
    std::shared_ptr<CaptureResult> pendingInstall;
    std::atomic<int> captureVersion { 0 };
    std::shared_ptr<const DynResult> dynResult;   // protetto da captureLock
    std::atomic<int> dynVersion { 0 };

    mutable juce::CriticalSection namLock;
    std::vector<float> namInput;
    double namInputRate = 48000.0, namOutputRate = 48000.0;
    juce::String namInputName;
    juce::AudioBuffer<float> namOutput;
    std::atomic<bool> namOutputReady { false };
    bool usingBuiltInNam = true;   // di default usa l'input NAM integrato

    mutable juce::CriticalSection statusLock;
    juce::String statusText { TR ("Pronto") };

    // DSP di riproduzione
    juce::SpinLock dspLock;
    DspParams dspParams, localDsp;

    juce::dsp::Convolution convolution;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::None> dryDelay { 48000 };
    juce::AudioBuffer<float> dryBuffer;
    std::atomic<int> latencyForDry { 0 };

    juce::SmoothedValue<float> inputGain, outputGain, mixAmount;
    float envelope = 0.0f;
    std::array<float, 2> dcX {}, dcY {};

    AnalysisThread analysisThread { *this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScribaSweepAudioProcessor)
};
