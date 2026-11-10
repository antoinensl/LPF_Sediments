#include "Corpus.h"

#include <numeric>
#include <limits>
#include <cstdint>

namespace
{
    // Top eigenvector of a symmetric dim x dim matrix by power iteration.
    std::vector<double> topEigenvector (const std::vector<double>& m, int dim, int seed)
    {
        std::vector<double> v ((size_t) dim), w ((size_t) dim);
        for (int i = 0; i < dim; ++i)
            v[(size_t) i] = 1.0 + 0.1 * ((i * 7 + seed * 13) % 5);

        for (int it = 0; it < 200; ++it)
        {
            double norm = 0.0;
            for (int r = 0; r < dim; ++r)
            {
                double s = 0.0;
                for (int c = 0; c < dim; ++c)
                    s += m[(size_t) (r * dim + c)] * v[(size_t) c];
                w[(size_t) r] = s;
                norm += s * s;
            }
            norm = std::sqrt (norm);
            if (norm < 1e-12)
                break;
            for (int i = 0; i < dim; ++i)
                v[(size_t) i] = w[(size_t) i] / norm;
        }
        return v;
    }

    void robustScale (std::vector<float>& xy, int n, int axis)
    {
        std::vector<float> vals ((size_t) n);
        for (int i = 0; i < n; ++i)
            vals[(size_t) i] = xy[(size_t) (i * 2 + axis)];
        std::sort (vals.begin(), vals.end());
        const float lo = vals[(size_t) ((n - 1) * 0.01)];
        const float hi = vals[(size_t) ((n - 1) * 0.99)];
        const float range = std::max (hi - lo, 1e-6f);
        for (int i = 0; i < n; ++i)
        {
            auto& p = xy[(size_t) (i * 2 + axis)];
            p = 0.04f + 0.92f * std::clamp ((p - lo) / range, 0.0f, 1.0f);
        }
    }
}

void Corpus::computeLayout()
{
    const int n = size();
    xy.assign ((size_t) n * 2, 0.5f);
    if (n < 2 || dim < 1)
        return;

    // Values used for PCA: model latents as they are, descriptors standardised.
    std::vector<float> data;
    const float* src = latents.data();
    int d = dim;
    if (! modelLatents)
    {
        d = dim;
        data = latents;
        for (int k = 0; k < d; ++k)
        {
            double m = 0.0, v = 0.0;
            for (int i = 0; i < n; ++i) m += data[(size_t) (i * d + k)];
            m /= n;
            for (int i = 0; i < n; ++i) { const double a = data[(size_t) (i * d + k)] - m; v += a * a; }
            const double sd = std::sqrt (v / n) + 1e-6;
            for (int i = 0; i < n; ++i) data[(size_t) (i * d + k)] = (float) ((data[(size_t) (i * d + k)] - m) / sd);
        }
        src = data.data();
    }

    std::vector<double> mean ((size_t) d, 0.0);
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < d; ++k)
            mean[(size_t) k] += src[(size_t) (i * d + k)];
    for (auto& m : mean)
        m /= n;

    std::vector<double> cov ((size_t) (d * d), 0.0);
    for (int i = 0; i < n; ++i)
    {
        const float* z = src + (size_t) i * (size_t) d;
        for (int r = 0; r < d; ++r)
        {
            const double a = z[r] - mean[(size_t) r];
            for (int c = r; c < d; ++c)
                cov[(size_t) (r * d + c)] += a * (z[c] - mean[(size_t) c]);
        }
    }
    for (int r = 0; r < d; ++r)
        for (int c = r; c < d; ++c)
            cov[(size_t) (c * d + r)] = cov[(size_t) (r * d + c)] /= n;

    auto e1 = topEigenvector (cov, d, 1);
    double lambda1 = 0.0;
    for (int r = 0; r < d; ++r)
        for (int c = 0; c < d; ++c)
            lambda1 += e1[(size_t) r] * cov[(size_t) (r * d + c)] * e1[(size_t) c];
    auto cov2 = cov;
    for (int r = 0; r < d; ++r)
        for (int c = 0; c < d; ++c)
            cov2[(size_t) (r * d + c)] -= lambda1 * e1[(size_t) r] * e1[(size_t) c];
    auto e2 = topEigenvector (cov2, d, 2);

    // Descriptor value for an axis; unclear pitches go to the median pitch.
    float medianPitch = 60.0f;
    const bool hasDesc = desc.size() == (size_t) n * numDesc;
    if (hasDesc)
    {
        std::vector<float> clear;
        for (int i = 0; i < n; ++i)
            if (desc[(size_t) (i * numDesc + 1)] > 0.6f) clear.push_back (desc[(size_t) (i * numDesc)]);
        if (! clear.empty())
        {
            std::nth_element (clear.begin(), clear.begin() + (long) clear.size() / 2, clear.end());
            medianPitch = clear[clear.size() / 2];
        }
    }
    auto axisValue = [&] (int axis, int i) -> float
    {
        switch (axis)
        {
            case 1: return desc[(size_t) (i * numDesc + 1)] > 0.6f ? desc[(size_t) (i * numDesc)] : medianPitch;
            case 2: return desc[(size_t) (i * numDesc + 2)];
            case 3: return desc[(size_t) (i * numDesc + 3)];
            case 4: return desc[(size_t) (i * numDesc + 4)];
            default: return 0.0f;
        }
    };

    for (int i = 0; i < n; ++i)
    {
        const float* z = src + (size_t) i * (size_t) d;
        double px = 0.0, py = 0.0;
        for (int k = 0; k < d; ++k)
        {
            const double a = z[k] - mean[(size_t) k];
            px += a * e1[(size_t) k];
            py += a * e2[(size_t) k];
        }
        // descriptor axes: low values on the left / at the bottom
        xy[(size_t) (i * 2)]     = (layoutX > 0 && hasDesc) ? axisValue (layoutX, i) : (float) px;
        xy[(size_t) (i * 2 + 1)] = (layoutY > 0 && hasDesc) ? -axisValue (layoutY, i) : (float) py;
    }

    robustScale (xy, n, 0);
    robustScale (xy, n, 1);
    computeZones();
}

