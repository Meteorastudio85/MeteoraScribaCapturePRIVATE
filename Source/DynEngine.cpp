#include "Lang.h"
#include "DynEngine.h"
#include "CaptureEngine.h"

#include <cmath>
#include <complex>

namespace
{
    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr double kPi    = 3.141592653589793238463;

    constexpr float kStepSeconds[]  = { 0.5f, 1.0f, 2.0f };
    constexpr float kQuietSeconds[] = { 2.0f, 4.0f, 8.0f };
    constexpr float kLoudSeconds[]  = { 1.0f, 1.5f, 3.0f };

    constexpr double kSyncSeconds  = 0.3;
    constexpr double kSyncF1 = 200.0, kSyncF2 = 8000.0;
    constexpr float  kSyncBelowMax = 20.0f;

    double chirpPhase (int n, double sr, int len)
    {
        const double T = (double) len / sr;
        const double L = T / std::log (kSyncF2 / kSyncF1);
        return kTwoPi * kSyncF1 * L * (std::exp (((double) n / sr) / L) - 1.0);
    }

    float chirpSample (int n, double sr, int len)
    {
        const int fade = juce::jmax (1, len / 10);
        float w = 1.0f;
        if (n < fade)              w = (float) (0.5 * (1.0 - std::cos (kPi * (double) n / (double) fade)));
        else if (len - 1 - n < fade) w = (float) (0.5 * (1.0 - std::cos (kPi * (double) (len - 1 - n) / (double) fade)));
        return w * (float) std::sin (chirpPhase (n, sr, len));
    }

    float rmsRange (const float* d, int start, int len, int total)
    {
        const int a = juce::jlimit (0, total, start);
        const int b = juce::jlimit (0, total, start + len);
        if (b <= a)
            return 0.0f;

        double acc = 0.0;
        for (int i = a; i < b; ++i)
            acc += (double) d[i] * (double) d[i];

        return (float) std::sqrt (acc / (double) (b - a));
    }

    // guadagno di un tono sinusoidale di ampiezza di picco amp, dato l'RMS dell'uscita
    float toneGainDb (float outRms, float amp)
    {
        const float inRms = amp * 0.70710678f;
        if (outRms < 1.0e-8f || inRms <= 0.0f)
            return -120.0f;
        return 20.0f * std::log10 (outRms / inRms);
    }

    // RMS scorrevole veloce tramite somme cumulative su una zona della registrazione
    struct RunningRms
    {
        RunningRms (const float* d, int start, int len, int total)
            : base (juce::jlimit (0, total, start))
        {
            const int end = juce::jlimit (0, total, start + len);
            sums.assign ((size_t) juce::jmax (0, end - base) + 1, 0.0);
            for (int i = base; i < end; ++i)
                sums[(size_t) (i - base + 1)] = sums[(size_t) (i - base)] + (double) d[i] * (double) d[i];
        }

        float rms (int start, int len) const
        {
            const int a = juce::jlimit (0, (int) sums.size() - 1, start - base);
            const int b = juce::jlimit (0, (int) sums.size() - 1, start - base + len);
            if (b <= a)
                return 0.0f;
            return (float) std::sqrt (juce::jmax (0.0, sums[(size_t) b] - sums[(size_t) a]) / (double) (b - a));
        }

        int base;
        std::vector<double> sums;
    };

    juce::String num (double v, int decimals)
    {
        return juce::String (v, decimals).replaceCharacter ('.', ',');
    }
}

//==============================================================================
float DynEngine::softKneeGr (float x, float T, float R, float W) noexcept
{
    const float slope = 1.0f - 1.0f / juce::jmax (1.0f, R);
    const float d = x - T;

    if (W > 0.01f)
    {
        if (2.0f * d < -W) return 0.0f;
        if (2.0f * d <= W) return slope * (d + 0.5f * W) * (d + 0.5f * W) / (2.0f * W);
        return slope * d;
    }

    return d > 0.0f ? slope * d : 0.0f;
}

float DynResult::modelGrDb (float levelDb) const noexcept
{
    if (! compresses)
        return 0.0f;
    return DynEngine::softKneeGr (levelDb, thresholdDb, ratio, kneeDb);
}

