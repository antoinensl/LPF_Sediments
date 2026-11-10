#include <juce_dsp/juce_dsp.h>
#include "Analysis.h"

#include <cmath>
#include <algorithm>

namespace Analysis
{
Analyzer::~Analyzer() = default;

Analyzer::Analyzer() : fft (std::make_unique<juce::dsp::FFT> (order))
{
    buf.resize ((size_t) size * 2);
    nsdf.resize ((size_t) size);
    sq.resize ((size_t) size + 1);
}

Descriptor Analyzer::analyse (const float* x, int n, double sr)
{
    Descriptor d;
    n = std::min (n, size / 2);
    if (n < 64)
        return d;

    // Loudness
    double e = 0.0;
    for (int i = 0; i < n; ++i) e += (double) x[i] * x[i];
    const double rms = std::sqrt (e / n);
    d.loudness = (float) (20.0 * std::log10 (rms + 1e-9));

    // Spectrum of the Hann-windowed frame
    if ((int) window.size() != n)
    {
        window.resize ((size_t) n);
        for (int i = 0; i < n; ++i)
            window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) (n - 1));
    }
    std::fill (buf.begin(), buf.end(), 0.0f);
    for (int i = 0; i < n; ++i) buf[(size_t) i] = x[i] * window[(size_t) i];
    fft->performFrequencyOnlyForwardTransform (buf.data(), true);   // magnitudes in buf[0 .. size/2]

    const int bins = size / 2;
    const double binHz = sr / size;
    double num = 0.0, den = 0.0, logSum = 0.0;
    int counted = 0;
    for (int k = 1; k < bins; ++k)
    {
        const double f = k * binHz;
        if (f < 30.0 || f > 18000.0) continue;
        const double p = (double) buf[(size_t) k] * buf[(size_t) k] + 1e-12;
        num += f * p;
        den += p;
        logSum += std::log (p);
        ++counted;
    }
    if (den > 0.0 && counted > 0)
    {
        d.brightness = (float) std::log2 (std::max (30.0, num / den));
        d.noisiness = (float) juce::jlimit (0.0, 1.0, std::exp (logSum / counted) / (den / counted));
    }

    // Pitch: McLeod pitch method (normalised square difference), autocorrelation via FFT
    std::fill (buf.begin(), buf.end(), 0.0f);
    std::copy (x, x + n, buf.begin());
    fft->performRealOnlyForwardTransform (buf.data(), true);
    for (int k = 0; k <= size / 2; ++k)
    {
        const float re = buf[(size_t) k * 2], im = buf[(size_t) k * 2 + 1];
        buf[(size_t) k * 2] = re * re + im * im;
        buf[(size_t) k * 2 + 1] = 0.0f;
    }
    fft->performRealOnlyInverseTransform (buf.data());   // buf[tau] = autocorrelation r(tau)

    sq[0] = 0.0f;
    for (int i = 0; i < n; ++i) sq[(size_t) i + 1] = sq[(size_t) i] + x[i] * x[i];
    const int minTau = std::max (2, (int) (sr / 2000.0)), maxTau = std::min (n - 2, (int) (sr / 50.0));
    if (maxTau <= minTau || rms < 1e-4)
        return d;
    for (int tau = 0; tau <= maxTau; ++tau)
    {
        const double m = (double) sq[(size_t) (n - tau)] + ((double) sq[(size_t) n] - sq[(size_t) tau]);
        nsdf[(size_t) tau] = m > 1e-12 ? (float) (2.0 * buf[(size_t) tau] / m) : 0.0f;
    }

    // key maxima between positive-going zero crossings
    std::vector<std::pair<int, float>> peaks;
    int tau = 1;
    while (tau < maxTau && nsdf[(size_t) tau] > 0.0f) ++tau;          // skip the lobe around 0
    while (tau < maxTau)
    {
        while (tau < maxTau && nsdf[(size_t) tau] <= 0.0f) ++tau;
        int best = -1; float bv = 0.0f;
        while (tau < maxTau && nsdf[(size_t) tau] > 0.0f)
        {
            if (tau >= minTau && nsdf[(size_t) tau] > bv) { bv = nsdf[(size_t) tau]; best = tau; }
            ++tau;
        }
        if (best > 0) peaks.push_back ({ best, bv });
    }
    if (peaks.empty())
        return d;
    float top = 0.0f;
    for (auto& p : peaks) top = std::max (top, p.second);
    for (auto& p : peaks)
        if (p.second >= 0.88f * top)
        {
            const int t = p.first;
            const float a = nsdf[(size_t) t - 1], b = nsdf[(size_t) t], c = nsdf[(size_t) t + 1];
            const float denom = a - 2.0f * b + c;
            const float shift = std::abs (denom) > 1e-9f ? 0.5f * (a - c) / denom : 0.0f;
            const double f0 = sr / (t + juce::jlimit (-1.0f, 1.0f, shift));
            d.pitch = (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
            d.clarity = juce::jlimit (0.0f, 1.0f, b);
            break;
        }
    return d;
}
}