void Corpus::computeZones (int maxZones)
{
    zones.clear();
    zoneRadius.clear();
    const int n = size();
    if (n == 0)
        return;
    const int k = std::min (maxZones, n);

    // k-means++ seeding with a fixed generator, so the same files give the same zones.
    std::vector<float> c ((size_t) k * 2);
    uint32_t rng = 12345u;
    auto rand01 = [&rng] { rng = rng * 1664525u + 1013904223u; return (float) (rng >> 8) / 16777216.0f; };
    auto d2 = [this] (int i, float cx, float cy)
    {
        const float dx = xy[(size_t) i * 2] - cx, dy = xy[(size_t) i * 2 + 1] - cy;
        return dx * dx + dy * dy;
    };
    {
        const int first = std::min (n - 1, (int) (rand01() * n));
        c[0] = xy[(size_t) first * 2];
        c[1] = xy[(size_t) first * 2 + 1];
        std::vector<float> best ((size_t) n, std::numeric_limits<float>::max());
        for (int j = 1; j < k; ++j)
        {
            double total = 0.0;
            for (int i = 0; i < n; ++i)
            {
                best[(size_t) i] = std::min (best[(size_t) i], d2 (i, c[(size_t) (j - 1) * 2], c[(size_t) (j - 1) * 2 + 1]));
                total += best[(size_t) i];
            }
            double r = rand01() * total;
            int pick = n - 1;
            for (int i = 0; i < n; ++i)
            {
                r -= best[(size_t) i];
                if (r <= 0.0) { pick = i; break; }
            }
            c[(size_t) j * 2] = xy[(size_t) pick * 2];
            c[(size_t) j * 2 + 1] = xy[(size_t) pick * 2 + 1];
        }
    }

    std::vector<int> label ((size_t) n, 0);
    for (int it = 0; it < 30; ++it)
    {
        for (int i = 0; i < n; ++i)
        {
            float bestD = std::numeric_limits<float>::max();
            for (int j = 0; j < k; ++j)
            {
                const float d = d2 (i, c[(size_t) j * 2], c[(size_t) j * 2 + 1]);
                if (d < bestD) { bestD = d; label[(size_t) i] = j; }
            }
        }
        std::vector<double> sx ((size_t) k, 0.0), sy ((size_t) k, 0.0);
        std::vector<int> cnt ((size_t) k, 0);
        for (int i = 0; i < n; ++i)
        {
            sx[(size_t) label[(size_t) i]] += xy[(size_t) i * 2];
            sy[(size_t) label[(size_t) i]] += xy[(size_t) i * 2 + 1];
            ++cnt[(size_t) label[(size_t) i]];
        }
        for (int j = 0; j < k; ++j)
            if (cnt[(size_t) j] > 0)
            {
                c[(size_t) j * 2] = (float) (sx[(size_t) j] / cnt[(size_t) j]);
                c[(size_t) j * 2 + 1] = (float) (sy[(size_t) j] / cnt[(size_t) j]);
            }
    }

    // Radius = mean distance of the members, kept within a playable range.
    std::vector<double> rs ((size_t) k, 0.0);
    std::vector<int> cnt ((size_t) k, 0);
    for (int i = 0; i < n; ++i)
    {
        const int j = label[(size_t) i];
        rs[(size_t) j] += std::sqrt (d2 (i, c[(size_t) j * 2], c[(size_t) j * 2 + 1]));
        ++cnt[(size_t) j];
    }

    // Order clockwise from the top of the map, like a clock: C at 12 o'clock.
    std::vector<int> order ((size_t) k);
    std::iota (order.begin(), order.end(), 0);
    auto angle = [&c] (int j)
    {
        const float a = std::atan2 (c[(size_t) j * 2] - 0.5f, -(c[(size_t) j * 2 + 1] - 0.5f));   // 0 at top, clockwise
        return a < 0.0f ? a + 6.2831853f : a;
    };
    std::sort (order.begin(), order.end(), [&] (int a, int b) { return angle (a) < angle (b); });

    for (int j : order)
    {
        zones.push_back (c[(size_t) j * 2]);
        zones.push_back (c[(size_t) j * 2 + 1]);
        const float r = cnt[(size_t) j] > 0 ? (float) (rs[(size_t) j] / cnt[(size_t) j]) : 0.05f;
        zoneRadius.push_back (std::clamp (r, 0.02f, 0.15f));
    }
}

