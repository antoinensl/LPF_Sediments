"""
Builds a tiny fake "RAVE-like" TorchScript model, for testing the plugin
pipeline without a real RAVE model. It exposes the same interface as models
exported with RAVE / nn~ :
  encode(x[B, 1, T])  -> z[B, D, T/ratio]
  decode(z[B, D, F])  -> y[B, 1, F*ratio]
  encode_params / decode_params buffers, and an `sr` attribute.

The "decoder" is a small additive synth whose pitch/brightness/level follow
the latent vector, with phase kept between calls (like a streaming model).

Usage:  python make_dummy_model.py dummy_rave.ts
"""
import sys
import math
import torch
import torch.nn as nn

D = 8
RATIO = 2048
SR = 44100


class DummyRave(nn.Module):
    sr: int
    ratio: int

    def __init__(self):
        super().__init__()
        self.sr = SR
        self.ratio = RATIO
        self.register_buffer("encode_params", torch.tensor([1, 1, D, RATIO]))
        self.register_buffer("decode_params", torch.tensor([D, RATIO, 1, 1]))
        self.register_buffer("forward_params", torch.tensor([1, 1, 1, 1]))
        self.register_buffer("phase", torch.zeros(1, 4))
        g = torch.Generator().manual_seed(0)
        self.register_buffer("proj", torch.randn(D, D, generator=g) * 0.5)

    @torch.jit.export
    def encode(self, x: torch.Tensor) -> torch.Tensor:
        b = x.shape[0]
        n = x.shape[-1] // self.ratio
        frames = x[..., : n * self.ratio].reshape(b, n, self.ratio)
        rms = frames.pow(2).mean(-1).sqrt()
        zc = (frames[..., 1:] * frames[..., :-1] < 0).float().mean(-1)
        mean = frames.mean(-1)
        peak = frames.abs().amax(-1)
        feats = torch.stack([rms, zc, mean, peak, rms * zc, rms - peak, zc * zc, rms * 4.0], 1)
        return torch.tanh(torch.einsum("de,ben->bdn", self.proj, feats) * 3.0)

    @torch.jit.export
    def decode(self, z: torch.Tensor) -> torch.Tensor:
        b = z.shape[0]
        nf = z.shape[-1]
        out = []
        t = torch.arange(self.ratio, dtype=torch.float32) / float(self.sr)
        for f in range(nf):
            zf = z[0, :, f]
            f0 = 110.0 * torch.pow(2.0, 2.0 + 2.0 * zf[0])
            amp = 0.15 * torch.sigmoid(2.0 * zf[1] + 1.0)
            frame = torch.zeros(self.ratio)
            for k in range(4):
                freq = f0 * float(k + 1)
                ph = self.phase[0, k] + 2.0 * math.pi * freq * t
                gain = amp * torch.sigmoid(zf[2 + k] * 3.0) / float(k + 1)
                frame = frame + gain * torch.sin(ph)
                self.phase[0, k] = torch.remainder(ph[-1] + 2.0 * math.pi * freq / float(self.sr), 2.0 * math.pi)
            out.append(frame)
        y = torch.cat(out).reshape(1, 1, -1)
        return y.repeat(b, 1, 1)

    def forward(self, x: torch.Tensor) -> torch.Tensor:
        return self.decode(self.encode(x))


if __name__ == "__main__":
    path = sys.argv[1] if len(sys.argv) > 1 else "dummy_rave.ts"
    m = torch.jit.script(DummyRave())
    m.save(path)
    print("saved", path)
