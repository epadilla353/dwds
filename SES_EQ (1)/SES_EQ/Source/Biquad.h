#pragma once
#include <juce_core/juce_core.h>
#include <cmath>
#include <complex>

enum BandType { Bell = 0, LowShelf, HighShelf, LowCut, HighCut, Notch, BandPass };

inline const juce::StringArray& bandTypeNames()
{
    static const juce::StringArray s { "Bell", "Low Shelf", "High Shelf", "Low Cut", "High Cut", "Notch" };
    return s;
}

inline bool typeUsesGain (int t) { return t == Bell || t == LowShelf || t == HighShelf; }
inline bool typeCanBeDynamic (int t) { return t == Bell || t == LowShelf || t == HighShelf || t == Notch; }

struct BiquadCoeffs { double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0; };

// RBJ audio-EQ-cookbook biquads
inline BiquadCoeffs makeCoeffs (int type, double fs, double f, double gainDb, double q)
{
    const double pi = juce::MathConstants<double>::pi;
    const double w0 = 2.0 * pi * juce::jlimit (10.0, fs * 0.49, f) / fs;
    const double cw = std::cos (w0), sw = std::sin (w0);
    q = juce::jmax (0.05, q);
    const double alpha = sw / (2.0 * q);
    const double A = std::pow (10.0, gainDb / 40.0);
    const double sq = 2.0 * std::sqrt (A) * alpha;
    double b0 = 1, b1 = 0, b2 = 0, a0 = 1, a1 = 0, a2 = 0;

    switch (type)
    {
        case Bell:
            b0 = 1 + alpha * A; b1 = -2 * cw; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cw; a2 = 1 - alpha / A; break;
        case LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cw + sq);
            b1 = 2 * A * ((A - 1) - (A + 1) * cw);
            b2 = A * ((A + 1) - (A - 1) * cw - sq);
            a0 = (A + 1) + (A - 1) * cw + sq;
            a1 = -2 * ((A - 1) + (A + 1) * cw);
            a2 = (A + 1) + (A - 1) * cw - sq; break;
        case HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cw + sq);
            b1 = -2 * A * ((A - 1) + (A + 1) * cw);
            b2 = A * ((A + 1) + (A - 1) * cw - sq);
            a0 = (A + 1) - (A - 1) * cw + sq;
            a1 = 2 * ((A - 1) - (A + 1) * cw);
            a2 = (A + 1) - (A - 1) * cw - sq; break;
        case LowCut: // high-pass
            b0 = (1 + cw) / 2; b1 = -(1 + cw); b2 = (1 + cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case HighCut: // low-pass
            b0 = (1 - cw) / 2; b1 = 1 - cw; b2 = (1 - cw) / 2;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case Notch:
            b0 = 1; b1 = -2 * cw; b2 = 1;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
        case BandPass:
            b0 = alpha; b1 = 0; b2 = -alpha;
            a0 = 1 + alpha; a1 = -2 * cw; a2 = 1 - alpha; break;
    }
    BiquadCoeffs c;
    c.b0 = b0 / a0; c.b1 = b1 / a0; c.b2 = b2 / a0; c.a1 = a1 / a0; c.a2 = a2 / a0;
    return c;
}

inline double magnitudeDb (const BiquadCoeffs& c, double fs, double f)
{
    const double w = 2.0 * juce::MathConstants<double>::pi * f / fs;
    const std::complex<double> z1 = std::polar (1.0, -w), z2 = std::polar (1.0, -2.0 * w);
    const auto num = c.b0 + c.b1 * z1 + c.b2 * z2;
    const auto den = 1.0 + c.a1 * z1 + c.a2 * z2;
    return 20.0 * std::log10 (std::abs (num) / std::abs (den) + 1e-12);
}

struct Biquad
{
    BiquadCoeffs c;
    double z1 = 0, z2 = 0;
    void reset() { z1 = z2 = 0; }
    inline float process (float x)
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        return (float) y;
    }
};
