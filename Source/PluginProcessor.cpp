#include "Lang.h"
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "BinaryData.h"

namespace
{
    const char* const kBuiltInNamName = "input.wav (integrato)";

    bool writeWavFile (const juce::File& target, const juce::AudioBuffer<float>& buffer, double sampleRate,
                       juce::String& error, int bitsPerSample = 32)
    {
        target.deleteFile();

        std::unique_ptr<juce::OutputStream> stream (target.createOutputStream());
        if (stream == nullptr)
        {
            error = TR ("Impossibile creare il file ") + target.getFileName();
            return false;
        }

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate,
                                                                              (unsigned int) buffer.getNumChannels(),
                                                                              bitsPerSample, {}, 0));
        if (writer == nullptr)
        {
            error = TR ("Impossibile scrivere il WAV.");
            return false;
        }

        stream.release();   // ora appartiene al writer
        return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
    }

    const float kSweepSeconds[] = { 3.0f, 6.0f, 10.0f };
    const float kTailSeconds[]  = { 0.3f, 1.0f, 3.0f, 6.0f };

    juce::AudioParameterFloatAttributes dbAttr (const juce::String& unit)
    {
        return juce::AudioParameterFloatAttributes()
                   .withLabel (unit)
                   .withStringFromValueFunction ([] (float v, int) { return juce::String (v, 1); });
    }
}

//==============================================================================
juce::AudioProcessorValueTreeState::ParameterLayout ScribaSweepAudioProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Modo",
                                                        StringArray { "Riproduzione", "Cattura", "Cattura NAM", "Cattura DYN" }, 0));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttr ("dB")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "harmonics", 1 }, "Armoniche",
                                                       NormalisableRange<float> (0.0f, 200.0f, 1.0f), 100.0f, dbAttr ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dynamics", 1 }, "Dinamica",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, dbAttr ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix",
                                                       NormalisableRange<float> (0.0f, 100.0f, 1.0f), 100.0f, dbAttr ("%")));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output",
                                                       NormalisableRange<float> (-24.0f, 24.0f, 0.1f), 0.0f, dbAttr ("dB")));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "testLevel", 1 }, "Livello test",
                                                       NormalisableRange<float> (-36.0f, -6.0f, 0.5f), -18.0f, dbAttr ("dBFS")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "sweepLength", 1 }, "Durata sweep",
                                                        StringArray { "3 s", "6 s", "10 s" }, 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "tail", 1 }, "Coda",
                                                        StringArray { "0,3 s", "1 s", "3 s", "6 s" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dynTest", 1 }, "Test dinamica", true));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "saveRaw", 1 }, "Registrazione grezza", true));

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "dynLevel", 1 }, "Livello max DYN",
                                                       NormalisableRange<float> (-24.0f, 0.0f, 0.5f), -6.0f, dbAttr ("dBFS")));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "dynSpeed", 1 }, "Durata DYN",
                                                        StringArray { "Veloce", "Standard", "Lenta" }, 1));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { "dynApply", 1 }, "Applica DYN a PLAY", true));

    return layout;
}

//==============================================================================
ScribaSweepAudioProcessor::ScribaSweepAudioProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "SCRIBA_SWEEP", createLayout())
{
    modeParam    = apvts.getRawParameterValue ("mode");
    inputParam   = apvts.getRawParameterValue ("input");
    outputParam  = apvts.getRawParameterValue ("output");
    mixParam     = apvts.getRawParameterValue ("mix");
    harmParam    = apvts.getRawParameterValue ("harmonics");
    dynParam     = apvts.getRawParameterValue ("dynamics");
    levelParam   = apvts.getRawParameterValue ("testLevel");
    sweepParam   = apvts.getRawParameterValue ("sweepLength");
    tailParam    = apvts.getRawParameterValue ("tail");
    dynTestParam = apvts.getRawParameterValue ("dynTest");
    saveRawParam = apvts.getRawParameterValue ("saveRaw");
    dynLevelParam = apvts.getRawParameterValue ("dynLevel");
    dynSpeedParam = apvts.getRawParameterValue ("dynSpeed");
    dynApplyParam = apvts.getRawParameterValue ("dynApply");

    analysisThread.startThread();
}

ScribaSweepAudioProcessor::~ScribaSweepAudioProcessor()
{
    cancelPendingUpdate();
    analysisThread.stopThread (20000);
}

//==============================================================================
void ScribaSweepAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const bool rateChanged = std::abs (sampleRate - currentSampleRate) > 0.5;
    currentSampleRate = sampleRate;

    const auto numCh = (juce::uint32) juce::jmax (1, getTotalNumOutputChannels());
    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) juce::jmax (1, samplesPerBlock), numCh };

    convolution.prepare (spec);

    dryDelay.setMaximumDelayInSamples (juce::jmax (64, (int) (sampleRate * 0.05)));
    dryDelay.prepare (spec);
    dryDelay.reset();

    dryBuffer.setSize ((int) numCh, juce::jmax (1, samplesPerBlock));

    inputGain.reset (sampleRate, 0.05);
    outputGain.reset (sampleRate, 0.05);
    mixAmount.reset (sampleRate, 0.05);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (inputParam->load()));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    mixAmount.setCurrentAndTargetValue (mixParam->load() * 0.01f);

    envelope = 0.0f;
    dcX.fill (0.0f);
    dcY.fill (0.0f);

    if (rateChanged && captureState.load() == (int) CaptureState::Running)
    {
        captureState.store ((int) CaptureState::Idle);
        setStatusText (TR ("Cattura interrotta: il sample rate e' cambiato."));
    }

    updateLatency();
}

