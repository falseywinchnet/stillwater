# Retained camera visibility and direct lighting

The default renderer now acquires the fixed camera layer once and reuses its
visibility and material response. It is an implemented retained stage, not a
complete sparse image-repair engine. Fish, plants and other moving surfaces still
rasterize each frame, and final composition covers the full image.

## What is retained

The fixed layer contains imported sand, rocks, wood, gravel and moss geometry.
Each covered MSAA sample retains camera depth, object identity, world shading
position, base colour, attenuated albedo and two scalar lighting weights. Objects behind the moving
layer are present in this cache even while a fish or plant hides them.

The material calculation is factored into:

```
linear colour = base + intensity * visibility * (direct + caustic(time) * caustic_coefficient)
```

`base` includes fixed ambient/fill illumination and water fog. `direct` includes
albedo, mapped normal response, light colour and water attenuation.
`caustic_coefficient` retains the material, orientation and attenuation factors.
The direct and caustic vectors share the attenuated albedo, so the cache packs them
as that RGB value plus two scalar orientation weights. It does not store three
redundant RGB vectors. Tone mapping happens after evaluating the expression. The wave-caustic function
continues at the original animation rate; the optical model has not been replaced
with physical refraction.

A second retained texture stores the filtered light-to-surface visibility at each
fixed camera sample. It is refreshed only when the moving shadow map or fixed
camera surfaces change. Light intensity is a scalar applied at composition time;
changing it needs neither material reacquisition nor new occlusion tests.

The composition pass restores fixed depth per sample. Moving geometry then uses
the ordinary depth test and alpha-to-coverage against it. A fish leaving a region
reveals the cached background; nothing from its previous colour or depth is stored
there. Static geometry exposed by moving another static object is rediscovered by
rebuilding the fixed acquisition pass.

## Dependencies

| Change | Fixed camera/material cache | Fixed shadow map | Receiver visibility cache |
| --- | --- | --- | --- |
| Ordinary animation | Reuse | Reuse | Refresh when moving shadow map advances |
| Glass tap / creature-route edit | Reuse | Reuse | Refresh immediately with moving shadows |
| Plant or other moving-instance edit | Reuse | Reuse | Refresh immediately with moving shadows |
| Fixed-instance edit | Rebuild whole fixed view | Rebuild | Rebuild |
| Camera or output dimensions | Rebuild whole fixed view | Reuse | Rebuild |
| Primary-light intensity | Reuse | Reuse | Reuse |
| Time rewind | Reuse | Reuse | Refresh with recomputed moving shadows |

Instance revisions are inspected only when scene revision changes. The renderer
classifies changed records by their actual material/motion dependency, not their
broad object-kind label. For example, imported moss and animated plants can share
an object-kind label while requiring different invalidation.

The current light direction/projection and material assets are immutable renderer
inputs. Runtime edits to them are not exposed. Replacing scene geometry/storage
requires recreating the renderer; it now rejects replacement rather than silently
using stale resident buffers. Camera changes and light intensity have named APIs.

Fixed-object edits currently invalidate the entire fixed view. This conservatively
covers old/new footprints, disoccluded geometry and changed shadow receivers.
Moving-shadow changes invalidate receiver visibility across that view, including
receivers outside the moving object's screen footprint. There is no claim of
per-tile discovery, local receiver lists or BVH traversal yet.

## Query and comparison interfaces

`Renderer::query_fixed_visibility(x, y, result)` synchronously reads up to four
samples from the last prepared fixed view. Coordinates are integer render pixels
from the top-left. Each sample supplies world shading position and scene identity;
the separate depths are the raster sample depths. Identity zero means background.
These are the fixed surfaces underneath moving geometry, not a query of the final
frontmost fish/plant. Invalid coordinates or an unprepared view return false.
There is no GPU readback in normal animation.

`--full-redraw` selects a reference path that recomputes fixed materials and draws
all geometry. It shares the lighting expression but does not read the retained
textures; a fresh full-redraw process does not allocate those textures.
Switching paths within the verification harness also tests light-cache invalidation
while the reference path advances the shadow map.

## Verification

The native `--verify-retained` harness creates 15 exact-time image pairs: initial
view, animation, unchanged shadow reuse, a tap, escape, a moved/restored rock, a moved
plant, shifted/unchanged camera, dimmed/unchanged light, odd-sized resize, return from
the reference path, and time rewind. It asserts cache build/reuse rules, checks
cached identities and finite shading positions, rejects invalid queries/intensity,
and rejects a different scene's storage.

