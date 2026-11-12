#include "GrainPlayer.h"

#include <cmath>

void GrainPlayer::reset()
{
    grains.clear();
    untilNext = 0;
    lastUnit = -1;
    lastCorpus = nullptr;
}

int GrainPlayer::pickUnit (const Corpus& c, float x, float y, float sigma, juce::Random& rnd)
{
    constexpr int K = 12;
    std::pair<float, int> best[K];
    int count = 0;
    const int n = c.size();
    for (int i = 0; i < n; ++i)
    {
        const float dx = c.xy[(size_t) i * 2] - x, dy = c.xy[(size_t) i * 2 + 1] - y;
        const float d = dx * dx + dy * dy;
        if (count < K || d < best[count - 1].first)
        {
            int j = count < K ? count++ : K - 1;
            while (j > 0 && best[j - 1].first > d) { best[j] = best[j - 1]; --j; }
            best[j] = { d, i };
        }
    }
    if (count == 0)
        return -1;
    // weights relative to the nearest, so they never all underflow
    float w[K], total = 0.0f;
    const float inv = 1.0f / (2.0f * sigma * sigma);
    for (int k = 0; k < count; ++k)
    {
        w[k] = std::exp (-(best[k].first - best[0].first) * inv);
        total += w[k];
    }
    float r = rnd.nextFloat() * total;
    for (int k = 0; k < count; ++k)
    {
        r -= w[k];
        if (r <= 0.0f)
            return best[k].second;
    }
    return best[0].second;
}

void GrainPlayer::startGrain (const Corpus& c, const Params& p, double hostRate, int hop, juce::Random& rnd)
{
    if (c.frameTime.size() != (size_t) c.size() || c.audio.size() != c.sources.size())
        return;

    int unit = -1;
    double t = 0.0;

    // Flow: continue in the same file, as long as it stays near the cursor.
    if (lastCorpus == &c && lastUnit >= 0 && lastUnit < c.size() && rnd.nextFloat() < p.flow)
    {
        t = lastTime + (double) hop / hostRate * p.flowSpeed;
        int u = lastUnit;
        for (int nx = c.nextFrame (u); nx >= 0 && c.frameTime[(size_t) nx] <= t; nx = c.nextFrame (u))
            u = nx;
        const bool inside = t <= c.frameTime[(size_t) u] + c.unitSeconds * 1.5f;
        const float dx = c.xy[(size_t) u * 2] - p.x, dy = c.xy[(size_t) u * 2 + 1] - p.y;
        if (inside && dx * dx + dy * dy <= p.leash * p.leash)
            unit = u;
    }
    if (unit < 0)
    {
        unit = pickUnit (c, p.x, p.y, p.sigma, rnd);
        if (unit < 0)
            return;
        t = c.frameTime[(size_t) unit] + rnd.nextFloat() * c.unitSeconds * 0.5f;
    }
    lastCorpus = &c;
    lastUnit = unit;
    lastTime = t;

    auto audio = c.audio[(size_t) c.sourceIndex[(size_t) unit]];
    if (audio == nullptr || audio->length() < 4)
        return;
    Grain g;
    g.audio = std::move (audio);
    g.step = g.audio->rate / hostRate;
    g.pos = t * g.audio->rate;
    g.length = juce::jmax (64, (int) (p.grainMs * 0.001 * hostRate));
    Spatial::Place place;
    if (p.placer != nullptr)
        place = p.placer->placeUnit (c, unit, &rnd);
    Spatial::gains (p.format, place, g.audio->stereo, g.gl, g.gr);
    if (p.onGrain)
        p.onGrain (place, c.sourceIndex[(size_t) unit]);
    grains.push_back (std::move (g));
}

void GrainPlayer::render (float* const* outs, int numChannels, int n, double hostRate, const Corpus& c, const Params& p, juce::Random& rnd)
{
    const int grainLen = juce::jmax (64, (int) (p.grainMs * 0.001 * hostRate));
    const int hop = juce::jmax (32, grainLen / 2);
    const int nc = juce::jmin (numChannels, Spatial::maxChannels);
    int i = 0;
    while (i < n)
    {
        if (untilNext <= 0)
        {
            startGrain (c, p, hostRate, hop, rnd);
            untilNext = hop;
        }
        const int seg = juce::jmin (n - i, untilNext);
        for (auto& g : grains)
        {
            const auto& L = g.audio->left;
            const auto& R = g.audio->right;
            const int len = g.audio->length();
            for (int k = 0; k < seg && g.age < g.length; ++k, ++g.age, g.pos += g.step)
            {
                const int i0 = (int) g.pos;
                if (i0 < 0 || i0 + 1 >= len)
                    continue;
                const float f = (float) (g.pos - i0);
                const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) g.age / (float) g.length);
                const float l = w * (L[(size_t) i0] + f * (L[(size_t) i0 + 1] - L[(size_t) i0]));
                const float r = w * (R[(size_t) i0] + f * (R[(size_t) i0 + 1] - R[(size_t) i0]));
                for (int c = 0; c < nc; ++c)
                    outs[c][i + k] += g.gl[c] * l + g.gr[c] * r;
            }
        }
        grains.erase (std::remove_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.age >= g.length; }), grains.end());
        i += seg;
        untilNext -= seg;
    }
}