bool ScribaSweepAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainInputChannelSet() == out;
}

double ScribaSweepAudioProcessor::getTailLengthSeconds() const
{
    if (auto c = getCapture())
        return (double) c->ir.getNumSamples() / c->sampleRate;
    return 0.0;
}

//==============================================================================
void ScribaSweepAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numIn  = getTotalNumInputChannels();
    const int numOut = getTotalNumOutputChannels();

    for (int ch = numIn; ch < numOut; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples == 0 || numOut == 0)
        return;

    // livello del segnale in ingresso (meter del ritorno)
    float pk = 0.0f;
    for (int ch = 0; ch < numIn; ++ch)
        pk = juce::jmax (pk, buffer.getMagnitude (ch, 0, numSamples));
    if (pk > inputPeak.load (std::memory_order_relaxed))
        inputPeak.store (pk, std::memory_order_relaxed);

    int state = captureState.load();

    if (state == (int) CaptureState::Running && cancelRequested.exchange (false))
    {
        captureState.store ((int) CaptureState::Idle);
        state = (int) CaptureState::Idle;
    }

    // ---------------- CATTURA ----------------
    if (state == (int) CaptureState::Running)
    {
        const int total = (int) stimulus.size();
        const int pos   = capturePos.load();
        const int n     = juce::jlimit (0, numSamples, total - pos);
        const int recCh = juce::jmin (numIn, recording.getNumChannels());

        if (n > 0)
            for (int ch = 0; ch < recCh; ++ch)
                recording.copyFrom (ch, pos, buffer, ch, 0, n);

        for (int ch = 0; ch < numOut; ++ch)
        {
            float* d = buffer.getWritePointer (ch);
            for (int i = 0; i < n; ++i)
                d[i] = stimulus[(size_t) (pos + i)];
            for (int i = n; i < numSamples; ++i)
                d[i] = 0.0f;
        }

        capturePos.store (pos + n);
        if (pos + n >= total)
            captureState.store ((int) CaptureState::Recorded);

        return;
    }

    // in modo CATTURA, fuori dalla misura, l'uscita resta muta (evita anelli di feedback)
    if (modeParam->load() > 0.5f)
    {
        buffer.clear();
        return;
    }

    // ---------------- RIPRODUZIONE ----------------
    {
        const juce::SpinLock::ScopedTryLockType lock (dspLock);
        if (lock.isLocked())
            localDsp = dspParams;
    }

    const DspParams& p = localDsp;
    if (! p.valid)
        return;   // nessuna cattura caricata: passa il segnale

    const int numCh = juce::jmin (numOut, 2);

    // copia del segnale dry, ritardata come la convoluzione
    if (dryBuffer.getNumSamples() < numSamples || dryBuffer.getNumChannels() < numCh)
        dryBuffer.setSize (numCh, numSamples, false, false, true);

    for (int ch = 0; ch < numCh; ++ch)
        dryBuffer.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    dryDelay.setDelay ((float) latencyForDry.load());
    {
        juce::dsp::AudioBlock<float> dryBlock (dryBuffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) numSamples);
        dryDelay.process (juce::dsp::ProcessContextReplacing<float> (dryBlock));
    }

    inputGain.setTargetValue (juce::Decibels::decibelsToGain (inputParam->load()));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (outputParam->load()));
    mixAmount.setTargetValue (mixParam->load() * 0.01f);

    const float harm = harmParam->load() * 0.01f;
    const float a2 = juce::jlimit (-1.0f, 1.0f, p.a2 * harm);
    const float a3 = juce::jlimit (0.0f, 2.0f, p.a3 * harm);
    const float xm = a3 > 1.0e-6f ? juce::jmin (2.0f, 1.0f / std::sqrt (3.0f * a3)) : 2.0f;
    const bool useHarm = std::abs (a2) > 1.0e-6f || a3 > 1.0e-6f;

    const float dynAmt = dynParam->load() * 0.01f;
    const bool useDyn = p.hasDynamics && dynAmt > 0.001f;
    const float sr = (float) currentSampleRate;
    const float attC = std::exp (-1.0f / (juce::jmax (0.00005f, p.attackMs * 0.001f) * sr));
    const float relC = std::exp (-1.0f / (juce::jmax (0.001f, p.releaseMs * 0.001f) * sr));
    const float dcR = 1.0f - (juce::MathConstants<float>::twoPi * 5.0f / sr);

    float* data[2] = { buffer.getWritePointer (0), numCh > 1 ? buffer.getWritePointer (1) : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        const float gIn = inputGain.getNextValue();
        float peak = 0.0f;

        for (int ch = 0; ch < numCh; ++ch)
        {
            data[ch][i] *= gIn;
            peak = juce::jmax (peak, std::abs (data[ch][i]));
        }

        // dinamica (curva statica misurata + attacco/rilascio misurati)
        if (useDyn)
        {
            const float c = peak > envelope ? attC : relC;
            envelope = c * envelope + (1.0f - c) * peak;
            const float levelDb = juce::Decibels::gainToDecibels (envelope, -100.0f);
            const float g = juce::Decibels::decibelsToGain (CaptureEngine::interpolateCurve (p.curve, levelDb) * dynAmt);
            for (int ch = 0; ch < numCh; ++ch)
                data[ch][i] *= g;
        }

        // armoniche (2a e 3a misurate) + blocco della componente continua
        if (useHarm)
        {
            for (int ch = 0; ch < numCh; ++ch)
            {
                const float x = juce::jlimit (-xm, xm, data[ch][i]);
                const float y = x + a2 * x * x - a3 * x * x * x;
                const float o = y - dcX[(size_t) ch] + dcR * dcY[(size_t) ch];
                dcX[(size_t) ch] = y;
                dcY[(size_t) ch] = o;
                data[ch][i] = o;
            }
        }
    }

    // risposta all'impulso dell'hardware
    {
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) numCh, (size_t) numSamples);
        convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

    // mix e uscita
    for (int i = 0; i < numSamples; ++i)
    {
        const float gOut = outputGain.getNextValue();
        const float m = mixAmount.getNextValue();
        for (int ch = 0; ch < numCh; ++ch)
            data[ch][i] = (data[ch][i] * m + dryBuffer.getSample (ch, i) * (1.0f - m)) * gOut;
    }
}

