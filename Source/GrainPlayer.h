#pragma once

#include <juce_core/juce_core.h>
#include "Corpus.h"
#include "Spatial.h"
#include <functional>

// Concatenative playback of the original sounds: overlapping grains (Hann
// windows, 50 % overlap) cut from the moments nearest to the cursor.
//  - Focus (sigma): how far around the cursor grains may be picked.
//  - Flow: chance that the next grain continues where the last one was in its
//    file (the sound keeps its own motion) instead of jumping to a new pick.
class GrainPlayer
{
public:
    struct Params
    {
        float x = 0.5f, y = 0.5f;       // map position
        float sigma = 0.02f;            // Focus radius on the map
        float flow = 0.0f, flowSpeed = 1.0f;
        float grainMs = 120.0f;
        float leash = 0.05f;            // how far Flow may wander before jumping back
        // Space: each grain gets its own place when it starts.
        Spatial::Format format;
        const Spatial::Placer* placer = nullptr;   // null = no placement (centre)
        std::function<void (const Spatial::Place&, int source)> onGrain;   // for the display
    };

    void reset();
    // Adds n samples (host rate) into outs[0 .. numChannels-1].
    void render (float* const* outs, int numChannels, int n, double hostRate, const Corpus& corpus, const Params& p, juce::Random& rnd);

private:
    struct Grain
    {
        std::shared_ptr<const SourceAudio> audio;
        double pos = 0.0, step = 1.0;
        int age = 0, length = 1;
        float gl[Spatial::maxChannels], gr[Spatial::maxChannels];
    };
    std::vector<Grain> grains;
    int untilNext = 0;
    int lastUnit = -1;
    double lastTime = 0.0;
    const Corpus* lastCorpus = nullptr;

    int pickUnit (const Corpus&, float x, float y, float sigma, juce::Random&);
    void startGrain (const Corpus&, const Params&, double hostRate, int hop, juce::Random&);
};
