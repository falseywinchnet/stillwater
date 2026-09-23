# Stillwater on the MacBook Neo

The authoritative continuation is `/Users/ultimussecundai/stillwater`.
The source, Git history, assets, upstream provenance, documentation and development
artifacts were copied from
`/Users/joshuahkuttenkuler/Developer/Projects/stillwater` on the M4 Mini on
2026-09-23. The remote source was clean at `4a80ee3` (after prototype `465339b`).
Machine-specific CMake build directories were excluded and rebuilt locally.
The original M4 directory was left intact.

The source task is [Document GUI.Forms design principles](codex://threads/01a0ccfe-f3c3-7791-9fd4-37475d78fca7).
Its later turns created Stillwater and implemented retained camera visibility and
lighting. The continuation request explicitly moves development and usage here,
with Metal storage as the next target and a tighter machine budget.

This Mac is `Mac17,5`, Apple A18 Pro, five GPU cores, 8 GiB unified memory.
The tested desktop configuration renders 1408×881 image pixels. The original
M4 evidence uses 1600×900 for desktop comparisons; timings across those machines
and dimensions are not directly comparable.

## Carried context

- A separate C++20 product, with a typed C++ boundary to the Objective-C runtime.
  No Swift, SwiftUI, CloudKit, browser runtime, GUI.Forms or BFFT dependency.
- A real, queryable, retained 3D scene. No imagegen objects or background plates.
  The procedural Riverscape translation, conventional material textures and
  third-party attribution remain in the copied project.
- Roughly 7,772 retained objects and 18 actors, prepared geometry, analytic
  GPU motion, synthesized ambience and local glass-tap reactions.
- Desktop clicks may disturb fish through the explicit Desktop taps toggle.
  The aquarium and top control widget must leave keyboard focus alone. All
  actions, including quitting, are available by mouse.
- Explicit pause and sleep stop animation. Low application CPU is not a claim
  about compositor cost, total GPU utilization or battery life.
- Preserve the visual defaults while optimizing: 24 fps, 4× MSAA and the existing
  internal size policy. Optional quality reductions remain explicit options.
- The M4's retained renderer reduced its measured GPU duration but increased
  Metal allocation from about 205 to 418 MiB at 1600×900. Local sparse repair,
  exact moving-object picking and physically solved water optics are unfinished.

## Local continuation

Branch: `codex/neo-metal-storage`. The storage representation and local evidence
are documented in [METAL_STORAGE.md](METAL_STORAGE.md). Use that evidence for the
Neo; the older M4 receipts remain historical measurements.

```sh
cd /Users/ultimussecundai/stillwater
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
open build/Stillwater.app --args --desktop --muted
```

The app remains a local development bundle. There is no login item or change to
system wallpaper settings. The working copy, not the M4 copy, owns new edits.

## Menu-bar correction

The owner clarified that the top control means a macOS menu-bar dropdown.
The carried-over floating panel has been removed; the ◉ status item provides
all actions. Earlier widget measurements describe the prior UI.

The current default MSAA cache is the [camera-local registry](CAMERA_REGISTRY.md).
`--dense-camera` selects the prior cache for comparison. CONV geometric coverage
has not replaced MSAA; the earlier world-boundary overlay remains an experiment.
