#pragma once
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>
#include "Biquad.h"

// Result of an "AI" scan: a full band layout ready to be written into the plugin parameters.
struct AutoEqBand
{
    bool on = false;
    int type = Bell;
    float freq = 1000.f, gain = 0.f, q = 1.f;
    bool dyn = false;
    float thr = -30.f, range = -6.f, att = 10.f, rel = 100.f;
};

struct AutoEqResult
{
    bool valid = false;
    std::array<AutoEqBand, 8> bands;
    juce::String summary;
};

// Listens to a vocal, builds a long-term spectrum, compares it with a reference vocal curve
// and turns the differences into EQ moves. Band layout:
//   1: low cut | 2-6: resonance/tone bells | 7: dynamic de-esser | 8: air shelf
class AutoEqAnalyzer
{
public:
    static constexpr int kFftOrder = 13;
    static constexpr int kFftSize = 1 << kFftOrder;   // 8192 samples per analysis frame

    AutoEqAnalyzer()
        : fft (kFftOrder), window (kFftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        data.assign ((size_t) kFftSize * 2, 0.f);
        reset();
    }

    void reset() { power.assign ((size_t) kFftSize / 2 + 1, 0.0); frames = 0; }
    int numFrames() const { return frames; }

    // x = kFftSize most recent (pre-EQ) samples. Returns false if frame was too quiet (ignored).
    bool addFrame (const float* x)
    {
        double ms = 0.0;
        for (int i = 0; i < kFftSize; ++i) ms += (double) x[i] * x[i];
        ms /= kFftSize;
        if (10.0 * std::log10 (ms + 1e-12) < -45.0) return false;   // silence gate

        std::copy (x, x + kFftSize, data.begin());
        std::fill (data.begin() + kFftSize, data.end(), 0.f);
        window.multiplyWithWindowingTable (data.data(), kFftSize);
        fft.performFrequencyOnlyForwardTransform (data.data());

        const double norm = kFftSize / 4.0;
        for (size_t k = 0; k < power.size(); ++k)
        {
            const double m = data[k] / norm;
            power[k] += m * m;
        }
        ++frames;
        return true;
    }

