#include "Spatial.h"

#include <juce_core/juce_core.h>
#include <cmath>
#include <algorithm>

namespace Spatial
{
    const char* const outputNames[] = {
        "Stereo", "4 speakers", "5 speakers", "6 speakers", "7 speakers", "8 speakers", "10 speakers",
        "12 speakers", "16 speakers", "Ambisonics 1st order", "Ambisonics 2nd order", "Ambisonics 3rd order",
        "5.1", "7.1", "5.1.2", "5.1.4", "7.1.2", "7.1.4", "9.1.6"
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

    //==========================================================================
    // Surround and Atmos beds, SMPTE / Dolby channel order (side surrounds before rear ones).
    static std::vector<SpeakerLayout::Speaker> presetSpeakers (int preset)
    {
        using S = SpeakerLayout::Speaker;
        std::vector<S> v { { "L", -30, 0, false }, { "R", 30, 0, false }, { "C", 0, 0, false }, { "LFE", 0, 0, true } };
        const bool sevenOne = preset == 1 || preset == 4 || preset == 5 || preset == 6;
        if (sevenOne)
        {
            v.push_back ({ "Ls", -90, 0, false });   v.push_back ({ "Rs", 90, 0, false });
            v.push_back ({ "Lrs", -150, 0, false }); v.push_back ({ "Rrs", 150, 0, false });
        }
        else
        {
            v.push_back ({ "Ls", -110, 0, false });  v.push_back ({ "Rs", 110, 0, false });
        }
        if (preset == 6)
        {
            v.push_back ({ "Lw", -60, 0, false });   v.push_back ({ "Rw", 60, 0, false });
        }
        const bool two = preset == 2 || preset == 4, four = preset == 3 || preset == 5;
        if (two)
        {
            v.push_back ({ "Ltm", -90, 45, false }); v.push_back ({ "Rtm", 90, 45, false });
        }
        if (four || preset == 6)
        {
            v.push_back ({ "Ltf", -45, 45, false }); v.push_back ({ "Rtf", 45, 45, false });
            if (preset == 6) { v.push_back ({ "Ltm", -90, 45, false }); v.push_back ({ "Rtm", 90, 45, false }); }
            v.push_back ({ "Ltr", -135, 45, false }); v.push_back ({ "Rtr", 135, 45, false });
        }
        return v;
    }

    struct V3 { float x, y, z; };
    static V3 dirVec (float az, float el)
    {
        return { std::cos (rad (el)) * std::sin (rad (az)), std::cos (rad (el)) * std::cos (rad (az)), std::sin (rad (el)) };
    }

    static bool invert3 (const V3& a, const V3& b, const V3& c, float* inv)
    {
        // rows a, b, c; inverse in row-major order
        const float m[9] = { a.x, a.y, a.z, b.x, b.y, b.z, c.x, c.y, c.z };
        const float det = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
        if (std::abs (det) < 1e-5f) return false;
        const float d = 1.0f / det;
        inv[0] = (m[4] * m[8] - m[5] * m[7]) * d; inv[1] = (m[2] * m[7] - m[1] * m[8]) * d; inv[2] = (m[1] * m[5] - m[2] * m[4]) * d;
        inv[3] = (m[5] * m[6] - m[3] * m[8]) * d; inv[4] = (m[0] * m[8] - m[2] * m[6]) * d; inv[5] = (m[2] * m[3] - m[0] * m[5]) * d;
        inv[6] = (m[3] * m[7] - m[4] * m[6]) * d; inv[7] = (m[1] * m[6] - m[0] * m[7]) * d; inv[8] = (m[0] * m[4] - m[1] * m[3]) * d;
        return true;
    }

    static SpeakerLayout buildLayout (int preset, bool useCentre)
    {
        SpeakerLayout L;
        L.speakers = presetSpeakers (preset);
        std::vector<int> used;                       // speakers that take part in panning
        for (int i = 0; i < (int) L.speakers.size(); ++i)
        {
            const auto& s = L.speakers[(size_t) i];
            if (s.lfe) { L.lfeChannel = i; continue; }
            if (std::string (s.name) == "C") { L.centreChannel = i; L.centreUsed = useCentre; if (! useCentre) continue; }
            if (s.el != 0.0f) L.hasHeight = true;
            used.push_back (i);
        }
        L.panCount = (int) used.size();
        if (! L.hasHeight)
        {
            // flat: neighbouring pairs around the circle
            std::sort (used.begin(), used.end(), [&] (int a, int b) { return L.speakers[(size_t) a].az < L.speakers[(size_t) b].az; });
            for (size_t k = 0; k < used.size(); ++k)
            {
                const int a = used[k], b = used[(k + 1) % used.size()];
                const V3 va = dirVec (L.speakers[(size_t) a].az, 0), vb = dirVec (L.speakers[(size_t) b].az, 0);
                const float det = va.x * vb.y - va.y * vb.x;
                if (std::abs (det) < 1e-5f) continue;
                SpeakerLayout::Group g { { a, b, -1 }, 2, {} };
                g.inv[0] = vb.y / det;  g.inv[1] = -va.y / det;     // row-major 2x2 inverse
                g.inv[2] = -vb.x / det; g.inv[3] = va.x / det;
                L.groups.push_back (g);
            }
            return L;
        }

        // 3D: faces of the convex hull of the speakers plus a virtual speaker right below
        std::vector<V3> pts;
        std::vector<int> idx;
        for (int i : used) { pts.push_back (dirVec (L.speakers[(size_t) i].az, L.speakers[(size_t) i].el)); idx.push_back (i); }
        for (int i : used) if (L.speakers[(size_t) i].el == 0.0f) L.earLevel.push_back (i);
        L.nadir = (int) L.speakers.size();          // not a real channel
        pts.push_back ({ 0.0f, 0.0f, -1.0f });
        idx.push_back (L.nadir);
        const int n = (int) pts.size();
        for (int a = 0; a < n; ++a)
            for (int b = a + 1; b < n; ++b)
                for (int c = b + 1; c < n; ++c)
                {
                    const V3 u { pts[(size_t) b].x - pts[(size_t) a].x, pts[(size_t) b].y - pts[(size_t) a].y, pts[(size_t) b].z - pts[(size_t) a].z };
                    const V3 w { pts[(size_t) c].x - pts[(size_t) a].x, pts[(size_t) c].y - pts[(size_t) a].y, pts[(size_t) c].z - pts[(size_t) a].z };
                    const V3 nn { u.y * w.z - u.z * w.y, u.z * w.x - u.x * w.z, u.x * w.y - u.y * w.x };
                    bool above = false, below = false;
                    for (int m = 0; m < n && ! (above && below); ++m)
                    {
                        if (m == a || m == b || m == c) continue;
                        const float s = nn.x * (pts[(size_t) m].x - pts[(size_t) a].x) + nn.y * (pts[(size_t) m].y - pts[(size_t) a].y)
                                        + nn.z * (pts[(size_t) m].z - pts[(size_t) a].z);
                        if (s > 1e-4f) above = true;
                        if (s < -1e-4f) below = true;
                    }
                    if (above && below) continue;
                    SpeakerLayout::Group g { { idx[(size_t) a], idx[(size_t) b], idx[(size_t) c] }, 3, {} };
                    if (invert3 (pts[(size_t) a], pts[(size_t) b], pts[(size_t) c], g.inv))
                        L.groups.push_back (g);
                }
        return L;
    }

    static const SpeakerLayout& layoutFor (int preset, bool useCentre)
    {
        static const auto all = []
        {
            std::vector<SpeakerLayout> v;
            for (int p = 0; p < numOutputChoices - firstSurroundChoice; ++p)
                for (int c = 0; c < 2; ++c)
                    v.push_back (buildLayout (p, c == 1));
            return v;
        }();
        return all[(size_t) (preset * 2 + (useCentre ? 1 : 0))];
    }

    std::string SpeakerLayout::channelOrder() const
    {
        std::string s;
        for (auto& sp : speakers) { if (! s.empty()) s += " "; s += sp.name; }
        return s;
    }

    // VBAP: power-normalised gains for a direction, per real channel.
    static void vbap (const SpeakerLayout& L, float az, float el, float* out)
    {
        const V3 p = dirVec (az, L.hasHeight ? el : 0.0f);
        float best[3] = {}; int bestIdx[3] = { -1, -1, -1 }; float bestMin = -1e9f; int bestN = 0;
        for (auto& g : L.groups)
        {
            float v[3];
            if (g.n == 2)
            {
                v[0] = p.x * g.inv[0] + p.y * g.inv[2];
                v[1] = p.x * g.inv[1] + p.y * g.inv[3];
                v[2] = 0.0f;
            }
            else
                for (int j = 0; j < 3; ++j)
                    v[j] = p.x * g.inv[j] + p.y * g.inv[3 + j] + p.z * g.inv[6 + j];
            float mn = juce::jmin (v[0], v[1]);
            if (g.n == 3) mn = juce::jmin (mn, v[2]);
            if (mn > bestMin)
            {
                bestMin = mn; bestN = g.n;
                for (int j = 0; j < 3; ++j) { best[j] = juce::jmax (0.0f, v[j]); bestIdx[j] = g.idx[j]; }
                if (mn >= -1e-4f) break;
            }
        }
        float power = 0.0f;
        for (int j = 0; j < bestN; ++j) power += best[j] * best[j];
        const float norm = power > 1e-12f ? 1.0f / std::sqrt (power) : 0.0f;
        for (int j = 0; j < bestN; ++j)
        {
            const int i = bestIdx[j];
            const float gain = best[j] * norm;
            if (i == L.nadir)
            {
                // nothing below: the ear-level speakers share it
                const float share = gain * gain / (float) juce::jmax<size_t> (1, L.earLevel.size());
                for (int e : L.earLevel) out[e] = std::sqrt (out[e] * out[e] + share);
            }
            else if (i >= 0 && i < maxChannels)
                out[i] = std::sqrt (out[i] * out[i] + gain * gain);
        }
    }

    Format formatFor (int choice, bool frontCentre, bool useCentre, bool lfe)
    {
        Format f;
        choice = juce::jlimit (0, numOutputChoices - 1, choice);
        f.frontCentre = frontCentre;
        if (choice == 0) { f.kind = Kind::stereo; f.channels = 2; }
        else if (choice < 9) { f.kind = Kind::ring; f.channels = ringSizes[choice]; }
        else if (choice < firstSurroundChoice) { f.kind = Kind::ambisonics; f.order = choice - 8; f.channels = (f.order + 1) * (f.order + 1); }
        else
        {
            f.kind = Kind::surround;
            f.layout = &layoutFor (choice - firstSurroundChoice, useCentre);
            f.channels = (int) f.layout->speakers.size();
            f.lfe = lfe;
        }
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
        else if (f.kind == Kind::surround && f.layout != nullptr)
        {
            const auto& L = *f.layout;
            const bool high = L.hasHeight;
            const float d = juce::jlimit (0.0f, 1.0f, high ? p.ambiDist : dist);
            float pw[maxChannels] = {};
            vbap (L, p.az, p.el, pw);
            for (int c = 0; c < f.channels; ++c)
            {
                if (c == L.lfeChannel || (c == L.centreChannel && ! L.centreUsed)) continue;
                g[c] = std::sqrt (2.0f) * std::sqrt (d * pw[c] * pw[c] + (1.0f - d) / (float) juce::jmax (1, L.panCount));
            }
            if (f.lfe && L.lfeChannel >= 0)
                g[L.lfeChannel] = 1.0f;    // the whole mix; the engine keeps only the lows
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
