# SES EQ  (VST3 dynamic EQ)

## Build (Windows / macOS / Linux)
Requires CMake 3.22+, a C++17 compiler, and internet access on first configure (JUCE is fetched automatically).

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

`COPY_PLUGIN_AFTER_BUILD` installs the VST3 into your system VST3 folder. Output is also in
`build/SESEQ_artefacts/Release/VST3/SES EQ.vst3`. A Standalone app is built too for quick testing.

## Use
- Double-click empty graph space: add a band. Double-click a node: remove it.
- Drag nodes: frequency/gain. Mouse wheel over a node: Q.
- Bottom panel edits the selected band: type, Freq, Gain, Q, and (when "Dynamic" is on)
  Threshold, Dyn Range (negative = compress, positive = expand), Attack, Release.
- White dot on a dynamic node shows the live gain; hollow circle shows the range target.

## AI Scan
Press **AI Scan**, play the vocal (about 8 s of singing/speaking; **Apply now** finishes early), and SES EQ
builds an EQ from what it heard: low cut, up to 5 resonance/tone bells, a dynamic de-esser (band 7) and an
air/HF shelf (band 8). **AI Intensity** scales how hard it corrects. **Undo AI** restores your previous bands.
It works by long-term spectrum analysis against a reference vocal curve (see `Source/AutoEq.h`, tweak the
`tf`/`td` tables to change the target tone).

## Windows quick build
Double-click `build_windows.bat` (needs Visual Studio 2022 with "Desktop development with C++", CMake and Git).
Results land in `dist\`: `SES EQ.vst3` (plugin) and `SES EQ.exe` (standalone). No Visual Studio? Push this folder to a
GitHub repo and the included workflow builds it in the cloud; download it from the Actions tab > Artifacts.