//==============================================================================
bool ScribaSweepAudioProcessor::startCapture (juce::String& error)
{
    if (captureState.load() != (int) CaptureState::Idle)
    {
        error = TR ("Una cattura e' gia' in corso.");
        return false;
    }

    if (getModeIndex() != 1)
    {
        error = TR ("Passa in modo SWEEP prima di avviare.");
        return false;
    }

    if (getTotalNumInputChannels() == 0)
    {
        error = TR ("Il plugin non ha ingressi attivi.");
        return false;
    }

    CaptureSettings s;
    s.sampleRate   = currentSampleRate;
    s.testLevelDb  = levelParam->load();
    s.sweepSeconds = kSweepSeconds[juce::jlimit (0, 2, (int) sweepParam->load())];
    s.tailSeconds  = kTailSeconds[juce::jlimit (0, 3, (int) tailParam->load())];
    s.dynamicsTest = dynTestParam->load() > 0.5f;

    pendingSettings = s;
    pendingLayout   = CaptureEngine::makeLayout (s);
    CaptureEngine::generateStimulus (s, pendingLayout, stimulus);

    recording.setSize (juce::jlimit (1, 2, getTotalNumInputChannels()), pendingLayout.total);
    recording.clear();

    captureKind.store ((int) CaptureKind::Sweep);
    captureTotal = pendingLayout.total;
    capturePos.store (0);
    cancelRequested.store (false);
    setStatusText (TR ("Cattura in corso... non toccare l'hardware."));
    captureState.store ((int) CaptureState::Running);
    return true;
}

void ScribaSweepAudioProcessor::cancelCapture()
{
    if (captureState.load() == (int) CaptureState::Running)
    {
        cancelRequested.store (true);
        setStatusText (TR ("Cattura annullata."));
    }
}

float ScribaSweepAudioProcessor::getCaptureProgress() const
{
    const int total = captureTotal;
    return total > 0 ? juce::jlimit (0.0f, 1.0f, (float) capturePos.load() / (float) total) : 0.0f;
}

juce::String ScribaSweepAudioProcessor::getStatusText() const
{
    const juce::ScopedLock sl (statusLock);
    return statusText;
}

void ScribaSweepAudioProcessor::setStatusText (const juce::String& text)
{
    const juce::ScopedLock sl (statusLock);
    statusText = text;
}

std::shared_ptr<const CaptureResult> ScribaSweepAudioProcessor::getCapture() const
{
    const juce::ScopedLock sl (captureLock);
    return capture;
}

//==============================================================================
void ScribaSweepAudioProcessor::AnalysisThread::run()
{
    while (! threadShouldExit())
    {
        if (owner.captureState.load() == (int) CaptureState::Recorded)
        {
            if (owner.captureKind.load() == (int) CaptureKind::Nam)
                owner.finishNamCapture();
            else if (owner.captureKind.load() == (int) CaptureKind::Dyn)
                owner.runDynAnalysis();
            else
                owner.runAnalysis();
        }

        wait (50);
    }
}

void ScribaSweepAudioProcessor::runAnalysis()
{
    captureState.store ((int) CaptureState::Analysing);
    setStatusText (TR ("Analisi in corso..."));

    auto result = std::make_shared<CaptureResult>();
    juce::String error;

    const bool ok = CaptureEngine::analyse (pendingSettings, pendingLayout, recording, *result, error,
                                            [this] { return analysisThread.threadShouldExit(); });

    if (ok)
    {
        CaptureEngine::computeDisplay (*result);
        result->settings = pendingSettings;
        result->layout   = pendingLayout;

        // registrazione grezza: ch 0 = segnale inviato, ch 1-2 = ritorno
        auto raw = std::make_shared<juce::AudioBuffer<float>> (1 + recording.getNumChannels(), recording.getNumSamples());
        raw->clear();
        raw->copyFrom (0, 0, stimulus.data(), juce::jmin ((int) stimulus.size(), recording.getNumSamples()));
        for (int ch = 0; ch < recording.getNumChannels(); ++ch)
            raw->copyFrom (ch + 1, 0, recording, ch, 0, recording.getNumSamples());
        result->raw = raw;
        result->name = TR ("Cattura ") + juce::Time::getCurrentTime().formatted ("%d-%m-%Y %H:%M");

        {
            const juce::ScopedLock sl (captureLock);
            pendingInstall = result;
        }

        setStatusText (result->warning.isNotEmpty() ? result->warning
                                                    : juce::String (TR ("Cattura completata. Passa in PLAY per usarla.")));
        triggerAsyncUpdate();
    }
    else
    {
        setStatusText (error);
    }

    recording.setSize (1, 1);
    captureState.store ((int) CaptureState::Idle);
}

