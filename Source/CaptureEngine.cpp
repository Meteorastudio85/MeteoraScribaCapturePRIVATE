#include "Lang.h"
#include "CaptureEngine.h"

#include <cmath>
#include <complex>

namespace
{
    using cfloat = std::complex<float>;

    constexpr double kTwoPi = 6.283185307179586476925;
    constexpr double kPi    = 3.141592653589793238463;

    size_t nextPow2 (size_t n)
    {
        size_t p = 1;
        while (p < n)
            p <<= 1;
        return p;
    }

    inline cfloat cmul (cfloat a, cfloat b) noexcept
    {
        return { a.real() * b.real() - a.imag() * b.imag(),
                 a.real() * b.imag() + a.imag() * b.real() };
    }

    // FFT radix-2 iterativa (nessun limite di dimensione)
    void fft (std::vector<cfloat>& a, bool inverse)
    {
        const size_t n = a.size();
        if (n < 2)
            return;

        for (size_t i = 1, j = 0; i < n; ++i)
        {
            size_t bit = n >> 1;
            for (; (j & bit) != 0; bit >>= 1)
                j ^= bit;
            j ^= bit;

            if (i < j)
                std::swap (a[i], a[j]);
        }

        std::vector<cfloat> tw;

        for (size_t len = 2; len <= n; len <<= 1)
        {
            const size_t half = len >> 1;
            const double ang = (inverse ? kTwoPi : -kTwoPi) / (double) len;

            tw.resize (half);
            for (size_t k = 0; k < half; ++k)
                tw[k] = cfloat ((float) std::cos (ang * (double) k), (float) std::sin (ang * (double) k));

            for (size_t i = 0; i < n; i += len)
            {
                for (size_t k = 0; k < half; ++k)
                {
                    const cfloat u = a[i + k];
                    const cfloat v = cmul (a[i + k + half], tw[k]);
                    a[i + k]        = u + v;
                    a[i + k + half] = u - v;
                }
            }
        }

        if (inverse)
        {
            const float s = 1.0f / (float) n;
            for (auto& x : a)
                x *= s;
        }
    }

    double sweepPhase (const StimulusLayout& L, int n, double sr)
    {
        const double t = (double) n / sr;
        return kTwoPi * L.f1 * L.sweepL * (std::exp (t / L.sweepL) - 1.0);
    }