Both two- and four-sample MSAA comparisons passed. A native ASan/UBSan run passed the
same 15 cases (leak detection disabled). The largest mean RGB channel difference
across the tested pairs was about 0.00151 on a 0–255 scale; the largest individual
channel difference was 5. Half-precision stored coefficients and visibility permit
small rounding differences. These tests are numerical image comparisons, not a
claim of bitwise identity or proof for arbitrary scenes.

[The verification receipt](evidence/retained-verification.json) includes image and
source hashes. The comparison requires mean channel error below 0.01/255 and maximum
channel error at most 8/255. A separate check against the original full renderer
at commit `465339b` changed only two channel values by one:
[original-renderer comparison](evidence/original-renderer-comparison.json).
The [paused desktop receipt](evidence/retained-paused-desktop.json) recorded only
four startup/placement draws over its five-second run. Startup resized the paused
view, producing two acquisitions of each camera-dependent cache; the fixed shadow
map built once. No animation timer runs while explicitly paused.

```sh
./build/Stillwater.app/Contents/MacOS/Stillwater --preview --muted --verify-retained artifacts/retained-verification
python3 tools/compare_retained.py artifacts/retained-verification
python3 tools/measure.py --compare-retained --seconds 20
```

The image-comparison script requires Pillow. Native capture reads the app's own
render target and does not need screen-recording permission.

The packed-lighting refactor was also compared with its preceding full-redraw
image at time zero: only two RGB channel values changed, each by one. This
separately checks the shared expression instead of relying only on agreement
between two paths that call it.

## Desktop performance

On the local Apple M4/macOS 26.5, four sequential desktop runs used the order
full, retained, retained, full. Every run rendered 1600×900 at 24 fps with the same
four-sample antialiasing, shadow schedule, scene and muted audio. CPU samples were
20 seconds after four seconds of warmup; GPU averages include startup frames.

| Path | Mean GPU duration per frame | Warm app CPU, one core | Metal allocation counter |
| --- | ---: | ---: | ---: |
| Full redraw, before | 8.144 ms | 2.80% | 204.7 MiB |
| Retained, first | 5.526 ms | 3.10% | 418.0 MiB |
| Retained, second | 5.639 ms | 3.05% | 418.0 MiB |
| Full redraw, after | 8.307 ms | 2.70% | 204.7 MiB |

The mean GPU frame duration fell by **32.1%**. CPU remained around 3%, slightly
higher in the retained samples; this is a GPU-time improvement, not a demonstrated
CPU reduction. These counters do not measure Activity Monitor GPU utilization,
whole-system power or battery life. WindowServer included other applications and
averaged 25.0–26.1% alongside full redraw and 26.6–26.8% alongside retention.

Each retained run built the fixed view once and reused it 649 times. Fixed camera
geometry draw calls fell from 13,650 to 21 across 650 frames. The receiver-light
cache built 172–173 times and was reused on the remaining 477–478 frames. These
counts establish reuse without lowering frame rate or slowing the light animation.

The source/executable hashes and complete measurements are in
[the desktop receipt](evidence/desktop-retained-performance.json). The earlier
unpacked preview experiment is retained separately as
[historical evidence](evidence/retained-unpacked-performance.json).

## Memory and remaining work

The cache uses two RGBA16F coefficient textures, one
RGBA32F world-position/identity texture, depth32F and R16F light visibility: 38 bytes
per MSAA sample before allocation padding. At 1180×728 and four samples this is
124.5 MiB of additional logical GPU storage, down from 150.7 MiB in the first
working version. This change leaves antialiasing unchanged. `gpu_allocated_bytes` reports Metal's
device allocation counter separately from process RSS; do not add them as though
they were disjoint physical-memory totals.

At the tested 1600×900 desktop size the packed cache is 208.7 MiB logically, with
about 213.4 MiB added to Metal's allocation counter including padding/overhead.

Further representation changes may reduce memory. Local damage
repair and conservative source-to-receiver region tracking are the next architectural
steps. They must retain this full redraw as an image-correctness reference. Dense
foliage and broadly animated light can still make a full pass cheaper than sparse
bookkeeping.

The MSAA retention uses standard shader-readable multisample textures, documented
in Apple's [MSAA sample](https://developer.apple.com/documentation/metal/improving-edge-rendering-quality-with-multisample-antialiasing-msaa).
The local implementation and native measurements, rather than that sample, establish
the behavior and costs reported here.
