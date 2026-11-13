# Third-party notices

Sediments is © 2026 Antoine Boucherikha (Life Pass Filter), licensed under the GNU Affero General Public License v3 (see `LICENSE`).
Source code: https://github.com/lifepassfilter/LPF_Sediments

Sediments includes the following third-party software. Their licence texts are in the `licenses` folder.

| Component | Copyright | Licence | Licence text |
|---|---|---|---|
| JUCE 8.0.4 | Raw Material Software | AGPLv3 (used here under AGPLv3) | `licenses/JUCE-LICENSE.md`, `LICENSE` |
| VST3 SDK (shipped with JUCE) | Steinberg Media Technologies | GPLv3 (used here under GPLv3) or Steinberg VST3 licence | `licenses/VST3_SDK-LICENSE.txt`, `licenses/GPL-3.0.txt` |
| LibTorch (PyTorch C++ library), bundled with the plugin | PyTorch contributors, Meta Platforms | BSD-3-Clause; bundled third-party components are listed in its NOTICE | `licenses/PyTorch-LICENSE.txt`, `licenses/PyTorch-NOTICE.txt` |
| Atkinson Hyperlegible font (embedded) | Braille Institute of America | SIL Open Font License 1.1 | `licenses/OFL-AtkinsonHyperlegible.txt` |
| OpenDyslexic font (embedded) | Abbie Gonzalez | SIL Open Font License 1.1 | `licenses/OFL-OpenDyslexic.txt` |

## Not included

- **RAVE models**: Sediments loads `.ts` models exported with [RAVE](https://github.com/acids-ircam/RAVE) for [nn~](https://github.com/acids-ircam/nn_tilde), but ships none. Each model has its own licence; many public models are CC BY-NC 4.0 (non-commercial). Check the licence of every model you use.
- **Inspirations, no code used**: [nn.terrain](https://jasper-zheng.github.io/nn_terrain/) by Jasper Shuoyang Zheng, [CataRT](https://ismm.ircam.fr/catart/) by IRCAM.