float DynResult::referenceAttackMs() const noexcept
{
    if (displayBurst >= 0)
        return bursts[(size_t) displayBurst].attackMs;
    return 10.0f;
}

float DynResult::referenceReleaseMs() const noexcept
{
    if (displayBurst >= 0)
        return bursts[(size_t) displayBurst].releaseMs;
    return 100.0f;
}

//==============================================================================
namespace DynEngine
{

DynLayout makeLayout (const DynSettings& s)
{
    DynLayout L;
    const double sr = s.sampleRate;
    const int sp = juce::jlimit (0, 2, s.speed);
    auto secs = [sr] (double x) { return (int) std::round (x * sr); };

    L.maxLatency = secs (1.0);

    L.syncStart  = secs (0.5);
    L.syncLength = secs (kSyncSeconds);

    L.noiseStart  = L.syncStart + L.syncLength + L.maxLatency;
    L.noiseLength = secs (1.0);

    L.stepsStart = L.noiseStart + L.noiseLength;
    L.stepLength = secs (kStepSeconds[sp]);

    L.burstsStart = L.stepsStart + DynConst::numSteps * L.stepLength;
    L.burstQuiet  = secs (kQuietSeconds[sp]);
    L.burstLoud   = secs (kLoudSeconds[sp]);

    L.total = L.burstsStart + DynConst::numBursts * (L.burstQuiet + L.burstLoud) + L.burstQuiet + L.maxLatency;

    L.firstStepDb = s.maxLevelDb - DynConst::rangeDb;
    L.quietDb     = s.maxLevelDb - DynConst::burstQuietBelowMax;
    for (int b = 0; b < DynConst::numBursts; ++b)
        L.loudDb[(size_t) b] = s.maxLevelDb - DynConst::burstLoudBelowMax[b];

    return L;
}

void generateStimulus (const DynSettings& s, const DynLayout& L, std::vector<float>& out)
{
    const double sr = s.sampleRate;
    out.assign ((size_t) L.total, 0.0f);

    // chirp di sincronismo
    const float syncAmp = juce::Decibels::decibelsToGain (s.maxLevelDb - kSyncBelowMax);
    for (int n = 0; n < L.syncLength; ++n)
        out[(size_t) (L.syncStart + n)] = syncAmp * chirpSample (n, sr, L.syncLength);

    auto tone = [sr] (int n) { return (float) std::sin (kTwoPi * DynConst::toneHz * (double) n / sr); };

    // rampa breve all'inizio e alla fine di ogni blocco di tono (evita click)
    const int ramp = juce::jmax (1, (int) (0.005 * sr));

    // gradini (fase continua tra un gradino e l'altro)
    const int stepsEnd = L.stepsStart + DynConst::numSteps * L.stepLength;
    for (int i = 0; i < DynConst::numSteps; ++i)
    {
        const float amp = juce::Decibels::decibelsToGain (L.stepLevelDb (i));
        const int start = L.stepsStart + i * L.stepLength;
        for (int n = start; n < start + L.stepLength; ++n)
            out[(size_t) n] = amp * tone (n);
    }

    for (int i = 0; i < ramp; ++i)
        out[(size_t) (L.stepsStart + i)] *= (float) i / (float) ramp;

    // burst: basso, forte, basso, forte, ... basso (subito dopo i gradini, senza interruzioni)
    const float quiet = juce::Decibels::decibelsToGain (L.quietDb);
    const int burstsEnd = L.burstsStart + DynConst::numBursts * (L.burstQuiet + L.burstLoud) + L.burstQuiet;

    for (int n = stepsEnd; n < burstsEnd; ++n)
        out[(size_t) n] = quiet * tone (n);

    for (int b = 0; b < DynConst::numBursts; ++b)
    {
        const float loud = juce::Decibels::decibelsToGain (L.loudDb[(size_t) b]);
        const int start = L.loudStart (b);
        for (int n = start; n < start + L.burstLoud; ++n)
            out[(size_t) n] = loud * tone (n);
    }

    for (int i = 0; i < ramp; ++i)
        out[(size_t) (burstsEnd - 1 - i)] *= (float) i / (float) ramp;
}

//==============================================================================
bool analyse (const DynSettings& s,
              const DynLayout& L,
              const std::vector<float>& stimulus,
              const juce::AudioBuffer<float>& rec,
              DynResult& r,
              juce::String& error,
              const std::function<bool()>& shouldAbort)
{
    const double sr = s.sampleRate;
    const int total = rec.getNumSamples();

    if (rec.getNumChannels() == 0 || total < L.total || (int) stimulus.size() < L.total)
    {
        error = TR ("Registrazione DYN incompleta: riprova la cattura.");
        return false;
    }

    r.settings   = s;
    r.layout     = L;
    r.sampleRate = sr;

    // canale di ritorno: quello con piu' segnale durante i gradini
    const int stepsLen = DynConst::numSteps * L.stepLength;
    int ch = 0;
    float best = -1.0f;
    for (int c = 0; c < juce::jmin (2, rec.getNumChannels()); ++c)
    {
        const float e = rec.getRMSLevel (c, L.stepsStart, stepsLen);
        if (e > best) { best = e; ch = c; }
    }
    r.channel = ch;

    const float* d = rec.getReadPointer (ch);

    if (rec.getMagnitude (ch, L.stepsStart, total - L.stepsStart) < juce::Decibels::decibelsToGain (-60.0f))
    {
        error = TR ("Nessun segnale di ritorno: controlla collegamenti e routing dell'ingresso.");
        return false;
    }

    float overall = 0.0f;
    for (int c = 0; c < rec.getNumChannels(); ++c)
        overall = juce::jmax (overall, rec.getMagnitude (c, 0, total));
    if (overall >= 0.99f)
        r.warning = TR ("Cattura DYN fatta, ma il ritorno ha clippato: abbassa il LIVELLO MAX.");

    if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }

    // ---------------- latenza: correlazione con il chirp di sincronismo ----------------
    int dl = 0;
    {
        const int segLen = L.syncLength + L.maxLatency;
        size_t N = 1;
        while (N < (size_t) (segLen + L.syncLength))
            N <<= 1;

        std::vector<std::complex<float>> a (N), b (N);
        for (int n = 0; n < segLen; ++n)
            a[(size_t) n] = { d[L.syncStart + n], 0.0f };
        for (int n = 0; n < L.syncLength; ++n)
            b[(size_t) n] = { stimulus[(size_t) (L.syncStart + n)], 0.0f };

        CaptureEngine::fftComplex (a, false);
        CaptureEngine::fftComplex (b, false);
        for (size_t k = 0; k < N; ++k)
            a[k] *= std::conj (b[k]);
        CaptureEngine::fftComplex (a, true);

        float peak = 0.0f;
        for (int lag = 0; lag < L.maxLatency; ++lag)
        {
            const float v = std::abs (a[(size_t) lag].real());
            if (v > peak) { peak = v; dl = lag; }
        }

        if (peak <= 0.0f)
        {
            error = TR ("Sincronismo non trovato: il ritorno non contiene il segnale di test.");
            return false;
        }
    }
    r.latencyMs = (float) (1000.0 * (double) dl / sr);