//==============================================================================
DynSettings ScribaSweepAudioProcessor::getCurrentDynSettings() const
{
    DynSettings s;
    s.sampleRate = currentSampleRate;
    s.maxLevelDb = dynLevelParam->load();
    s.speed      = juce::jlimit (0, 2, juce::roundToInt (dynSpeedParam->load()));
    return s;
}

bool ScribaSweepAudioProcessor::startDynCapture (juce::String& error)
{
    if (captureState.load() != (int) CaptureState::Idle)
    {
        error = TR ("Una cattura e' gia' in corso.");
        return false;
    }

    if (getModeIndex() != 3)
    {
        error = TR ("Passa in modo DYN prima di avviare.");
        return false;
    }

    if (getTotalNumInputChannels() == 0)
    {
        error = TR ("Il plugin non ha ingressi attivi.");
        return false;
    }

    pendingDynSettings = getCurrentDynSettings();
    pendingDynLayout   = DynEngine::makeLayout (pendingDynSettings);
    DynEngine::generateStimulus (pendingDynSettings, pendingDynLayout, stimulus);

    recording.setSize (juce::jlimit (1, 2, getTotalNumInputChannels()), pendingDynLayout.total);
    recording.clear();

    captureKind.store ((int) CaptureKind::Dyn);
    captureTotal = pendingDynLayout.total;
    capturePos.store (0);
    cancelRequested.store (false);
    setStatusText (TR ("Cattura DYN in corso... non toccare l'hardware."));
    captureState.store ((int) CaptureState::Running);
    return true;
}

void ScribaSweepAudioProcessor::runDynAnalysis()
{
    captureState.store ((int) CaptureState::Analysing);
    setStatusText (TR ("Analisi DYN in corso..."));

    auto result = std::make_shared<DynResult>();
    juce::String error;

    const bool ok = DynEngine::analyse (pendingDynSettings, pendingDynLayout, stimulus, recording, *result, error,
                                        [this] { return analysisThread.threadShouldExit(); });

    if (ok)
    {
        auto raw = std::make_shared<juce::AudioBuffer<float>> (1 + recording.getNumChannels(), recording.getNumSamples());
        raw->clear();
        raw->copyFrom (0, 0, stimulus.data(), juce::jmin ((int) stimulus.size(), recording.getNumSamples()));
        for (int ch = 0; ch < recording.getNumChannels(); ++ch)
            raw->copyFrom (ch + 1, 0, recording, ch, 0, recording.getNumSamples());
        result->raw  = raw;
        result->name = "DYN " + juce::Time::getCurrentTime().formatted ("%d-%m-%Y %H:%M");

        juce::String message;
        if (result->compresses)
        {
            message << TR ("DYN: soglia ") << juce::String (result->thresholdDb, 1) << " dBFS, ratio "
                    << juce::String (result->ratio, 1) << ":1";
            if (result->displayBurst >= 0)
                message << ", att " << juce::String (result->referenceAttackMs(), 1) << TR (" ms, ril ")
                        << juce::String (result->referenceReleaseMs(), 0) << " ms";
            message << ".";
        }
        else
        {
            message = TR ("DYN: nessuna compressione rilevata. Abbassa la threshold dell'hardware o alza il LIVELLO MAX.");
        }

        // applica curva e tempi misurati alla cattura SWEEP caricata in PLAY
        if (result->compresses && dynApplyParam->load() > 0.5f)
        {
            if (auto current = getCapture())
            {
                auto updated = std::make_shared<CaptureResult> (*current);
                applyDynToCapture (*updated, *result);

                const juce::ScopedLock sl (captureLock);
                pendingInstall = updated;
                message << TR (" Applicata alla cattura in PLAY.");
            }
        }

        if (result->warning.isNotEmpty())
            message = result->warning;

        {
            const juce::ScopedLock sl (captureLock);
            dynResult = result;
        }
        ++dynVersion;

        setStatusText (message);
        triggerAsyncUpdate();
    }
    else
    {
        setStatusText (error);
    }

    recording.setSize (1, 1);
    captureState.store ((int) CaptureState::Idle);
}

void ScribaSweepAudioProcessor::applyDynToCapture (CaptureResult& c, const DynResult& d)
{
    // la curva del PLAY e' il guadagno relativo al livello di test dello sweep
    const float ref = d.modelGrDb (c.testLevelDb);
    for (int i = 0; i < ScribaConst::numSteps; ++i)
        c.curveGainDb[(size_t) i] = juce::jlimit (-40.0f, 40.0f, -(d.modelGrDb (stepLevelDb (i)) - ref));

    c.hasDynamics = true;
    c.attackMs    = juce::jlimit (0.05f, 500.0f, d.referenceAttackMs());
    c.releaseMs   = juce::jlimit (5.0f, 5000.0f, d.referenceReleaseMs());
}

