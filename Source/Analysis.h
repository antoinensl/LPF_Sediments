#pragma once

#include <vector>
#include <memory>

namespace juce { namespace dsp { class FFT; } }

// Classic audio descriptors for one short frame, used to place sounds on the
// map without a model (and as optional map axes with one).
namespace Analysis
{
    enum Index { pitch = 0, clarity, brightness, noisiness, loudness, count };

    struct Descriptor
    {
        float pitch = 0.0f;       // MIDI note number (fractional)
        float clarity = 0.0f;     // 0..1, how clearly pitched the frame is
        float brightness = 0.0f;  // log2 of the spectral centroid in Hz
        float noisiness = 0.0f;   // spectral flatness, 0 (tonal) .. 1 (noise)
        float loudness = -120.0f; // RMS in dBFS
    };

    class Analyzer
    {
    public:
        Analyzer();
        ~Analyzer();
        // x: n mono samples (n <= 4096) at sample rate sr.
        Descriptor analyse (const float* x, int n, double sr);

    private:
        static constexpr int order = 13, size = 1 << order;   // 8192-point FFT
        std::unique_ptr<juce::dsp::FFT> fft;
        std::vector<float> buf, window, nsdf, sq;
    };
}