    if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }

    // ---------------- rumore di fondo ----------------
    const float noiseRms = rmsRange (d, L.noiseStart + dl + L.noiseLength / 5, L.noiseLength * 3 / 5, total);
    r.noiseDb = juce::Decibels::gainToDecibels (noiseRms, -120.0f);

    // ---------------- curva statica ----------------
    std::vector<float> fitIn, fitGain;
    for (int i = 0; i < DynConst::numSteps; ++i)
    {
        const float lvl = L.stepLevelDb (i);
        const float amp = juce::Decibels::decibelsToGain (lvl);
        const int st = L.stepsStart + i * L.stepLength + dl;
        // ultimo 40% del gradino: il compressore ha gia' raggiunto il regime
        const float outRms = rmsRange (d, st + (int) (0.55 * L.stepLength), (int) (0.40 * L.stepLength), total);
        const float g = toneGainDb (outRms, amp);

        r.inDb[(size_t) i]   = lvl;
        r.outDb[(size_t) i]  = lvl + g;
        r.usable[(size_t) i] = outRms > noiseRms * 5.6f;   // almeno 15 dB sopra il rumore

        if (r.usable[(size_t) i])
        {
            fitIn.push_back (lvl);
            fitGain.push_back (g);
        }
    }

    if (fitIn.size() < 6)
    {
        error = TR ("Ritorno troppo vicino al rumore: alza il LIVELLO MAX o il guadagno d'ingresso.");
        return false;
    }

    // ---------------- adattamento del modello (ricerca su griglia) ----------------
    {
        const float ratios[] = { 1.05f, 1.1f, 1.2f, 1.3f, 1.5f, 1.7f, 2.0f, 2.5f, 3.0f, 4.0f, 5.0f,
                                 6.0f, 8.0f, 10.0f, 12.0f, 15.0f, 20.0f, 30.0f, 50.0f, 100.0f };
        const int n = (int) fitIn.size();
        const float lo = fitIn.front() - 2.0f, hi = fitIn.back() + 2.0f;

        // riferimento senza compressione: guadagno medio costante
        double meanG = 0.0;
        for (auto g : fitGain) meanG += g;
        meanG /= n;
        double flatErr = 0.0;
        for (auto g : fitGain) flatErr += ((double) g - meanG) * ((double) g - meanG);

        double bestErr = flatErr;
        float bT = 0.0f, bR = 1.0f, bW = 0.0f, bM = (float) meanG;
        bool found = false;

        std::vector<float> gr ((size_t) n);
        for (float T = lo; T <= hi; T += 0.5f)
        {
            for (float R : ratios)
            {
                for (float W = 0.0f; W <= 24.0f; W += 2.0f)
                {
                    double m = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        gr[(size_t) i] = softKneeGr (fitIn[(size_t) i], T, R, W);
                        m += (double) fitGain[(size_t) i] + gr[(size_t) i];
                    }
                    m /= n;

                    double e = 0.0;
                    for (int i = 0; i < n; ++i)
                    {
                        const double diff = (double) fitGain[(size_t) i] - (m - gr[(size_t) i]);
                        e += diff * diff;
                    }

                    if (e < bestErr)
                    {
                        bestErr = e; bT = T; bR = R; bW = W; bM = (float) m; found = true;
                    }
                }
            }

            if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }
        }

        r.makeupDb    = bM;
        r.thresholdDb = bT;
        r.ratio       = bR;
        r.kneeDb      = bW;
        r.fitErrorDb  = (float) std::sqrt (bestErr / n);
        r.compresses  = found;

        const float topGr = found ? softKneeGr (fitIn.back(), bT, bR, bW) : 0.0f;
        r.maxGrDb = topGr;

        // meno di 1 dB di riduzione al livello massimo: non e' compressione
        if (! found || topGr < 1.0f || bR < 1.1f)
        {
            r.compresses  = false;
            r.makeupDb    = (float) meanG;
            r.thresholdDb = 0.0f;
            r.ratio       = 1.0f;
            r.kneeDb      = 0.0f;
            r.maxGrDb     = 0.0f;
            r.fitErrorDb  = (float) std::sqrt (flatErr / n);
        }
    }

    // ---------------- attacco e rilascio ----------------
    const int win = juce::jmax (8, (int) std::round (2.0 * sr / DynConst::toneHz));   // due periodi del tono
    const float quietAmp = juce::Decibels::decibelsToGain (L.quietDb);

    // guadagno "a riposo" al livello basso, preso dalla curva statica (gradino allo stesso livello):
    // e' il punto d'arrivo del rilascio anche quando il rilascio e' piu' lungo del test
    const int quietStep = juce::jlimit (0, DynConst::numSteps - 1,
                                        (int) std::round ((L.quietDb - L.firstStepDb) / DynConst::stepDb));
    const float gRest = r.outDb[(size_t) quietStep] - r.inDb[(size_t) quietStep];
    bool releaseIncomplete = false;

    for (int b = 0; b < DynConst::numBursts; ++b)
    {
        auto& B = r.bursts[(size_t) b];
        B = {};
        B.loudDb = L.loudDb[(size_t) b];

        const float loudAmp = juce::Decibels::decibelsToGain (B.loudDb);
        const int ls = L.loudStart (b) + dl;
        const int qs = L.quietStart (b) + dl;

        const int tailQ = L.burstQuiet / 5;
        const int tailL = L.burstLoud / 5;

        const float gBefore = toneGainDb (rmsRange (d, ls - tailQ, tailQ, total), quietAmp);
        const float gLoud   = toneGainDb (rmsRange (d, qs - tailL, tailL, total), loudAmp);
        const float gAfter  = toneGainDb (rmsRange (d, qs + L.burstQuiet - tailQ, tailQ, total), quietAmp);

        const float grLoud = gBefore - gLoud;
        B.grDb = juce::jmax (0.0f, grLoud);

        const float quietOut = rmsRange (d, ls - tailQ, tailQ, total);
        if (grLoud < 1.0f || quietOut < noiseRms * 3.0f)
            continue;

        B.valid = true;

        // attacco: dal guadagno prima del burst a quello a regime
        RunningRms runA (d, ls, L.burstLoud + win, total);
        const int hopA = juce::jmax (1, (int) std::round (0.0001 * sr));
        auto gainA = [&] (int p) { return toneGainDb (runA.rms (p, win), loudAmp); };

        auto findTime = [] (auto&& gainFn, int startPos, int endPos, int hop, int w, float from, float to, float frac, double rate)
        {
            const float target = from + frac * (to - from);
            const bool falling = to < from;
            for (int p = startPos; p < endPos - w; p += hop)
            {
                const float g = gainFn (p);
                if (falling ? g <= target : g >= target)
                    return (float) (1000.0 * (double) (p - startPos + w / 2) / rate);
            }
            return -1.0f;
        };

        const float a63 = findTime (gainA, ls, ls + L.burstLoud, hopA, win, gBefore, gLoud, 0.632f, sr);
        const float a90 = findTime (gainA, ls, ls + L.burstLoud, hopA, win, gBefore, gLoud, 0.9f, sr);
        B.attackMs   = a63 >= 0.0f ? juce::jmax (0.1f, a63) : (float) (1000.0 * L.burstLoud / sr);
        B.attack90Ms = a90 >= 0.0f ? juce::jmax (B.attackMs, a90) : (float) (1000.0 * L.burstLoud / sr);

        // rilascio: dal guadagno compresso a quello a riposo
        RunningRms runR (d, qs, L.burstQuiet + win, total);
        const int hopR = juce::jmax (1, (int) std::round (0.0005 * sr));
        auto gainR = [&] (int p) { return toneGainDb (runR.rms (p, win), quietAmp); };

        const float relTarget = juce::jmax (gRest, gLoud + 0.5f);
        const float r63 = findTime (gainR, qs, qs + L.burstQuiet, hopR, win, gLoud, relTarget, 0.632f, sr);
        const float r90 = findTime (gainR, qs, qs + L.burstQuiet, hopR, win, gLoud, relTarget, 0.9f, sr);
        B.releaseMs   = r63 >= 0.0f ? juce::jmax (1.0f, r63) : (float) (1000.0 * L.burstQuiet / sr);
        B.release90Ms = r90 >= 0.0f ? juce::jmax (B.releaseMs, r90) : (float) (1000.0 * L.burstQuiet / sr);

        if (r90 < 0.0f || gAfter < relTarget - 0.1f * (relTarget - gLoud))
            releaseIncomplete = true;

        // traccia per il grafico (solo il burst valido piu' forte)
        r.displayBurst = b;
        r.attackTrace.clear();
        r.releaseTrace.clear();

        const int hopTA = juce::jmax (1, (int) std::round (0.0001 * sr));
        r.attackHopMs = (float) (1000.0 * hopTA / sr);
        for (int p = ls; p < ls + L.burstLoud - win; p += hopTA)
            r.attackTrace.push_back (juce::jmax (0.0f, gBefore - gainA (p)));

        const int hopTR = juce::jmax (1, (int) std::round (0.001 * sr));
        r.releaseHopMs = (float) (1000.0 * hopTR / sr);
        for (int p = qs; p < qs + L.burstQuiet - win; p += hopTR)
            r.releaseTrace.push_back (juce::jmax (0.0f, gBefore - gainR (p)));

        if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }
    }

    if (releaseIncomplete)
        r.warning = TR ("Il rilascio non si completa nel tempo del test: rifai la cattura con DURATA lenta.");

    if (r.compresses && r.displayBurst < 0)
        r.warning = TR ("Curva misurata, ma i burst non comprimono: alza il LIVELLO MAX o abbassa la threshold.");

    return true;
}