std::shared_ptr<const DynResult> ScribaSweepAudioProcessor::getDynResult() const
{
    const juce::ScopedLock sl (captureLock);
    return dynResult;
}

bool ScribaSweepAudioProcessor::saveDynFiles (const juce::File& file, juce::String& error, juce::String& savedDescription) const
{
    auto d = getDynResult();
    if (d == nullptr)
    {
        error = TR ("Nessuna cattura DYN da salvare.");
        return false;
    }

    const auto csvFile = file.withFileExtension ("csv");
    if (! csvFile.replaceWithText (DynEngine::toCsv (*d), false, false, "\r\n"))
    {
        error = TR ("Impossibile scrivere ") + csvFile.getFileName();
        return false;
    }

    savedDescription = TR ("Salvata: ") + csvFile.getFileName();

    if (saveRawParam->load() > 0.5f && d->raw != nullptr)
    {
        const auto rawFile = csvFile.getSiblingFile (csvFile.getFileNameWithoutExtension() + "_raw.wav");
        juce::String rawError;
        if (writeWavFile (rawFile, *d->raw, d->sampleRate, rawError))
            savedDescription << " + " << rawFile.getFileName();
        else
            savedDescription << TR (" (registrazione grezza non salvata)");
    }

    return true;
}

//==============================================================================
int ScribaSweepAudioProcessor::getModeIndex() const
{
    return juce::roundToInt (modeParam->load());
}

bool ScribaSweepAudioProcessor::setNamInputFromReader (juce::AudioFormatReader& reader, const juce::String& name, juce::String& error)
{
    const auto maxLen = (juce::int64) (reader.sampleRate * 60.0 * 15.0);
    if (reader.lengthInSamples <= 0 || reader.lengthInSamples > maxLen)
    {
        error = TR ("Il file di input NAM e' vuoto o troppo lungo.");
        return false;
    }

    const int len = (int) reader.lengthInSamples;
    juce::AudioBuffer<float> buf (1, len);
    reader.read (&buf, 0, len, 0, true, false);

    const juce::ScopedLock sl (namLock);
    namInput.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    namInputRate = reader.sampleRate;
    namInputName = name;
    namOutput.setSize (1, 1);
    namOutputReady.store (false);
    return true;
}

bool ScribaSweepAudioProcessor::loadBuiltInNamInput (juce::String& error)
{
    // input.wav di addestramento NAM, integrato nel plugin come FLAC (identico bit per bit all'originale)
    juce::FlacAudioFormat flac;
    std::unique_ptr<juce::AudioFormatReader> reader (
        flac.createReaderFor (new juce::MemoryInputStream (BinaryData::nam_input_flac, (size_t) BinaryData::nam_input_flacSize, false), true));

    if (reader == nullptr)
    {
        error = TR ("Impossibile leggere l'input NAM integrato.");
        return false;
    }

    if (! setNamInputFromReader (*reader, TR (kBuiltInNamName), error))
        return false;

    usingBuiltInNam = true;
    return true;
}

bool ScribaSweepAudioProcessor::loadNamInput (const juce::File& file, juce::String& error)
{
    if (captureState.load() != (int) CaptureState::Idle)
    {
        error = TR ("Attendi la fine della cattura in corso.");
        return false;
    }

    juce::AudioFormatManager fm;
    fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
    if (reader == nullptr)
    {
        error = TR ("Impossibile leggere il file di input NAM.");
        return false;
    }

    if (! setNamInputFromReader (*reader, file.getFileName(), error))
        return false;

    usingBuiltInNam = false;
    setStatusText (TR ("Input NAM caricato: ") + file.getFileName());
    return true;
}

void ScribaSweepAudioProcessor::useBuiltInNamInput()
{
    if (captureState.load() != (int) CaptureState::Idle)
        return;

    const juce::ScopedLock sl (namLock);
    namInput.clear();
    namInput.shrink_to_fit();
    namInputName.clear();
    namOutput.setSize (1, 1);
    namOutputReady.store (false);
    usingBuiltInNam = true;
    setStatusText (TR ("Input NAM: file integrato."));
}

bool ScribaSweepAudioProcessor::startNamCapture (juce::String& error)
{
    if (captureState.load() != (int) CaptureState::Idle)
    {
        error = TR ("Una cattura e' gia' in corso.");
        return false;
    }

    if (getModeIndex() != 2)
    {
        error = TR ("Passa in modo NAM prima di avviare.");
        return false;
    }

    if (getTotalNumInputChannels() == 0)
    {
        error = TR ("Il plugin non ha ingressi attivi.");
        return false;
    }

    // l'input integrato viene decodificato solo quando serve (evita ~36 MB per ogni istanza)
    if (usingBuiltInNam && ! hasDecodedNamInput())
        if (! loadBuiltInNamInput (error))
            return false;

    const juce::ScopedLock sl (namLock);

    if (namInput.empty())
    {
        error = TR ("Carica prima il file di input NAM.");
        return false;
    }

    if (std::abs (namInputRate - currentSampleRate) > 0.5)
    {
        error = TR ("L'input NAM e' a ") + juce::String (juce::roundToInt (namInputRate)) + TR (" Hz: imposta la scheda/sessione a ")
              + juce::String (juce::roundToInt (namInputRate)) + TR (" Hz (ora e' a ") + juce::String (juce::roundToInt (currentSampleRate)) + " Hz).";
        return false;
    }

    stimulus = namInput;
    recording.setSize (1, (int) stimulus.size());
    recording.clear();
    namOutputReady.store (false);

    captureKind.store ((int) CaptureKind::Nam);
    captureTotal = (int) stimulus.size();
    capturePos.store (0);
    cancelRequested.store (false);
    setStatusText (TR ("Cattura NAM in corso... non toccare l'hardware."));
    captureState.store ((int) CaptureState::Running);
    return true;
}

