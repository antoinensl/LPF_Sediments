// libtorch is only included here, so the rest of the plugin never sees its headers.
#ifndef NOMINMAX
 #define NOMINMAX   // keep windows.h (pulled in by torch on Windows) from defining min/max macros
#endif
#include <torch/script.h>
#include <torch/torch.h>

#include "RaveModel.h"

#include <stdexcept>
#include <cstring>
#include <regex>

struct RaveModel::Impl
{
    torch::jit::script::Module module;
};

RaveModel::RaveModel() : impl (std::make_unique<Impl>()) {}
RaveModel::~RaveModel() = default;

static std::vector<int> readParams (torch::jit::script::Module& m, const std::string& name)
{
    if (! m.hasattr (name))
        return {};
    auto t = m.attr (name).toTensor().to (torch::kInt64);
    std::vector<int> out;
    for (int64_t i = 0; i < t.numel(); ++i)
        out.push_back ((int) t[i].item<int64_t>());
    return out;
}

void RaveModel::load (const std::string& p)
{
    c10::InferenceMode guard;
    try
    {
        impl->module = torch::jit::load (p);
        impl->module.eval();
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error (std::string ("Could not load model: ") + e.what());
    }

    auto& m = impl->module;
    if (m.find_method ("encode") == std::nullopt || m.find_method ("decode") == std::nullopt)
        throw std::runtime_error ("This model has no encode/decode methods (is it a RAVE model exported for nn~?)");

    auto enc = readParams (m, "encode_params");
    auto dec = readParams (m, "decode_params");
    if (enc.size() != 4 || dec.size() != 4)
        throw std::runtime_error ("Model is missing encode_params / decode_params");

    inChannels  = std::max (1, enc[0]);
    latentSize  = dec[0];
    ratio       = dec[1];
    outChannels = std::max (1, dec[2]);

    if (latentSize <= 0 || ratio <= 0)
        throw std::runtime_error ("Unexpected model dimensions");

    srKnown = false;
    for (auto* name : { "sr", "sampling_rate", "sample_rate" })
    {
        if (m.hasattr (name))
        {
            auto v = m.attr (name);
            if (v.isInt())      { sampleRate = (int) v.toInt(); srKnown = true; break; }
            if (v.isDouble())   { sampleRate = (int) v.toDouble(); srKnown = true; break; }
            if (v.isTensor())   { sampleRate = (int) v.toTensor().item<double>(); srKnown = true; break; }
        }
    }
    if (! srKnown)
    {
        // Many shared models encode the rate in the file name, e.g. "_r48000_" or "_sr44100_".
        std::smatch m;
        const auto file = p.substr (p.find_last_of ("/\\") + 1);
        if (std::regex_search (file, m, std::regex ("(?:^|[_-])s?r(16000|22050|24000|32000|44100|48000|88200|96000)(?:[_.-])")))
        {
            sampleRate = std::stoi (m[1].str());
            srKnown = true;
        }
        else
            sampleRate = 44100;
    }

    path = p;
    loaded = true;
}

std::vector<float> RaveModel::encode (const std::vector<std::vector<float>>& audio)
{
    if (! loaded || audio.empty())
        return {};

    c10::InferenceMode guard;
    const auto n = (int64_t) audio[0].size();
    auto x = torch::zeros ({ 1, (int64_t) inChannels, n });
    for (int c = 0; c < inChannels; ++c)
    {
        const auto& src = audio[(size_t) std::min (c, (int) audio.size() - 1)];
        x[0][c].copy_ (torch::from_blob ((void*) src.data(), { n }, torch::kFloat32));
    }

    auto z = impl->module.get_method ("encode") ({ x }).toTensor();  // [1, D, F]
    z = z.select (0, 0).transpose (0, 1).contiguous().to (torch::kFloat32); // [F, D]
    std::vector<float> out ((size_t) z.numel());
    std::memcpy (out.data(), z.data_ptr<float>(), out.size() * sizeof (float));
    return out;
}

std::vector<std::vector<float>> RaveModel::decode (const float* zData, int nFrames)
{
    std::vector<std::vector<float>> out;
    if (! loaded || nFrames <= 0)
        return out;

    c10::InferenceMode guard;
    auto z = torch::from_blob ((void*) zData, { (int64_t) nFrames, (int64_t) latentSize }, torch::kFloat32)
                 .transpose (0, 1).unsqueeze (0).contiguous();     // [1, D, F]

    auto y = impl->module.get_method ("decode") ({ z }).toTensor().to (torch::kFloat32);
    // RAVE stereo models may return [2, 1, T] (batch = channels) or [1, 2, T].
    if (y.dim() == 3 && y.size (0) > 1 && y.size (1) == 1)
        y = y.transpose (0, 1);
    y = y.select (0, 0).contiguous();                                  // [C, T]

    const auto channels = (int) y.size (0);
    const auto len = (size_t) y.size (1);
    out.resize ((size_t) channels);
    for (int c = 0; c < channels; ++c)
    {
        out[(size_t) c].resize (len);
        std::memcpy (out[(size_t) c].data(), y[c].data_ptr<float>(), len * sizeof (float));
    }
    return out;
}