    float sweepFade (const StimulusLayout& L, int n, double sr)
    {
        const int fadeIn  = juce::jmax (1, (int) (0.01 * sr));
        const int fadeOut = juce::jmax (1, (int) (0.002 * sr));

        if (n < fadeIn)
            return (float) (0.5 * (1.0 - std::cos (kPi * (double) n / (double) fadeIn)));

        const int fromEnd = L.sweepLength - 1 - n;
        if (fromEnd < fadeOut)
            return (float) (0.5 * (1.0 - std::cos (kPi * (double) fromEnd / (double) fadeOut)));

        return 1.0f;
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

    float toneGainDb (float outRms, float amp)
    {
        const float inRms = amp * 0.70710678f;
        if (outRms < 1.0e-7f || inRms <= 0.0f)
            return -100.0f;
        return 20.0f * std::log10 (outRms / inRms);
    }
}

namespace CaptureEngine
{

void fftComplex (std::vector<std::complex<float>>& a, bool inverse)
{
    fft (a, inverse);
}

StimulusLayout makeLayout (const CaptureSettings& s)
{
    StimulusLayout L;
    const double sr = s.sampleRate;
    auto secs = [sr] (double x) { return (int) std::round (x * sr); };

    L.f1 = 20.0;
    L.f2 = juce::jmin (20000.0, 0.45 * sr);

    L.preSilence  = secs (0.5);
    L.sweepStart  = L.preSilence;
    L.sweepLength = secs (s.sweepSeconds);
    L.sweepL      = ((double) L.sweepLength / sr) / std::log (L.f2 / L.f1);
    L.tailLength  = secs (juce::jmax (0.5, (double) s.tailSeconds));
    L.maxLatency  = secs (1.0);

    int cursor = L.sweepStart + L.sweepLength + L.tailLength + L.maxLatency;

    if (s.dynamicsTest)
    {
        L.stepsStart = cursor;
        L.stepLength = secs (0.3);
        cursor += ScribaConst::numSteps * L.stepLength;

        L.arStart  = cursor;
        L.arQuiet1 = secs (2.0);
        L.arLoud   = secs (1.5);
        L.arQuiet2 = secs (2.0);
        cursor += L.arQuiet1 + L.arLoud + L.arQuiet2;
    }

    L.total = cursor + L.maxLatency;
    return L;
}

void generateStimulus (const CaptureSettings& s, const StimulusLayout& L, std::vector<float>& out)
{
    const double sr = s.sampleRate;
    out.assign ((size_t) L.total, 0.0f);

    const float A = juce::Decibels::decibelsToGain (s.testLevelDb);

    // sweep sinusoidale esponenziale
    for (int n = 0; n < L.sweepLength; ++n)
        out[(size_t) (L.sweepStart + n)] = A * sweepFade (L, n, sr) * (float) std::sin (sweepPhase (L, n, sr));

    if (! s.dynamicsTest)
        return;

    auto tone = [sr] (int n) { return (float) std::sin (kTwoPi * 1000.0 * (double) n / sr); };

    // gradini di livello a 1 kHz
    for (int i = 0; i < ScribaConst::numSteps; ++i)
    {
        const float amp = juce::Decibels::decibelsToGain (stepLevelDb (i));
        const int start = L.stepsStart + i * L.stepLength;
        for (int n = start; n < start + L.stepLength; ++n)
            out[(size_t) n] = amp * tone (n);
    }

    // test attacco / rilascio: -30 dB -> -6 dB -> -30 dB
    const float quiet = juce::Decibels::decibelsToGain (-30.0f);
    const float loud  = juce::Decibels::decibelsToGain (-6.0f);

    int n = L.arStart;
    for (int i = 0; i < L.arQuiet1; ++i, ++n) out[(size_t) n] = quiet * tone (n);
    for (int i = 0; i < L.arLoud;   ++i, ++n) out[(size_t) n] = loud  * tone (n);
    for (int i = 0; i < L.arQuiet2; ++i, ++n) out[(size_t) n] = quiet * tone (n);
}

bool analyse (const CaptureSettings& s,
              const StimulusLayout& L,
              const juce::AudioBuffer<float>& rec,
              CaptureResult& r,
              juce::String& error,
              const std::function<bool()>& shouldAbort)
{
    const double sr = s.sampleRate;
    const int Ns = L.sweepLength;
    const int segLen = Ns + L.tailLength + L.maxLatency;
    const int total = rec.getNumSamples();

    if (rec.getNumChannels() == 0 || total < L.sweepStart + segLen)
    {
        error = TR ("Registrazione incompleta: riprova la cattura.");
        return false;
    }

    // controllo livelli
    const float retPeak = rec.getMagnitude (0, L.sweepStart, segLen);
    if (retPeak < juce::Decibels::decibelsToGain (-60.0f))
    {
        error = TR ("Nessun segnale di ritorno: controlla collegamenti e routing dell'ingresso.");
        return false;
    }

    float overall = 0.0f;
    for (int ch = 0; ch < rec.getNumChannels(); ++ch)
        overall = juce::jmax (overall, rec.getMagnitude (ch, 0, total));

    if (overall >= 0.99f)
        r.warning = TR ("Cattura fatta, ma il ritorno ha clippato: abbassa il livello di test.");

    const size_t N = nextPow2 ((size_t) segLen + (size_t) Ns);
    const float A = juce::Decibels::decibelsToGain (s.testLevelDb);

    // filtro inverso (sweep rovesciato con enfasi di +6 dB/ottava)
    std::vector<cfloat> F (N, cfloat {});
    for (int n = 0; n < Ns; ++n)
    {
        const int m = Ns - 1 - n;
        const double env = std::exp (((double) m / sr) / L.sweepL);
        F[(size_t) n] = cfloat ((float) (std::sin (sweepPhase (L, m, sr)) * env), 0.0f);
    }
    fft (F, false);

    if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }

    // normalizzazione: lo sweep di riferimento deve dare guadagno 1 a 1 kHz
    std::vector<cfloat> work (N, cfloat {});
    for (int n = 0; n < Ns; ++n)
        work[(size_t) n] = cfloat (A * sweepFade (L, n, sr) * (float) std::sin (sweepPhase (L, n, sr)), 0.0f);
    fft (work, false);

    double norm = 0.0;
    int count = 0;
    const int b0 = (int) std::floor (900.0 * (double) N / sr);
    const int b1 = (int) std::ceil (1100.0 * (double) N / sr);
    for (int b = b0; b <= b1; ++b)
    {
        norm += (double) std::abs (cmul (work[(size_t) b], F[(size_t) b]));
        ++count;
    }
    norm /= (double) juce::jmax (1, count);

    if (norm <= 0.0)
    {
        error = TR ("Errore di normalizzazione.");
        return false;
    }

    if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }

    // deconvoluzione di ogni canale registrato
    const int numCh = juce::jmin (2, rec.getNumChannels());
    std::vector<std::vector<float>> h ((size_t) numCh);
    const float invNorm = (float) (1.0 / norm);

