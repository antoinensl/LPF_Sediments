#ifndef NOMINMAX
 #define NOMINMAX
#endif
#include <torch/torch.h>

#include "LearnedTerrain.h"

#include <chrono>
#include <cmath>
#include <thread>
#include <cstring>

namespace
{
    constexpr int numFourier   = 32;     // random frequencies per axis pair
    constexpr float freqScale  = 2.0f;   // higher = more detail, closer to the points
    constexpr int hidden       = 128;
    constexpr int gridWidth    = 128;
    constexpr int gridHeight   = 96;
    constexpr double pi        = 3.14159265358979323846;

    struct TerrainNetImpl : torch::nn::Module
    {
        torch::Tensor B;   // [2, numFourier], fixed random frequencies
        torch::nn::Sequential mlp { nullptr };

        explicit TerrainNetImpl (int outDim)
        {
            B = register_buffer ("B", torch::randn ({ 2, numFourier }) * freqScale);
            const int inDim = 2 * numFourier + 2;
            mlp = register_module ("mlp", torch::nn::Sequential (
                torch::nn::Linear (inDim, hidden), torch::nn::SiLU(),
                torch::nn::Linear (hidden, hidden), torch::nn::SiLU(),
                torch::nn::Linear (hidden, hidden), torch::nn::SiLU(),
                torch::nn::Linear (hidden, outDim)));
        }

        // xy in [-1, 1], shape [N, 2]
        torch::Tensor forward (const torch::Tensor& xy)
        {
            auto proj = 2.0 * pi * torch::matmul (xy, B);
            return mlp->forward (torch::cat ({ torch::sin (proj), torch::cos (proj), xy }, 1));
        }
    };
    TORCH_MODULE (TerrainNet);
}

struct LearnedTerrain::Impl
{
    TerrainNet net { nullptr };
    torch::Tensor mean, std;   // latent normalisation, [D]
};

LearnedTerrain::LearnedTerrain() : impl (std::make_unique<Impl>()) {}
LearnedTerrain::~LearnedTerrain() = default;

std::shared_ptr<LearnedTerrain> LearnedTerrain::train (std::shared_ptr<const Corpus> corpus,
                                                       const std::function<bool (float)>& progress,
                                                       int pauseMs)
{
    if (corpus == nullptr || corpus->size() < 4 || corpus->dim < 1)
        return nullptr;

    torch::manual_seed (0);
    torch::AutoGradMode gradOn (true);

    const int n = corpus->size();
    const int d = corpus->dim;

    auto xy = torch::from_blob ((void*) corpus->xy.data(), { n, 2 }, torch::kFloat32).clone() * 2.0 - 1.0;
    auto z  = torch::from_blob ((void*) corpus->latents.data(), { n, d }, torch::kFloat32).clone();
    auto mean = z.mean (0);
    auto std = z.std (0).clamp_min (1e-3);
    auto zn = (z - mean) / std;

    std::shared_ptr<LearnedTerrain> result (new LearnedTerrain());
    auto& net = result->impl->net;
    net = TerrainNet (d);
    net->train();

    const int steps = 2000;
    const int batch = std::min (n, 512);
    const double lr0 = 3e-3, lr1 = 1e-4;
    torch::optim::Adam opt (net->parameters(), torch::optim::AdamOptions (lr0));

    float loss = 0.0f;
    for (int step = 0; step < steps; ++step)
    {
        // cosine learning-rate decay
        const double t = (double) step / steps;
        const double lr = lr1 + 0.5 * (lr0 - lr1) * (1.0 + std::cos (pi * t));
        for (auto& group : opt.param_groups())
            static_cast<torch::optim::AdamOptions&> (group.options()).lr (lr);

        auto idx = batch < n ? torch::randint (n, { batch }, torch::kLong) : torch::arange (n);
        auto pred = net->forward (xy.index_select (0, idx));
        auto l = torch::mse_loss (pred, zn.index_select (0, idx));
        opt.zero_grad();
        l.backward();
        opt.step();

        if (step % 20 == 0)
        {
            loss = l.item<float>();
            if (! progress ((float) step / steps))
                return nullptr;
        }
        if (pauseMs > 0)
            std::this_thread::sleep_for (std::chrono::milliseconds (pauseMs));
    }
    result->finalLoss = loss;

    net->eval();
    result->impl->mean = mean;
    result->impl->std = std;
    result->corpusPtr = corpus;
    result->dim = d;

    // Picture of the terrain: evaluate a grid, colour it by the 3 main directions
    // of variation of the predicted latents.
    {
        c10::InferenceMode guard;
        auto gx = torch::linspace (-1.0, 1.0, gridWidth);
        auto gy = torch::linspace (-1.0, 1.0, gridHeight);
        auto mesh = torch::meshgrid ({ gy, gx }, "ij");
        auto pts = torch::stack ({ mesh[1].reshape (-1), mesh[0].reshape (-1) }, 1);   // [H*W, 2] (x, y)
        auto out = net->forward (pts);                                                  // normalised latents
        auto centred = out - out.mean (0);
        auto svd = torch::linalg_svd (centred, false);
        auto vh = std::get<2> (svd);
        const int k = std::min<int64_t> (3, vh.size (0));
        auto comps = torch::matmul (centred, vh.slice (0, 0, k).transpose (0, 1));       // [H*W, k]

        result->gridW = gridWidth;
        result->gridH = gridHeight;
        result->rgb.assign ((size_t) gridWidth * gridHeight * 3, 0);
        for (int c = 0; c < 3; ++c)
        {
            torch::Tensor col;
            if (c < k)
            {
                auto v = comps.select (1, c);
                auto lo = torch::quantile (v, 0.02), hi = torch::quantile (v, 0.98);
                col = ((v - lo) / (hi - lo).clamp_min (1e-6)).clamp (0.0, 1.0);
            }
            else
            {
                col = torch::full ({ gridWidth * gridHeight }, 0.5);
            }
            col = col.contiguous();
            const float* p = col.data_ptr<float>();
            for (int i = 0; i < gridWidth * gridHeight; ++i)
                result->rgb[(size_t) i * 3 + (size_t) c] = (uint8_t) std::lround (255.0f * (0.10f + 0.5f * p[i]));
        }
    }

    progress (1.0f);
    return result;
}

void LearnedTerrain::query (float x, float y, float* zOut) const
{
    c10::InferenceMode guard;
    float in[2] = { x * 2.0f - 1.0f, y * 2.0f - 1.0f };
    auto xy = torch::from_blob (in, { 1, 2 }, torch::kFloat32);
    auto out = (impl->net->forward (xy) * impl->std + impl->mean).contiguous();
    std::memcpy (zOut, out.data_ptr<float>(), (size_t) dim * sizeof (float));
}
