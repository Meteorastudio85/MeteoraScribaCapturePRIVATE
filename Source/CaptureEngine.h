#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <complex>
#include <functional>
#include <memory>
#include <vector>

namespace ScribaConst
{
    constexpr int   numSteps      = 17;      // gradini di livello: da -48 a 0 dBFS
    constexpr float firstStepDb   = -48.0f;
    constexpr float stepSizeDb    = 3.0f;
    constexpr int   displayPoints = 200;     // punti della curva di risposta
}

inline float stepLevelDb (int i) noexcept
{
    return ScribaConst::firstStepDb + ScribaConst::stepSizeDb * (float) i;
}

// Impostazioni della cattura scelte dall'utente
struct CaptureSettings
{
    double sampleRate   = 48000.0;
    float  testLevelDb  = -18.0f;  // picco dello sweep in dBFS
    float  sweepSeconds = 6.0f;
    float  tailSeconds  = 1.0f;    // lunghezza della risposta all'impulso
    bool   dynamicsTest = true;    // gradini di livello + attacco/rilascio
};

// Posizione (in campioni) di ogni sezione del segnale di test
struct StimulusLayout
{
    int preSilence  = 0;
    int sweepStart  = 0;
    int sweepLength = 0;
    int tailLength  = 0;
    int maxLatency  = 0;

    int stepsStart  = 0;
    int stepLength  = 0;

    int arStart  = 0;
    int arQuiet1 = 0;
    int arLoud   = 0;
    int arQuiet2 = 0;

    int total = 0;

    double f1 = 20.0;
    double f2 = 20000.0;
    double sweepL = 1.0;   // costante dello sweep esponenziale: T / ln(f2/f1)
};

// Risultato della cattura: tutto quello che serve per riprodurre l'hardware
struct CaptureResult
{
    juce::String name;
    juce::String warning;

    double sampleRate = 48000.0;
    juce::AudioBuffer<float> ir;     // risposta all'impulso, 1 o 2 canali
    int   preSamples  = 0;           // campioni prima dell'attacco (latenza riportata)
    float testLevelDb = -18.0f;

    float h2Db = -120.0f;            // 2a armonica relativa alla fondamentale
    float h3Db = -120.0f;            // 3a armonica relativa alla fondamentale

    bool  hasDynamics = false;
    std::array<float, ScribaConst::numSteps> curveGainDb {};  // guadagno relativo al livello di test
    float attackMs  = 10.0f;
    float releaseMs = 100.0f;

    float latencyMs = 0.0f;
    float gain1kDb  = 0.0f;
    std::vector<float> responseDb;   // per il display

    // impostazioni e tempi del segnale di test (servono per analisi successive)
    CaptureSettings settings;
    StimulusLayout  layout;

    // registrazione grezza: canale 0 = segnale inviato, canali 1-2 = ritorno.
    // Esiste solo subito dopo una cattura (non viene salvata nella sessione).
    std::shared_ptr<juce::AudioBuffer<float>> raw;
};

namespace CaptureEngine
{
    StimulusLayout makeLayout (const CaptureSettings& s);

    void generateStimulus (const CaptureSettings& s, const StimulusLayout& L, std::vector<float>& out);

    bool analyse (const CaptureSettings& s,
                  const StimulusLayout& L,
                  const juce::AudioBuffer<float>& recording,
                  CaptureResult& result,
                  juce::String& error,
                  const std::function<bool()>& shouldAbort);

    void computeDisplay (CaptureResult& r);

    // FFT radix-2 in place (la dimensione deve essere una potenza di 2); usata anche dalla cattura DYN
    void fftComplex (std::vector<std::complex<float>>& a, bool inverse);

    inline float interpolateCurve (const std::array<float, ScribaConst::numSteps>& c, float levelDb) noexcept
    {
        constexpr int n = ScribaConst::numSteps;
        const float pos = (levelDb - ScribaConst::firstStepDb) / ScribaConst::stepSizeDb;

        if (pos <= 0.0f)
            return c[0];

        if (pos >= (float) (n - 1))
        {
            const float slope = c[(size_t) n - 1] - c[(size_t) n - 2];
            return juce::jmax (-60.0f, c[(size_t) n - 1] + (pos - (float) (n - 1)) * slope);
        }

        const int i = (int) pos;
        const float f = pos - (float) i;
        return c[(size_t) i] + f * (c[(size_t) i + 1] - c[(size_t) i]);
    }
}