    for (int ch = 0; ch < numCh; ++ch)
    {
        std::fill (work.begin(), work.end(), cfloat {});
        const float* d = rec.getReadPointer (ch, L.sweepStart);
        for (int n = 0; n < segLen; ++n)
            work[(size_t) n] = cfloat (d[n], 0.0f);

        fft (work, false);
        for (size_t k = 0; k < N; ++k)
            work[k] = cmul (work[k], F[k]) * invNorm;
        fft (work, true);

        auto& hc = h[(size_t) ch];
        hc.resize (N);
        for (size_t k = 0; k < N; ++k)
            hc[k] = work[k].real();

        if (shouldAbort()) { error = TR ("Analisi interrotta."); return false; }
    }

    F.clear();  F.shrink_to_fit();
    work.clear(); work.shrink_to_fit();

    const auto& h0 = h[0];

    // picco della risposta lineare = latenza di andata e ritorno
    const int searchStart = Ns - 1;
    const int searchEnd   = juce::jmin ((int) N - 1, Ns - 1 + L.maxLatency);
    int peakIdx = searchStart;
    float peakAbs = 0.0f;
    for (int i = searchStart; i < searchEnd; ++i)
    {
        const float a = std::abs (h0[(size_t) i]);
        if (a > peakAbs) { peakAbs = a; peakIdx = i; }
    }

    if (peakAbs < 1.0e-5f)
    {
        error = TR ("Risposta non trovata: il ritorno non contiene lo sweep.");
        return false;
    }

    int onset = peakIdx;
    const int back = (int) (0.002 * sr);
    for (int i = peakIdx; i >= juce::jmax (0, peakIdx - back); --i)
        if (std::abs (h0[(size_t) i]) > 0.02f * peakAbs)
            onset = i;

    const int pre   = juce::jmax (8, (int) std::round (0.0005 * sr));
    const int start = juce::jmax (0, onset - pre);

    r.sampleRate  = sr;
    r.testLevelDb = s.testLevelDb;
    r.preSamples  = onset - start;
    r.latencyMs   = (float) (1000.0 * (double) (onset - (Ns - 1)) / sr);

    int irLen = (int) std::round ((double) s.tailSeconds * sr) + r.preSamples;
    irLen = juce::jmin (irLen, (int) N - start);

    // stereo o mono?
    bool stereo = false;
    if (numCh > 1)
    {
        double e0 = 0.0, e1 = 0.0;
        for (int i = start; i < start + irLen; ++i)
        {
            e0 += (double) h[0][(size_t) i] * h[0][(size_t) i];
            e1 += (double) h[1][(size_t) i] * h[1][(size_t) i];
        }
        stereo = e1 > e0 * 1.0e-4;
    }

    r.ir.setSize (stereo ? 2 : 1, irLen);
    const int fadeOut = juce::jmax (64, irLen / 10);

    for (int ch = 0; ch < r.ir.getNumChannels(); ++ch)
    {
        float* dst = r.ir.getWritePointer (ch);
        const auto& src = h[(size_t) ch];

        for (int i = 0; i < irLen; ++i)
            dst[i] = src[(size_t) (start + i)];

        for (int i = 0; i < r.preSamples; ++i)
            dst[i] *= (float) (0.5 * (1.0 - std::cos (kPi * (double) i / (double) juce::jmax (1, r.preSamples))));

        for (int i = 0; i < fadeOut && i < irLen; ++i)
            dst[irLen - 1 - i] *= (float) (0.5 * (1.0 - std::cos (kPi * (double) i / (double) fadeOut)));
    }

    // armoniche (risposte che cadono prima della lineare)
    auto energyAt = [&] (int centre) -> double
    {
        const int a = centre - (int) (0.001 * sr);
        const int b = centre + (int) (0.009 * sr);
        if (a < 0 || b >= (int) N)
            return -1.0;

        double e = 0.0;
        for (int i = a; i < b; ++i)
            e += (double) h0[(size_t) i] * h0[(size_t) i];
        return e;
    };

    const double e1 = energyAt (peakIdx);
    const double en = juce::jmax (0.0, energyAt (peakIdx - (int) std::round (L.sweepL * std::log (7.5) * sr)));

    auto harmonicDb = [&] (int k) -> float
    {
        const double ek = energyAt (peakIdx - (int) std::round (L.sweepL * std::log ((double) k) * sr));
        if (ek < 0.0 || e1 <= 0.0)
            return -120.0f;

        const double ratio = std::sqrt (juce::jmax (0.0, ek - en) / e1);
        if (ratio < 1.0e-4)
            return -120.0f;

        return juce::jmin (-6.0f, (float) (20.0 * std::log10 (ratio)));
    };

    r.h2Db = harmonicDb (2);
    r.h3Db = harmonicDb (3);

    // dinamica: curva statica + attacco/rilascio
    r.hasDynamics = false;
    r.curveGainDb.fill (0.0f);