float Terrain::blurToSigma (float blur01) noexcept
{
    blur01 = std::clamp (blur01, 0.0f, 1.0f);
    return 0.004f + 0.25f * blur01 * blur01;
}

void Terrain::query (const Corpus& corpus, float x, float y, float sigma, float* zOut)
{
    const int n = corpus.size();
    const int dim = corpus.dim;
    std::fill (zOut, zOut + dim, 0.0f);
    if (n == 0)
        return;

    // Squared distances, and the minimum so the kernel never underflows.
    thread_local std::vector<float> d2;
    d2.resize ((size_t) n);
    float minD2 = std::numeric_limits<float>::max();
    for (int i = 0; i < n; ++i)
    {
        const float dx = corpus.xy[(size_t) (i * 2)] - x;
        const float dy = corpus.xy[(size_t) (i * 2 + 1)] - y;
        d2[(size_t) i] = dx * dx + dy * dy;
        minD2 = std::min (minD2, d2[(size_t) i]);
    }

    const float inv = 1.0f / (2.0f * sigma * sigma);
    double wsum = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const float w = std::exp (-(d2[(size_t) i] - minD2) * inv);
        if (w < 1e-6f)
            continue;
        wsum += w;
        const float* z = corpus.latents.data() + (size_t) i * (size_t) dim;
        for (int d = 0; d < dim; ++d)
            zOut[d] += w * z[d];
    }
    if (wsum > 0.0)
        for (int d = 0; d < dim; ++d)
            zOut[d] = (float) (zOut[d] / wsum);
}
