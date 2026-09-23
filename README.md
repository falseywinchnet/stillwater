# Stillwater

A native C++ desktop aquarium. The first scene translates the procedural Riverscape
from [Desktop Habitats](https://github.com/chaseleantj/desktop-habitats), with retained
3D objects, Metal rendering, local glass-tap reactions, and original synthesized sound.

This is a working first prototype. It carries the reference's actual geometry and
surface textures into a new renderer; its lighting and fish behavior are not yet an
exact reproduction. There is no image-generation content, video backdrop, web view,
Swift, SwiftUI, or CloudKit in the app.

The active MacBook Neo working copy and carried task context are documented in
[the local handoff](docs/NEO_HANDOFF.md). The current renderer uses a compact
position cache and memoryless temporary targets on Apple GPUs; see
[Metal storage and Neo verification](docs/METAL_STORAGE.md).

## Run

On macOS with Xcode command-line tools and CMake:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
open build/Stillwater.app --args --preview
```

Launch without `--preview` for desktop placement. A compact **Stillwater** widget
sits near the top of the screen. Drag its background to move it. Its buttons control
pause/resume, sound, desktop taps, window/desktop placement, and quitting.
The **◉** menu-bar item and the aquarium's right-click menu expose the same actions.

- Click the preview to tap the glass and startle nearby fish.
- Normal desktop mode passes clicks through to Finder.
- **Desktop taps on** temporarily places the aquarium above desktop icons and
  accepts taps. Click that same button to turn taps off and restore desktop access.
- The widget remains above the aquarium in both modes.
- The aquarium and widget never become keyboard targets. There are no Escape,
  Space, letter-key, or Command-Q handlers, and no menu keyboard shortcuts.
- Closing the preview returns it to the desktop.
- `--muted` starts silently. `--fps 1..60` changes the animation rate; default 24.
- Optional profiling controls: `--msaa 2` reduces coverage samples, and
  `--render-scale 0.75` reduces each internal image dimension. Defaults remain
  four samples and full internal size (capped at 1600 pixels wide).

The host currently uses the main display. It does not install a login item, change
system wallpaper settings, monitor global input, or request screen recording access.
The compiled app is a local development build, not a notarized distribution.

## What persists

The scene retains about 7,700 objects, 16 fish, two crabs, a bubble emitter, geometry,
material textures, and per-object GPU records. Construction and upload happen once.
Fish trajectories and plant bending are evaluated on the GPU from retained parameters.
A glass tap changes only nearby creature records; moving a plant or rock changes one
instance record. The static shadow map is cached. Moving shadows refresh at 8 Hz or
on an edit. Explicit pause stops the frame timer and ambient audio.

The fixed camera view now retains depth, surface identity, world shading positions,
and material/lighting coefficients for sand, rocks, wood and other fixed imported
surfaces. A separate cache retains direct-light visibility at those surfaces until
the shadow map changes. Moving plants and animals rasterize against the retained
depth each frame, preserving occlusion and newly exposed background. Caustics still
animate at the original frame rate. `--full-redraw` selects the comparison renderer.

Moving a fixed object conservatively rebuilds the fixed-view cache; moving a plant
does not. Light intensity changes reuse the caches. The final composition still
covers the whole view: local tile repair, receiver-specific shadow invalidation,
exact moving-object picking, collision-aware fish routes, and a deeper environment
remain development work. CPU, GPU, compositor and memory costs are measured
separately; see [the retained experiment](docs/RETAINED_VISIBILITY.md) and
[performance measurements](docs/PERFORMANCE.md).

## Source and art

Application, sound, scene ownership, input, and platform integration are C++20.
Metal shaders run on the GPU. A small typed C++ boundary calls macOS's Objective-C
runtime; AppKit is still the native window service, without Objective-C++ source.

The upstream procedural geometry is translated **offline** into a versioned mesh
archive by `tools/translate_riverscape.mjs`. The original MIT source is vendored
verbatim with hashes. Node and Three.js are developer-side asset tools only; neither
is needed to build from the checked-in archive or to run the app. The geometry
builders have not all been rewritten in C++. Rebuild the archive with:

```sh
node tools/translate_riverscape.mjs
cmake --build build --target stillwater_assets
```

The archive contains positions, normals, material coordinates, current-response
parameters, instances, and object identities—not rendered pictures. Conventional
Poly Haven CC0 albedo/normal maps provide the wood, rock, and sand surfaces.
See [third-party credits](THIRD_PARTY.md), [engine design](docs/ENGINE.md),
[the house style](docs/PROGRAMMING_HOUSE_STYLE.md), and [BFFT review](docs/BFFT_REVIEW.md).

## Diagnostics

```sh
python3 tools/measure.py --seconds 20
python3 tools/measure.py --compare-gpu --seconds 15
python3 tools/measure.py --compare-retained --seconds 20
./build/Stillwater.app/Contents/MacOS/Stillwater --preview --muted --verify-retained artifacts/retained-verification
python3 tools/compare_retained.py artifacts/retained-verification  # requires Pillow
./build/Stillwater.app/Contents/MacOS/Stillwater --preview --smoke-tap --quit-after 7 --metrics /tmp/stillwater.json
cmake -S . -B build-sanitize -DSTILLWATER_SANITIZERS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build-sanitize -j4
ctest --test-dir build-sanitize --output-on-failure
```

`--capture /absolute/path.png` reads our own Metal render target and saves a PNG.
`--export-tap file.wav` and `--export-ambience file.wav` export the synthesized audio.