void ScribaSweepAudioProcessor::finishNamCapture()
{
    captureState.store ((int) CaptureState::Analysing);

    const int len = recording.getNumSamples();
    const float peak = recording.getMagnitude (0, 0, len);
    const float peakDb = juce::Decibels::gainToDecibels (peak, -120.0f);

    juce::String message;
    if (peakDb < -50.0f)
    {
        message = TR ("Cattura NAM: nessun segnale di ritorno. Controlla collegamenti e routing.");
    }
    else
    {
        {
            const juce::ScopedLock sl (namLock);
            namOutput.makeCopyOf (recording);
            namOutputRate = currentSampleRate;
        }
        namOutputReady.store (true);

        message = TR ("Cattura NAM completata (picco ") + juce::String (peakDb, 1) + TR (" dBFS). Premi SALVA OUTPUT NAM.");
        if (peak >= 0.99f)
            message = TR ("Attenzione: il ritorno NAM ha clippato. Abbassa il livello e rifai la cattura.");
    }

    setStatusText (message);
    recording.setSize (1, 1);
    captureState.store ((int) CaptureState::Idle);
}

bool ScribaSweepAudioProcessor::saveNamOutput (const juce::File& file, juce::String& error)
{
    const juce::ScopedLock sl (namLock);

    if (! namOutputReady.load() || namOutput.getNumSamples() < 2)
    {
        error = TR ("Nessuna cattura NAM da salvare.");
        return false;
    }

    // WAV mono 24 bit, stessa durata dell'input: il formato che si aspettano NAM e TONE3000
    if (! writeWavFile (file.withFileExtension ("wav"), namOutput, namOutputRate, error, 24))
        return false;

    namOutputReady.store (false);
    return true;
}

bool ScribaSweepAudioProcessor::hasNamInput() const
{
    const juce::ScopedLock sl (namLock);
    return usingBuiltInNam || ! namInput.empty();
}

bool ScribaSweepAudioProcessor::hasDecodedNamInput() const
{
    const juce::ScopedLock sl (namLock);
    return ! namInput.empty();
}

juce::String ScribaSweepAudioProcessor::getNamInputDescription() const
{
    const juce::ScopedLock sl (namLock);
    if (namInput.empty())
        return usingBuiltInNam ? TR (kBuiltInNamName) + "  |  48.0 kHz  |  3:10" : juce::String();

    const int secs = juce::roundToInt ((double) namInput.size() / namInputRate);
    return namInputName + "  |  " + juce::String (namInputRate / 1000.0, 1) + " kHz  |  "
         + juce::String (secs / 60) + ":" + juce::String (secs % 60).paddedLeft ('0', 2);
}

void ScribaSweepAudioProcessor::handleAsyncUpdate()
{
    std::shared_ptr<CaptureResult> c;
    {
        const juce::ScopedLock sl (captureLock);
        c = std::move (pendingInstall);
        pendingInstall.reset();
    }

    if (c != nullptr)
        installCapture (c);
}

void ScribaSweepAudioProcessor::installCapture (std::shared_ptr<CaptureResult> c)
{
    if (c == nullptr || c->ir.getNumSamples() == 0)
        return;

    if (c->responseDb.empty())
        CaptureEngine::computeDisplay (*c);

    juce::AudioBuffer<float> irCopy (c->ir);
    convolution.loadImpulseResponse (std::move (irCopy), c->sampleRate,
                                     c->ir.getNumChannels() > 1 ? juce::dsp::Convolution::Stereo::yes
                                                                : juce::dsp::Convolution::Stereo::no,
                                     juce::dsp::Convolution::Trim::no,
                                     juce::dsp::Convolution::Normalise::no);

    DspParams dp;
    dp.valid = true;

    const float A  = juce::Decibels::decibelsToGain (c->testLevelDb);
    const float h2 = c->h2Db > -119.0f ? juce::Decibels::decibelsToGain (c->h2Db) : 0.0f;
    const float h3 = c->h3Db > -119.0f ? juce::Decibels::decibelsToGain (c->h3Db) : 0.0f;
    dp.a2 = juce::jlimit (0.0f, 1.0f, 2.0f * h2 / A);
    dp.a3 = juce::jlimit (0.0f, 2.0f, 4.0f * h3 / (A * A));

    dp.hasDynamics = c->hasDynamics;
    dp.curve       = c->curveGainDb;
    dp.attackMs    = c->attackMs;
    dp.releaseMs   = c->releaseMs;

    {
        const juce::SpinLock::ScopedLockType sl (dspLock);
        dspParams = dp;
    }

    {
        const juce::ScopedLock sl (captureLock);
        capture = c;
    }

    updateLatency();
    ++captureVersion;
}

