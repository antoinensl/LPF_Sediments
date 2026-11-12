#include "Spatial.h"

#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

namespace Spatial
{
    const char* const outputNames[] = {
        "Stereo", "4 speakers", "5 speakers", "6 speakers", "7 speakers", "8 speakers", "10 speakers",
        "12 speakers", "16 speakers", "Ambisonics 1st order", "Ambisonics 2nd order", "Ambisonics 3rd order"
    };

    static constexpr int ringSizes[] = { 2, 4, 5, 6, 7, 8, 10, 12, 16 };
    static constexpr float pi = 3.14159265358979f;
    static float rad (float deg) { return deg * pi / 180.0f; }

    static float wrap (float deg)
    {
        deg = std::fmod (deg + 180.0f, 360.0f);
        if (deg < 0.0f) deg += 360.0f;
        return deg - 180.0f;
    }

    Format formatFor (int choice, bool frontCentre)
    {
        Format f;
        choice = juce::jlimit (0, numOutputChoices - 1, choice);
        f.frontCentre = frontCentre;
        if (choice == 0) { f.kind = Kind::stereo; f.channels = 2; }
        else if (choice < 9) { f.kind = Kind::ring; f.channels = ringSizes[choice]; }
        else { f.kind = Kind::ambisonics; f.order = choice - 8; f.channels = (f.order + 1) * (f.order + 1); }
        return f;
    }

    int channelsFor (int choice) { return formatFor (choice, false).channels; }

    float Format::speakerAngle (int k) const noexcept
    {
        const float step = 360.0f / (float) channels;
        return wrap ((frontCentre ? 0.0f : -0.5f * step) + (float) k * step);
    }

    // Real spherical harmonics, ACN order, SN3D, up to 3rd order.
    static void encodeSH (float az, float el, float dist, int order, float* out)
    {
        const float a = rad (-az);          // Ambisonics turns counter-clockwise
        const float e = rad (el);
        const float x = std::cos (e) * std::cos (a), y = std::cos (e) * std::sin (a), z = std::sin (e);
        const float s3 = std::sqrt (3.0f);
        out[0] = 1.0f;
        if (order >= 1)
        {
            out[1] = y * dist; out[2] = z * dist; out[3] = x * dist;
        }
        if (order >= 2)
        {
            out[4] = s3 * x * y * dist;
            out[5] = s3 * y * z * dist;
            out[6] = 0.5f * (3.0f * z * z - 1.0f) * dist;
            out[7] = s3 * x * z * dist;
            out[8] = 0.5f * s3 * (x * x - y * y) * dist;
        }
        if (order >= 3)
        {
            const float c58 = std::sqrt (5.0f / 8.0f), c38 = std::sqrt (3.0f / 8.0f), c15 = std::sqrt (15.0f);
            out[9]  = c58 * y * (3.0f * x * x - y * y) * dist;
            out[10] = c15 * x * y * z * dist;
            out[11] = c38 * y * (5.0f * z * z - 1.0f) * dist;
            out[12] = 0.5f * z * (5.0f * z * z - 3.0f) * dist;
            out[13] = c38 * x * (5.0f * z * z - 1.0f) * dist;
            out[14] = 0.5f * c15 * z * (x * x - y * y) * dist;
            out[15] = c58 * x * (x * x - 3.0f * y * y) * dist;
        }
    }

