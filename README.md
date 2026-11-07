# FiveInOne (VST3 + Standalone, JUCE 7)

Auto-Tune / 6-band EQ / Visual EQ + spectrum analyser / 3-band multiband compressor / Output limiter

## Build
Requirements: CMake >= 3.22, C++17 compiler (Visual Studio 2022 / Xcode / GCC), git.

    cmake -B build -DCMAKE_BUILD_TYPE=Release
    cmake --build build --config Release

JUCE 7.0.12 is downloaded automatically. The VST3 is copied to your system VST3 folder
after the build (COPY_PLUGIN_AFTER_BUILD). Output is also in build/FiveInOne_artefacts/.

## Notes
- Auto-Tune adds ~1024 samples latency (reported to the host).
- Visual EQ: drag nodes (freq/gain), mouse wheel over node = Q, double-click = reset gain.

## Build without a local toolchain (GitHub Actions)
Push this folder to a GitHub repository. The workflow in .github/workflows/build.yml
builds the VST3 for Windows and macOS; download it from the "Actions" tab -> Artifacts.