void ScribaSweepAudioProcessor::updateLatency()
{
    int lat = 0;
    if (auto c = getCapture())
        lat = juce::roundToInt ((double) c->preSamples * currentSampleRate / c->sampleRate);

    latencyForDry.store (lat);
    setLatencySamples (lat);
}

//==============================================================================
std::unique_ptr<juce::XmlElement> ScribaSweepAudioProcessor::captureToXml (const CaptureResult& c)
{
    auto x = std::make_unique<juce::XmlElement> ("SCRIBA_CAPTURE");
    x->setAttribute ("version", 1);
    x->setAttribute ("name", c.name);
    x->setAttribute ("sampleRate", c.sampleRate);
    x->setAttribute ("pre", c.preSamples);
    x->setAttribute ("testLevel", (double) c.testLevelDb);
    x->setAttribute ("h2", (double) c.h2Db);
    x->setAttribute ("h3", (double) c.h3Db);
    x->setAttribute ("dyn", c.hasDynamics ? 1 : 0);
    x->setAttribute ("attack", (double) c.attackMs);
    x->setAttribute ("release", (double) c.releaseMs);
    x->setAttribute ("latency", (double) c.latencyMs);

    // impostazioni e tempi del test (per analisi successive della registrazione grezza)
    x->setAttribute ("sweepSeconds", (double) c.settings.sweepSeconds);
    x->setAttribute ("tailSeconds", (double) c.settings.tailSeconds);
    x->setAttribute ("dynTest", c.settings.dynamicsTest ? 1 : 0);
    x->setAttribute ("f1", c.layout.f1);
    x->setAttribute ("f2", c.layout.f2);
    x->setAttribute ("sweepStart", c.layout.sweepStart);
    x->setAttribute ("sweepLength", c.layout.sweepLength);
    x->setAttribute ("tailLength", c.layout.tailLength);
    x->setAttribute ("maxLatency", c.layout.maxLatency);
    x->setAttribute ("stepsStart", c.layout.stepsStart);
    x->setAttribute ("stepLength", c.layout.stepLength);
    x->setAttribute ("arStart", c.layout.arStart);
    x->setAttribute ("arQuiet1", c.layout.arQuiet1);
    x->setAttribute ("arLoud", c.layout.arLoud);
    x->setAttribute ("arQuiet2", c.layout.arQuiet2);
    x->setAttribute ("total", c.layout.total);

    juce::StringArray curve;
    for (auto v : c.curveGainDb)
        curve.add (juce::String (v, 3));
    x->setAttribute ("curve", curve.joinIntoString (","));

    x->setAttribute ("channels", c.ir.getNumChannels());
    x->setAttribute ("length", c.ir.getNumSamples());

    for (int ch = 0; ch < c.ir.getNumChannels(); ++ch)
    {
        juce::MemoryBlock mb (c.ir.getReadPointer (ch), sizeof (float) * (size_t) c.ir.getNumSamples());
        x->createNewChildElement ("CH")->setAttribute ("data", mb.toBase64Encoding());
    }

    return x;
}

std::shared_ptr<CaptureResult> ScribaSweepAudioProcessor::captureFromXml (const juce::XmlElement& x)
{
    if (! x.hasTagName ("SCRIBA_CAPTURE"))
        return {};

    const int chs = juce::jlimit (1, 2, x.getIntAttribute ("channels", 1));
    const int len = x.getIntAttribute ("length", 0);
    if (len <= 0)
        return {};

    auto c = std::make_shared<CaptureResult>();
    c->name        = x.getStringAttribute ("name", "Cattura");
    c->sampleRate  = x.getDoubleAttribute ("sampleRate", 48000.0);
    c->preSamples  = x.getIntAttribute ("pre", 0);
    c->testLevelDb = (float) x.getDoubleAttribute ("testLevel", -18.0);
    c->h2Db        = (float) x.getDoubleAttribute ("h2", -120.0);
    c->h3Db        = (float) x.getDoubleAttribute ("h3", -120.0);
    c->hasDynamics = x.getBoolAttribute ("dyn", false);
    c->attackMs    = (float) x.getDoubleAttribute ("attack", 10.0);
    c->releaseMs   = (float) x.getDoubleAttribute ("release", 100.0);
    c->latencyMs   = (float) x.getDoubleAttribute ("latency", 0.0);

    c->settings.sampleRate   = c->sampleRate;
    c->settings.testLevelDb  = c->testLevelDb;
    c->settings.sweepSeconds = (float) x.getDoubleAttribute ("sweepSeconds", 6.0);
    c->settings.tailSeconds  = (float) x.getDoubleAttribute ("tailSeconds", 1.0);
    c->settings.dynamicsTest = x.getBoolAttribute ("dynTest", true);
    c->layout.f1          = x.getDoubleAttribute ("f1", 20.0);
    c->layout.f2          = x.getDoubleAttribute ("f2", 20000.0);
    c->layout.sweepStart  = x.getIntAttribute ("sweepStart");
    c->layout.sweepLength = x.getIntAttribute ("sweepLength");
    c->layout.tailLength  = x.getIntAttribute ("tailLength");
    c->layout.maxLatency  = x.getIntAttribute ("maxLatency");
    c->layout.stepsStart  = x.getIntAttribute ("stepsStart");
    c->layout.stepLength  = x.getIntAttribute ("stepLength");
    c->layout.arStart     = x.getIntAttribute ("arStart");
    c->layout.arQuiet1    = x.getIntAttribute ("arQuiet1");
    c->layout.arLoud      = x.getIntAttribute ("arLoud");
    c->layout.arQuiet2    = x.getIntAttribute ("arQuiet2");
    c->layout.total       = x.getIntAttribute ("total");

    const auto curve = juce::StringArray::fromTokens (x.getStringAttribute ("curve"), ",", "");
    for (int i = 0; i < juce::jmin (ScribaConst::numSteps, curve.size()); ++i)
        c->curveGainDb[(size_t) i] = curve[i].getFloatValue();

    c->ir.setSize (chs, len);
    c->ir.clear();

    int ch = 0;
    for (auto* e : x.getChildWithTagNameIterator ("CH"))
    {
        if (ch >= chs)
            break;

        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (e->getStringAttribute ("data")))
        {
            const size_t bytes = juce::jmin (mb.getSize(), sizeof (float) * (size_t) len);
            std::memcpy (c->ir.getWritePointer (ch), mb.getData(), bytes);
        }
        ++ch;
    }

    CaptureEngine::computeDisplay (*c);
    return c;
}

