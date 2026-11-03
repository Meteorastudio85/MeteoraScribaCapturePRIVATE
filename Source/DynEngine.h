#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <functional>
#include <memory>
#include <vector>

// Cattura DYN: misura dedicata della compressione di un hardware.
// Segnale di test (tono a 1 kHz):
//   1. breve chirp di sincronismo (misura della latenza) e un secondo di silenzio (rumore di fondo)
//   2. 31 gradini da (max - 60) a max dBFS, a passi di 2 dB  -> curva statica (threshold, ratio, knee)
//   3. 3 tone burst da (max - 40) dB a max-20 / max-10 / max   -> attacco e rilascio

namespace DynConst
{
    constexpr int   numSteps   = 31;
    constexpr float rangeDb    = 60.0f;   // escursione dei gradini sotto il livello massimo
    constexpr float stepDb     = 2.0f;
    constexpr int   numBursts  = 3;
    constexpr float burstQuietBelowMax = 40.0f;
    constexpr float burstLoudBelowMax[numBursts] = { 20.0f, 10.0f, 0.0f };
    constexpr double toneHz    = 1000.0;
}

struct DynSettings
{
    double sampleRate = 48000.0;
    float  maxLevelDb = -6.0f;   // livello del gradino piu' alto e dei burst forti (picco, dBFS)
    int    speed      = 1;       // 0 = veloce, 1 = standard, 2 = lenta
};

struct DynLayout
{
    int syncStart = 0, syncLength = 0;
    int noiseStart = 0, noiseLength = 0;
    int stepsStart = 0, stepLength = 0;
    int burstsStart = 0, burstQuiet = 0, burstLoud = 0;
    int maxLatency = 0;
    int total = 0;

    float firstStepDb = -66.0f;
    float quietDb = -46.0f;
    std::array<float, DynConst::numBursts> loudDb {};

    int loudStart (int b) const noexcept  { return burstsStart + burstQuiet + b * (burstLoud + burstQuiet); }
    int quietStart (int b) const noexcept { return loudStart (b) + burstLoud; }
    float stepLevelDb (int i) const noexcept { return firstStepDb + DynConst::stepDb * (float) i; }
};

struct DynBurst
{
    bool  valid = false;          // il compressore ha ridotto il guadagno in modo misurabile
    float loudDb = 0.0f;          // livello del burst forte (dBFS picco)
    float grDb = 0.0f;            // gain reduction a regime durante il burst
    float attackMs = 0.0f;        // costante di tempo (63%)
    float attack90Ms = 0.0f;      // tempo al 90%
    float releaseMs = 0.0f;       // costante di tempo (63%)
    float release90Ms = 0.0f;     // tempo al 90%
};

struct DynResult
{
    juce::String name;
    juce::String warning;

    DynSettings settings;
    DynLayout   layout;
    double sampleRate = 48000.0;
    int    channel    = 0;        // canale di ritorno analizzato (0 = sinistro / ingresso 1)

    float latencyMs = 0.0f;
    float noiseDb   = -120.0f;    // rumore di fondo del ritorno (RMS, dBFS)

    // curva statica misurata
    std::array<float, DynConst::numSteps> inDb {};
    std::array<float, DynConst::numSteps> outDb {};
    std::array<bool,  DynConst::numSteps> usable {};   // false = troppo vicino al rumore

    // modello adattato: out = in + makeup - GR(in), soft knee
    bool  compresses  = false;
    float makeupDb    = 0.0f;     // guadagno sotto la soglia
    float thresholdDb = 0.0f;
    float ratio       = 1.0f;
    float kneeDb      = 0.0f;
    float maxGrDb     = 0.0f;
    float fitErrorDb  = 0.0f;     // errore RMS del modello

    std::array<DynBurst, DynConst::numBursts> bursts {};
    int displayBurst = -1;        // burst mostrato nel grafico (il piu' forte valido)

    // gain reduction nel tempo del burst mostrato (dB, positivo = riduzione)
    std::vector<float> attackTrace;   // dall'inizio del burst forte, passo attackHopMs
    std::vector<float> releaseTrace;  // dall'inizio del ritorno al livello basso, passo releaseHopMs
    float attackHopMs = 0.1f, releaseHopMs = 1.0f;

    // registrazione grezza: canale 0 = segnale inviato, canali 1-2 = ritorno
    std::shared_ptr<juce::AudioBuffer<float>> raw;

    // gain reduction del modello a un dato livello d'ingresso (dB, positivo)
    float modelGrDb (float levelDb) const noexcept;
    float modelOutDb (float levelDb) const noexcept { return levelDb + makeupDb - modelGrDb (levelDb); }
    float referenceAttackMs() const noexcept;
    float referenceReleaseMs() const noexcept;
};

namespace DynEngine
{
    DynLayout makeLayout (const DynSettings& s);

    void generateStimulus (const DynSettings& s, const DynLayout& L, std::vector<float>& out);

    bool analyse (const DynSettings& s,
                  const DynLayout& L,
                  const std::vector<float>& stimulus,
                  const juce::AudioBuffer<float>& recording,
                  DynResult& result,
                  juce::String& error,
                  const std::function<bool()>& shouldAbort);

    // CSV (separatore ";" e virgola decimale, per Excel in italiano)
    juce::String toCsv (const DynResult& r);

    float softKneeGr (float levelDb, float thresholdDb, float ratio, float kneeDb) noexcept;
}