//==============================================================================
juce::String toCsv (const DynResult& r)
{
    juce::String csv;
    const juce::String nl ("\r\n");

    csv << TR ("Scriba Capture - cattura DYN;") << r.name << nl;
    csv << "Sample rate (Hz);" << num (r.sampleRate, 0) << nl;
    csv << TR ("Livello massimo (dBFS);") << num (r.settings.maxLevelDb, 1) << nl;
    csv << TR ("Canale analizzato;") << (r.channel + 1) << nl;
    csv << TR ("Latenza (ms);") << num (r.latencyMs, 2) << nl;
    csv << TR ("Rumore di fondo (dBFS RMS);") << num (r.noiseDb, 1) << nl;
    csv << nl;

    csv << TR ("MODELLO;") << (r.compresses ? TR ("compressione rilevata") : TR ("nessuna compressione")) << nl;
    csv << "Threshold (dBFS);" << num (r.thresholdDb, 1) << nl;
    csv << "Ratio;" << num (r.ratio, 2) << nl;
    csv << "Knee (dB);" << num (r.kneeDb, 1) << nl;
    csv << TR ("Guadagno sotto soglia (dB);") << num (r.makeupDb, 2) << nl;
    csv << TR ("Gain reduction max (dB);") << num (r.maxGrDb, 1) << nl;
    csv << TR ("Errore del modello (dB RMS);") << num (r.fitErrorDb, 2) << nl;
    csv << nl;

    csv << TR ("CURVA STATICA (tono 1 kHz)") << nl;
    csv << TR ("Ingresso (dBFS);Uscita (dBFS);Guadagno (dB);Gain reduction misurata (dB);Gain reduction modello (dB);Usabile") << nl;
    for (int i = 0; i < DynConst::numSteps; ++i)
    {
        const float in = r.inDb[(size_t) i], out = r.outDb[(size_t) i];
        const float g = out - in;
        csv << num (in, 1) << ";" << num (out, 2) << ";" << num (g, 2) << ";"
            << num (r.makeupDb - g, 2) << ";" << num (r.modelGrDb (in), 2) << ";"
            << (r.usable[(size_t) i] ? TR ("si") : TR ("no (rumore)")) << nl;
    }
    csv << nl;

    csv << TR ("ATTACCO E RILASCIO (da ") << num (r.layout.quietDb, 1) << TR (" dBFS al livello del burst)") << nl;
    csv << TR ("Burst (dBFS);Gain reduction (dB);Attacco 63% (ms);Attacco 90% (ms);Rilascio 63% (ms);Rilascio 90% (ms)") << nl;
    for (const auto& B : r.bursts)
    {
        csv << num (B.loudDb, 1) << ";" << num (B.grDb, 2) << ";";
        if (B.valid)
            csv << num (B.attackMs, 2) << ";" << num (B.attack90Ms, 2) << ";" << num (B.releaseMs, 1) << ";" << num (B.release90Ms, 1);
        else
            csv << "-;-;-;-";
        csv << nl;
    }

    if (! r.attackTrace.empty())
    {
        csv << nl << TR ("INVILUPPO ATTACCO (burst ") << num (r.bursts[(size_t) r.displayBurst].loudDb, 1) << " dBFS)" << nl;
        csv << TR ("Tempo (ms);Gain reduction (dB)") << nl;
        const int stepA = juce::jmax (1, (int) (r.attackTrace.size() / 1500));
        for (size_t i = 0; i < r.attackTrace.size(); i += (size_t) stepA)
            csv << num (r.attackHopMs * (double) i, 2) << ";" << num (r.attackTrace[i], 2) << nl;

        csv << nl << TR ("INVILUPPO RILASCIO") << nl;
        csv << TR ("Tempo (ms);Gain reduction (dB)") << nl;
        const int stepR = juce::jmax (1, (int) (r.releaseTrace.size() / 1500));
        for (size_t i = 0; i < r.releaseTrace.size(); i += (size_t) stepR)
            csv << num (r.releaseHopMs * (double) i, 1) << ";" << num (r.releaseTrace[i], 2) << nl;
    }

    return csv;
}

} // namespace DynEngine
