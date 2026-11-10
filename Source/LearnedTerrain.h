#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <cstdint>

#include "Corpus.h"

// The learned terrain, as in nn.terrain: a small neural network that maps a
// position on the map (x, y) to a latent vector. It is trained on the corpus
// points, so it passes close to them, but it is a continuous surface: between
// and around the points it invents latents that belong to no file.
//
// The network is a coordinate MLP with random Fourier features, trained on the
// CPU in the background. Once trained, an instance is immutable except for
// query(), which only the synthesis thread calls.
class LearnedTerrain
{
public:
    ~LearnedTerrain();

    // Trains a terrain on the corpus. `progress` is called with 0..1 and returns
    // false to abort (then train() returns nullptr). `pauseBetweenStepsMs`
    // leaves CPU time to the real-time decoder.
    static std::shared_ptr<LearnedTerrain> train (std::shared_ptr<const Corpus> corpus,
                                                  const std::function<bool (float)>& progress,
                                                  int pauseBetweenStepsMs = 1);

    void query (float x, float y, float* zOut) const;

    const Corpus* trainedOn() const noexcept { return corpusPtr.get(); }
    int getDim() const noexcept { return dim; }

    // A colour picture of the terrain for the UI: gridW x gridH RGB bytes,
    // row-major, top row first. Similar colours = similar sounds.
    int gridW = 0, gridH = 0;
    std::vector<uint8_t> rgb;
    float finalLoss = 0.0f;

private:
    LearnedTerrain();
    struct Impl;
    std::unique_ptr<Impl> impl;
    std::shared_ptr<const Corpus> corpusPtr;
    int dim = 0;
};