bool ScribaSweepAudioProcessor::loadCaptureFile (const juce::File& file, juce::String& error)
{
    const auto ext = file.getFileExtension().toLowerCase();

    if (ext == ".wav" || ext == ".aif" || ext == ".aiff")
    {
        juce::AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (file));
        if (reader == nullptr)
        {
            error = TR ("Impossibile leggere il file audio.");
            return false;
        }

        const int chs = juce::jlimit (1, 2, (int) reader->numChannels);
        const int len = (int) juce::jmin<juce::int64> (reader->lengthInSamples, (juce::int64) (reader->sampleRate * 10.0));
        if (len <= 0)
        {
            error = TR ("Il file audio e' vuoto.");
            return false;
        }

        auto c = std::make_shared<CaptureResult>();
        c->name = file.getFileNameWithoutExtension();
        c->sampleRate = reader->sampleRate;
        c->ir.setSize (chs, len);
        reader->read (&c->ir, 0, len, 0, true, chs > 1);
        CaptureEngine::computeDisplay (*c);

        installCapture (c);
        setStatusText (TR ("Risposta all'impulso caricata: ") + c->name);
        return true;
    }

    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr)
    {
        error = TR ("File non valido.");
        return false;
    }

    auto c = captureFromXml (*xml);
    if (c == nullptr)
    {
        error = TR ("Il file non contiene una cattura Scriba Capture.");
        return false;
    }

    c->name = file.getFileNameWithoutExtension();
    installCapture (c);
    setStatusText (TR ("Cattura caricata: ") + c->name);
    return true;
}

bool ScribaSweepAudioProcessor::saveCaptureFile (const juce::File& file, juce::String& error, juce::String& savedDescription) const
{
    auto c = getCapture();
    if (c == nullptr)
    {
        error = TR ("Nessuna cattura da salvare.");
        return false;
    }

    const auto target = file.withFileExtension ("scriba");
    if (! captureToXml (*c)->writeTo (target))
    {
        error = TR ("Impossibile scrivere il file.");
        return false;
    }

    savedDescription = TR ("Salvata: ") + target.getFileName();

    if (saveRawParam->load() > 0.5f && c->raw != nullptr)
    {
        const auto rawFile = target.getSiblingFile (target.getFileNameWithoutExtension() + "_raw.wav");
        juce::String rawError;
        if (writeWavFile (rawFile, *c->raw, c->sampleRate, rawError))
            savedDescription << " + " << rawFile.getFileName();
        else
            savedDescription << TR (" (registrazione grezza non salvata)");
    }

    return true;
}

bool ScribaSweepAudioProcessor::exportImpulseWav (const juce::File& file, juce::String& error) const
{
    auto c = getCapture();
    if (c == nullptr)
    {
        error = TR ("Nessuna cattura da esportare.");
        return false;
    }

    return writeWavFile (file.withFileExtension ("wav"), c->ir, c->sampleRate, error);
}

//==============================================================================
void ScribaSweepAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return;

    xml->deleteAllChildElementsWithTagName ("SCRIBA_CAPTURE");
    if (auto c = getCapture())
        xml->addChildElement (captureToXml (*c).release());

    copyXmlToBinary (*xml, destData);
}

void ScribaSweepAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    std::shared_ptr<CaptureResult> c;
    if (auto* capXml = xml->getChildByName ("SCRIBA_CAPTURE"))
        c = captureFromXml (*capXml);

    xml->deleteAllChildElementsWithTagName ("SCRIBA_CAPTURE");

    if (xml->hasTagName (apvts.state.getType()))
        apvts.replaceState (juce::ValueTree::fromXml (*xml));

    if (c != nullptr)
        installCapture (c);
}

//==============================================================================
juce::AudioProcessorEditor* ScribaSweepAudioProcessor::createEditor()
{
    return new ScribaSweepAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ScribaSweepAudioProcessor();
}