    void gains (const Format& f, const Place& p, bool inputStereo, float* gl, float* gr)
    {
        for (int c = 0; c < maxChannels; ++c)
            gl[c] = gr[c] = 0.0f;
        const float dist = juce::jlimit (0.0f, 1.0f, p.dist);

        if (f.kind == Kind::stereo)
        {
            // Left/right only: the front/back part of the direction is folded.
            const float pan = std::sin (rad (p.az)) * dist;
            auto put = [] (float q, float& toL, float& toR, float scale)
            {
                const float t = (juce::jlimit (-1.0f, 1.0f, q) + 1.0f) * pi * 0.25f;
                toL = std::cos (t) * scale;
                toR = std::sin (t) * scale;
            };
            if (inputStereo)
            {
                // Shift the stereo image: in the middle it is left untouched.
                put (pan - 1.0f, gl[0], gl[1], 1.0f);
                put (pan + 1.0f, gr[0], gr[1], 1.0f);
            }
            else
                put (pan, gl[0], gl[1], std::sqrt (2.0f));
            return;
        }

        float g[maxChannels] = {};
        if (f.kind == Kind::ring)
        {
            const int n = f.channels;
            const float step = 360.0f / (float) n;
            // position in "speakers" from speaker 0, clockwise
            float u = wrap (p.az - f.speakerAngle (0));
            if (u < 0.0f) u += 360.0f;
            u /= step;
            const int k0 = (int) std::floor (u) % n, k1 = (k0 + 1) % n;
            const float frac = u - std::floor (u);
            float pw[maxChannels] = {};
            pw[k0] = std::cos (frac * pi * 0.5f);
            pw[k1] += std::sin (frac * pi * 0.5f);
            for (int c = 0; c < n; ++c)
                g[c] = std::sqrt (2.0f) * std::sqrt (dist * pw[c] * pw[c] + (1.0f - dist) / (float) n);
        }
        else
            encodeSH (p.az, p.el, juce::jlimit (0.0f, 1.0f, p.ambiDist), f.order, g);

        const float fold = inputStereo ? std::sqrt (0.5f) : 1.0f;
        for (int c = 0; c < f.channels; ++c)
        {
            gl[c] = g[c] * fold;
            gr[c] = inputStereo ? g[c] * fold : 0.0f;
        }
    }

    float defaultSoundAz (int index, int count)
    {
        if (count <= 1) return 0.0f;
        return wrap (360.0f * (float) index / (float) count);
    }

    //==========================================================================
    void Placer::prepare (const Corpus& c)
    {
        const int n = c.size();
        if (c.desc.size() != (size_t) n * Corpus::numDesc || n == 0)
            return;
        std::vector<float> clear;
        for (int i = 0; i < n; ++i)
            if (c.desc[(size_t) (i * Corpus::numDesc + 1)] > 0.6f)
                clear.push_back (c.desc[(size_t) (i * Corpus::numDesc)]);
        if (! clear.empty())
        {
            std::sort (clear.begin(), clear.end());
            medianPitch = clear[clear.size() / 2];
            lo[0] = clear[(size_t) ((clear.size() - 1) * 0.02)];
            hi[0] = clear[(size_t) ((clear.size() - 1) * 0.98)];
        }
        std::vector<float> v ((size_t) n);
        for (int k = 2; k < Corpus::numDesc; ++k)
        {
            for (int i = 0; i < n; ++i)
                v[(size_t) i] = c.desc[(size_t) (i * Corpus::numDesc + k)];
            std::sort (v.begin(), v.end());
            lo[(size_t) k] = v[(size_t) ((n - 1) * 0.02)];
            hi[(size_t) k] = v[(size_t) ((n - 1) * 0.98)];
        }
    }

    float Placer::descValue (const Corpus& c, int unit, int which) const
    {
        if (c.desc.size() != (size_t) c.size() * Corpus::numDesc)
            return 0.5f;
        const float* d = c.desc.data() + (size_t) unit * Corpus::numDesc;
        int k = 0;
        float val = 0.0f;
        switch (which)
        {
            case 1:  k = 0; val = d[1] > 0.6f ? d[0] : medianPitch; break;   // pitch
            case 2:  k = 2; val = d[2]; break;                                 // brightness
            case 3:  k = 3; val = d[3]; break;                                 // noisiness
            default: k = 4; val = d[4]; break;                                 // loudness
        }
        const float range = hi[(size_t) k] - lo[(size_t) k];
        if (range <= 1e-6f)
            return 0.5f;
        return juce::jlimit (0.0f, 1.0f, (val - lo[(size_t) k]) / range);
    }