    AutoEqResult compute (double fs, float intensity) const
    {
        using juce::jlimit;
        AutoEqResult res;
        if (frames < 30)
        {
            res.summary = "Not enough signal heard - play more of the vocal and try again.";
            return res;
        }
        intensity = jlimit (0.f, 1.f, intensity);

        constexpr int N = 50;   // 1/6-octave analysis points, ~50 Hz .. 14 kHz
        std::array<double, N> fc {}, lvl {}, tgt {}, err {}, corr {};

        // Reference vocal curve: 1/3-octave band level in dB relative to 1 kHz.
        static const double tf[] = { 50, 63, 100, 160, 250, 400, 630, 1000, 1600, 2500, 4000, 6300, 10000, 16000, 20000 };
        static const double td[] = { -20, -14, -6, -2, 2, 3, 1.5, 0, -2, -4, -7, -10, -15, -22, -26 };
        constexpr int nT = (int) (sizeof (tf) / sizeof (tf[0]));
        auto target = [&] (double f)
        {
            if (f <= tf[0]) return td[0];
            for (int i = 1; i < nT; ++i)
                if (f <= tf[i])
                {
                    const double t = std::log (f / tf[i - 1]) / std::log (tf[i] / tf[i - 1]);
                    return td[i - 1] + t * (td[i] - td[i - 1]);
                }
            return td[nT - 1];
        };

        const double binHz = fs / kFftSize;
        const int maxBin = kFftSize / 2;
        for (int i = 0; i < N; ++i)
        {
            fc[(size_t) i] = 50.0 * std::pow (2.0, i / 6.0);
            const double lo = fc[(size_t) i] * std::pow (2.0, -1.0 / 6.0);
            const double hi = fc[(size_t) i] * std::pow (2.0, 1.0 / 6.0);
            int k0 = (int) std::ceil (lo / binHz), k1 = juce::jmin (maxBin, (int) std::floor (hi / binHz));
            if (k1 < k0) k0 = k1 = juce::jmin (maxBin, (int) std::round (fc[(size_t) i] / binHz));
            double e = 0.0;
            for (int k = k0; k <= k1; ++k) e += power[(size_t) k];
            e /= frames;
            lvl[(size_t) i] = 10.0 * std::log10 (e + 1e-12);
            tgt[(size_t) i] = target (fc[(size_t) i]);
        }

        // Normalise overall level using the 250 Hz - 4 kHz region, then measure deviation from target.
        double ref = 0, tref = 0; int cnt = 0;
        for (int i = 0; i < N; ++i)
            if (fc[(size_t) i] >= 250 && fc[(size_t) i] <= 4000) { ref += lvl[(size_t) i]; tref += tgt[(size_t) i]; ++cnt; }
        ref /= cnt; tref /= cnt;

        for (int i = 0; i < N; ++i)
        {
            err[(size_t) i] = (lvl[(size_t) i] - ref) - (tgt[(size_t) i] - tref);
            double c = -err[(size_t) i] * intensity;
            if (c > 0) c *= 0.5;                       // favour cutting over boosting
            corr[(size_t) i] = jlimit (-9.0, 5.0, c);
        }

        juce::StringArray notes;
        auto fmtHz = [] (double f) { return f >= 1000 ? juce::String (f / 1000.0, 1) + " kHz" : juce::String ((int) std::round (f)) + " Hz"; };

        // ---- band 1: low cut just under the lowest significant content
        {
            double mx = -1e9;
            for (int i = 0; i < N; ++i) if (fc[(size_t) i] >= 100 && fc[(size_t) i] <= 2000) mx = juce::jmax (mx, lvl[(size_t) i]);
            double fLow = 100.0;
            for (int i = 0; i < N; ++i) if (lvl[(size_t) i] > mx - 20.0) { fLow = fc[(size_t) i]; break; }
            auto& b = res.bands[0];
            b.on = true; b.type = LowCut; b.freq = (float) jlimit (70.0, 130.0, fLow * 0.8); b.q = 0.71f;
            notes.add ("Low cut " + fmtHz (b.freq));
        }

        // ---- bands 2-6: biggest deviations (resonances / mud / harshness / dullness)
        {
            std::vector<int> cand;
            for (int i = 1; i < N - 1; ++i)
            {
                if (fc[(size_t) i] < 100 || fc[(size_t) i] > 12000) continue;
                const double a = std::abs (corr[(size_t) i]);
                if (a >= 1.5 && a >= std::abs (corr[(size_t) i - 1]) && a >= std::abs (corr[(size_t) i + 1])) cand.push_back (i);
            }
            std::sort (cand.begin(), cand.end(), [&] (int a, int b) { return std::abs (corr[(size_t) a]) > std::abs (corr[(size_t) b]); });
            std::vector<int> picked;
            for (int i : cand)
            {
                bool ok = true;
                for (int j : picked) if (std::abs (i - j) < 3) ok = false;
                if (ok) { picked.push_back (i); if (picked.size() >= 5) break; }
            }
            std::sort (picked.begin(), picked.end());

            int slot = 1;
            for (int i : picked)
            {
                const double s = corr[(size_t) i] > 0 ? 1.0 : -1.0, half = std::abs (corr[(size_t) i]) * 0.5;
                int L = i, R = i;
                while (L > 0 && corr[(size_t) L - 1] * s > half) --L;
                while (R < N - 1 && corr[(size_t) R + 1] * s > half) ++R;
                const double bw = juce::jmax (1.0 / 3.0, (R - L + 1) / 6.0);
                auto& b = res.bands[(size_t) slot++];
                b.on = true; b.type = Bell; b.freq = (float) fc[(size_t) i]; b.gain = (float) corr[(size_t) i];
                b.q = (float) jlimit (0.7, 8.0, 1.0 / (2.0 * std::sinh (std::log (2.0) / 2.0 * bw)));
                notes.add (juce::String (b.gain, 1) + " dB @ " + fmtHz (b.freq));
            }
        }

        // ---- band 7: dynamic de-esser on the strongest sibilant excess
        {
            int best = -1; double bestErr = 2.5;
            for (int i = 0; i < N; ++i)
                if (fc[(size_t) i] >= 4500 && fc[(size_t) i] <= 10000 && err[(size_t) i] > bestErr) { bestErr = err[(size_t) i]; best = i; }
            if (best >= 0)
            {
                auto& b = res.bands[6];
                b.on = true; b.type = Bell; b.freq = (float) fc[(size_t) best]; b.gain = 0.f; b.q = 2.5f;
                b.dyn = true; b.range = (float) (-jlimit (3.0, 8.0, bestErr) * (0.4 + 0.6 * intensity));
                b.thr = (float) jlimit (-60.0, -6.0, lvl[(size_t) best] - 4.0);
                b.att = 1.f; b.rel = 60.f;
                notes.add ("De-ess @ " + fmtHz (b.freq));
            }
        }

        // ---- band 8: air shelf (or HF tamer) from the average 9-16 kHz deviation
        {
            double m = 0; int c = 0;
            for (int i = 0; i < N; ++i) if (fc[(size_t) i] >= 9000) { m += corr[(size_t) i]; ++c; }
            if (c > 0) m /= c;
            if (std::abs (m) >= 1.5)
            {
                auto& b = res.bands[7];
                b.on = true; b.type = HighShelf; b.freq = 10000.f; b.gain = (float) jlimit (-4.0, 4.0, m); b.q = 0.71f;
                notes.add ((b.gain > 0 ? "Air +" : "HF ") + juce::String (b.gain, 1) + " dB");
            }
        }

        res.valid = true;
        res.summary = notes.joinIntoString (" | ");
        return res;
    }

private:
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> data;
    std::vector<double> power;
    int frames = 0;
};
