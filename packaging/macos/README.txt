SEDIMENTS - macOS (Apple Silicon: M1, M2, M3, M4...)
====================================================

Requires macOS 14 (Sonoma) or later.

Installation
------------
1. Close Reaper.
2. Right-click (or Ctrl-click) "Install.command", then choose "Open".
   macOS asks for confirmation the first time, because the plugin
   is not signed by Apple: click "Open".
   If macOS still refuses: System Settings > Privacy & Security,
   at the bottom, "Open Anyway".
3. Reopen Reaper: Sediments shows up in the FX list
   (as VST3 and AU).

The plugin is installed for your user account only:
  ~/Library/Audio/Plug-Ins/VST3/Sediments.vst3
  ~/Library/Audio/Plug-Ins/Components/Sediments.component
and the standalone app goes to /Applications.

Nothing else to install: libtorch is bundled inside the plugin.

Source code and user guide: https://github.com/antoinensl/LPF_Sediments
