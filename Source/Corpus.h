#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <memory>

// The original audio of one file, kept for playing grains of it.
struct SourceAudio
{
    std::vector<float> left, right;   // same length; mono files are duplicated
    double rate = 44100.0;
    int length() const noexcept { return (int) left.size(); }
};

// A corpus is the set of latent frames encoded from the files (or live input)
// the user gave us, each placed at a 2D position on the map.
// Snapshots are immutable once published: the engine swaps whole snapshots.
struct Corpus
{
    struct Source
    {
        std::string name;     // file name, or "Live take 3"
        std::string path;     // empty for live takes
        int colour = 0;       // stable colour index, kept when other files are removed
        float durationSec = 0.0f;
    };

    int dim = 0;                       // latent size
    std::vector<float> latents;        // N * dim, frame-major
    std::vector<float> xy;             // N * 2, in [0, 1]
    std::vector<int>   sourceIndex;    // N, index into sources
    std::vector<float> frameTime;      // N, seconds from the start of its file
    std::vector<Source> sources;
    std::vector<std::shared_ptr<const SourceAudio>> audio;   // per source (may be null)

    // Audio descriptors per frame (see Analysis.h): pitch, clarity, brightness,
    // noisiness, loudness.
    static constexpr int numDesc = 5;
    std::vector<float> desc;           // N * numDesc
    float unitSeconds = 0.0f;          // length of one frame, in seconds of its file
    bool modelLatents = false;         // latents come from a model (else = descriptors)

    // Map axes: 0 = similarity (PCA), 1 = pitch, 2 = brightness, 3 = noisiness, 4 = loudness
    int layoutX = 0, layoutY = 0;

    // Zones for MIDI playing: up to 12 groups of nearby frames (k-means on the
    // map), ordered clockwise from the top. Zone i is played by note name i (C = 0).
    std::vector<float> zones;          // Z * 2, centres
    std::vector<float> zoneRadius;     // Z

    int size() const noexcept { return dim > 0 ? (int) (latents.size() / (size_t) dim) : 0; }
    int numZones() const noexcept { return (int) zoneRadius.size(); }

    // Frames of one file are stored in time order, so the "next moment" of frame i
    // is i + 1 when it belongs to the same file. Returns -1 at the end of a file.
    int nextFrame (int i) const noexcept
    {
        return (i + 1 < size() && sourceIndex[(size_t) i + 1] == sourceIndex[(size_t) i]) ? i + 1 : -1;
    }

    // Lays out all frames on the map: a 2D PCA of the latent vectors (nearby
    // points = similar sounds) and/or descriptor axes. Robust min/max scaling to
    // [0.04, 0.96]. Also computes the zones.
    void computeLayout();
    void computeZones (int maxZones = 12);
};

// The terrain: maps a position on the map to a latent vector.
// v1 = Gaussian-kernel interpolation of the corpus frames (instant, no
// training). "blur" widens the kernel: low = snaps to the nearest sounds,
// high = smooth blends between regions.
namespace Terrain
{
    float blurToSigma (float blur01) noexcept;
    void  query (const Corpus& corpus, float x, float y, float sigma, float* zOut);
}