    if (s.dynamicsTest && L.stepLength > 0)
    {
        const float* r0 = rec.getReadPointer (0);
        const int dl = juce::jmax (0, peakIdx - (Ns - 1));

        std::array<float, ScribaConst::numSteps> g {};
        for (int i = 0; i < ScribaConst::numSteps; ++i)
        {
            const float amp = juce::Decibels::decibelsToGain (stepLevelDb (i));
            const int st = L.stepsStart + i * L.stepLength + dl;
            const float out = rmsRange (r0, st + (int) (0.55 * L.stepLength), (int) (0.4 * L.stepLength), total);
            g[(size_t) i] = toneGainDb (out, amp);
        }

        const float ref = interpolateCurve (g, s.testLevelDb);
        for (int i = 0; i < ScribaConst::numSteps; ++i)
            r.curveGainDb[(size_t) i] = juce::jlimit (-40.0f, 40.0f, g[(size_t) i] - ref);

        const float quietAmp = juce::Decibels::decibelsToGain (-30.0f);
        const float loudAmp  = juce::Decibels::decibelsToGain (-6.0f);
        const int w   = juce::jmax (4, (int) (0.001 * sr));
        const int hop = juce::jmax (1, (int) (0.00025 * sr));

        auto gainAt = [&] (int p, float amp) { return toneGainDb (rmsRange (r0, p, w, total), amp); };

        const int loudStart  = L.arStart + L.arQuiet1 + dl;
        const int quietStart = loudStart + L.arLoud;
        const int win300 = (int) (0.3 * sr);
        const int win200 = (int) (0.2 * sr);

        const float gQ  = toneGainDb (rmsRange (r0, loudStart - win300, win300, total), quietAmp);
        const float gF  = toneGainDb (rmsRange (r0, quietStart - win200, win200, total), loudAmp);
        const float gR0 = gainAt (quietStart + (int) (0.002 * sr), quietAmp);
        const float gR1 = toneGainDb (rmsRange (r0, quietStart + L.arQuiet2 - win300, win300, total), quietAmp);

        // compressore = riduce il guadagno sui forti E ha un rilascio misurabile
        if (gQ - gF > 1.0f && gR1 - gR0 > 0.5f)
        {
            r.hasDynamics = true;

            const float tA = gQ - 0.632f * (gQ - gF);
            int found = -1;
            for (int p = loudStart; p < quietStart - w; p += hop)
                if (gainAt (p, loudAmp) <= tA) { found = p - loudStart + w / 2; break; }

            r.attackMs = juce::jlimit (0.05f, 500.0f, found > 0 ? (float) (1000.0 * found / sr) : 0.05f);

            const float tR = gR0 + 0.632f * (gR1 - gR0);
            found = -1;
            for (int p = quietStart; p < quietStart + L.arQuiet2 - w; p += hop)
                if (gainAt (p, quietAmp) >= tR) { found = p - quietStart + w / 2; break; }

            r.releaseMs = juce::jlimit (5.0f, 5000.0f, found > 0 ? (float) (1000.0 * found / sr) : 2000.0f);
        }
    }

    return true;
}

void computeDisplay (CaptureResult& r)
{
    r.responseDb.assign ((size_t) ScribaConst::displayPoints, 0.0f);
    r.gain1kDb = 0.0f;

    const int len = r.ir.getNumSamples();
    if (len == 0 || r.ir.getNumChannels() == 0)
        return;

    const size_t N = 65536;
    std::vector<cfloat> a (N, cfloat {});
    const float* d = r.ir.getReadPointer (0);
    for (int i = 0; i < juce::jmin ((int) N, len); ++i)
        a[(size_t) i] = cfloat (d[i], 0.0f);

    fft (a, false);

    const double sr = r.sampleRate;
    const int maxBin = (int) N / 2 - 1;

    auto bandDb = [&] (double f) -> float
    {
        const double fLo = f * std::pow (2.0, -1.0 / 12.0);
        const double fHi = f * std::pow (2.0,  1.0 / 12.0);
        const int lo = juce::jlimit (1, maxBin, (int) std::floor (fLo * (double) N / sr));
        const int hi = juce::jlimit (lo, maxBin, (int) std::ceil (fHi * (double) N / sr));

        double p = 0.0;
        for (int b = lo; b <= hi; ++b)
            p += (double) std::norm (a[(size_t) b]);
        p /= (double) (hi - lo + 1);

        return (float) (10.0 * std::log10 (juce::jmax (1.0e-12, p)));
    };

    const double fMax = juce::jmin (20000.0, sr * 0.5);
    for (int i = 0; i < ScribaConst::displayPoints; ++i)
    {
        const double f = 20.0 * std::pow (1000.0, (double) i / (double) (ScribaConst::displayPoints - 1));
        r.responseDb[(size_t) i] = f < fMax ? bandDb (f) : -120.0f;
    }

    r.gain1kDb = bandDb (1000.0);
}

} // namespace CaptureEngine