    static void mapDirection (float x, float y, float& az, float& dist)
    {
        const float dx = x - 0.5f, dy = 0.5f - y;     // top of the map = front
        const float len = std::sqrt (dx * dx + dy * dy);
        az = len > 1e-5f ? std::atan2 (dx, dy) * 180.0f / pi : 0.0f;
        dist = juce::jmin (1.0f, len / 0.42f);
    }

    Place Placer::finish (Place p, const Corpus& c, int unit, float mapX, float mapY, juce::Random* rnd) const
    {
        if (by != PlaceBy::eachSound)          // directions set by hand stay where they were put
            p.az *= juce::jlimit (0.0f, 1.0f, width);
        p.ambiDist = p.dist;
        switch (height)
        {
            case Height::flat: p.el = 0.0f; break;
            case Height::dome:
            {
                float a, d;
                mapDirection (mapX, mapY, a, d);
                p.el = (1.0f - d) * 90.0f;      // the middle of the map is right above
                p.ambiDist = 1.0f;
                break;
            }
            default:
                p.el = (descValue (c, unit, (int) height - 1) - 0.5f) * 170.0f;   // the whole sphere
                break;
        }
        if (rnd != nullptr && scatter > 0.0f)
        {
            p.az += scatter * 180.0f * (2.0f * rnd->nextFloat() - 1.0f);
            const float out = scatter * (0.5f + 0.5f * rnd->nextFloat());
            p.dist = juce::jmax (p.dist, out);
            p.ambiDist = juce::jmax (p.ambiDist, out);
            p.el = juce::jlimit (-90.0f, 90.0f, p.el + scatter * 45.0f * (2.0f * rnd->nextFloat() - 1.0f));
        }
        p.az = wrap (p.az + rotation);
        return p;
    }

    Place Placer::placeUnit (const Corpus& c, int unit, juce::Random* rnd) const
    {
        Place p;
        if (unit < 0 || unit >= c.size())
            return p;
        const float mx = c.xy[(size_t) unit * 2], my = c.xy[(size_t) unit * 2 + 1];
        switch (by)
        {
            case PlaceBy::centre: p.az = 0.0f; p.dist = 0.0f; break;
            case PlaceBy::map:    mapDirection (mx, my, p.az, p.dist); break;
            case PlaceBy::eachSound:
            {
                const int s = c.sourceIndex[(size_t) unit];
                p.az = juce::isPositiveAndBelow (s, (int) soundAz.size()) ? soundAz[(size_t) s]
                                                                         : defaultSoundAz (s, (int) c.sources.size());
                p.dist = 1.0f;
                break;
            }
            default:
                p.az = (descValue (c, unit, (int) by - 2) - 0.5f) * 360.0f;
                p.dist = 1.0f;
                break;
        }
        return finish (p, c, unit, mx, my, rnd);
    }

    Place Placer::placeAt (const Corpus& c, float x, float y) const
    {
        int best = -1;
        float bd = 1e9f;
        for (int i = 0; i < c.size(); ++i)
        {
            const float dx = c.xy[(size_t) i * 2] - x, dy = c.xy[(size_t) i * 2 + 1] - y;
            const float d = dx * dx + dy * dy;
            if (d < bd) { bd = d; best = i; }
        }
        if (best < 0)
            return {};
        if (by == PlaceBy::map)
        {
            Place p;
            mapDirection (x, y, p.az, p.dist);
            return finish (p, c, best, x, y, nullptr);
        }
        auto p = placeUnit (c, best, nullptr);
        if (height == Height::dome)
        {
            // dome height follows the cursor itself
            float a, d;
            mapDirection (x, y, a, d);
            p.el = (1.0f - d) * 90.0f;
        }
        return p;
    }
}
