#pragma once

#include <string>
#include <vector>
#include <memory>

// Thin wrapper around a RAVE model exported for nn~ (a TorchScript .ts file).
// One instance holds its own streaming state (cached convolutions), so the
// engine keeps one instance for encoding and another for real-time decoding.
class RaveModel
{
public:
    RaveModel();
    ~RaveModel();

    // Throws std::runtime_error with a readable message on failure.
    void load (const std::string& path);

    bool isLoaded() const noexcept { return loaded; }
    int  getLatentSize() const noexcept { return latentSize; }
    int  getRatio() const noexcept { return ratio; }            // samples per latent frame
    int  getInputChannels() const noexcept { return inChannels; }
    int  getOutputChannels() const noexcept { return outChannels; }
    int  getSampleRate() const noexcept { return sampleRate; }   // model rate (may be a guess)
    bool sampleRateIsKnown() const noexcept { return srKnown; }
    const std::string& getPath() const noexcept { return path; }

    // audio: one vector per input channel, same length, at the model rate.
    // Length must be a multiple of getRatio(). Returns frames * latentSize
    // values, frame-major (frame 0 dims..., frame 1 dims...).
    std::vector<float> encode (const std::vector<std::vector<float>>& audio);

    // z: nFrames * latentSize values, frame-major. Returns one vector per
    // output channel, each nFrames * ratio samples long.
    std::vector<std::vector<float>> decode (const float* z, int nFrames);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    bool loaded = false;
    int latentSize = 0, ratio = 0, inChannels = 1, outChannels = 1;
    int sampleRate = 44100;
    bool srKnown = false;
    std::string path;
};
