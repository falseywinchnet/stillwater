# CONV boundary coverage trial on the Neo

The owner requested the fastest completed CONV-AA form from the sibling
[Investigate fast CONV antialiasing](codex://threads/01a0cd6e-9c70-7731-ad34-87888a07ac6e)
experiment, then requested substantial optimization of repeated geometry work.
This trial uses its fast-float geometric coverage algebra, copied into
`assets/conv_coverage.metal`, with an aquarium-specific adapter in
`assets/conv_geometry.metal`. It does not import BFFT as a dependency or alter
that repository. The upstream account is
`/Users/ultimussecundai/bfft/experiments/conv_fast_aa/FOLLOWUP.md`.

## Contract and current status

`--aa conv-fast` selects an experimental single-sample renderer. `--aa point`
provides a single-sample control without the boundary pass. Normal launches
retain 4× MSAA. This is a trial, not a promoted quality-equivalent replacement.

The upstream fast shader integrates each triangle's pixel area. Its additive
composition is correct for a nonoverlapping surface partition, not arbitrary
opaque overlap. The sibling's separate exact visibility prototype needs complete
supplied tile geometry with at most four triangles and affine depth/color. It
has neither general aquarium binning nor textured material integration.

The adapter first renders solid interiors, then blends fractional open edges
and silhouettes over that image with depth testing and no depth writes. It
preserves the existing materials, analytic motion, lights, shadows, object
identities and scene edits. It does not solve visible color-area at arbitrary
subpixel overlap. Near-plane-crossing primitives retain their ordinary raster
interiors, but are omitted from the analytic boundary pass. The antialiasing
trial therefore has a restricted camera contract.

The source also uses MSAA alpha-to-coverage for some foliage and fish fins.
Single-sample rendering changes that opacity behavior. Correct edge areas alone
do not reproduce it. This is a separate obstacle to promotion.

## Removing repeated work

The first adapter recalculated three deformed vertices at each of six support
vertices and revisited all 1,437,106 instanced triangles on every frame. The
source has 573,877 vertices and 889,962 indexed triangles before instancing.

The optimized adapter:

- Stores a 40-byte prepared record per unique vertex/instance: clip position,
  world normal and local position. It reconstructs world position from the
  camera transform and shares prepared data with regular and shadow draws.
- Retains static preparation and boundary lists until camera, size or fixed
  object changes invalidate them. Moving geometry is prepared once per frame.
- Builds indexed mesh adjacency once. Open, inconsistent-winding and
  nonmanifold edges remain conservative candidates; duplicate vertex seams are
  not implicitly welded. Projected orientation identifies silhouettes.
- Rejects off-screen and closed-mesh back faces, then compacts boundary
  candidates entirely on the GPU. Stable group counts, prefix offsets and
  compaction preserve source order; atomic append order must not choose alpha
  composition order.
- Draws four support vertices per candidate with indirect triangle-strip draws,
  instead of six vertices for every triangle. The coverage shader ignores
  internal edges. It uses the upstream smaller conservative triangle/rectangle
  support rule and exact single-edge CDF / multi-edge boundary sum.
- Bounds all allocations. GPU list capacity includes every source triangle, so
  there is no overflow truncation or hidden primitive-count quality limit.

The heavy preparation version exceeded the device's geometry parameter capacity
with memoryless depth. The trial uses a private single-sample depth attachment
(4.73 MiB at 1408×881). The normal MSAA path keeps memoryless transient targets.
All retained vertex/list/topology memory is included in Metal allocation.

## Measurement corrections

The old timing collector discarded a command when it was still running at the
next frame. Under GPU saturation this reported zero timed frames. The bounded
pending-command queue now keeps commands until completion and drains at shutdown.
Frame totals and timed-frame totals can therefore be checked directly. Reported
GPU intervals include startup, not just the warm CPU interval.

PNG export previously interpreted RGB as premultiplied by material coverage.
That brightened translucent materials in exported images, while the native layer
is opaque. Capture now exports opaque RGB matching the layer contract. Earlier
images in `artifacts/conv-aa/` and `comparison/` are historical diagnostics with
that export defect; use `final/` images for visual conclusions. Existing storage
receipts remain frozen rather than being relabeled as current captures.

## Reproduction

From `/Users/ultimussecundai/stillwater`:

```sh
cmake --build build -j2
python3 tools/measure_conv_aa.py
build/Stillwater.app/Contents/MacOS/Stillwater --desktop --muted --aa conv-fast
```

The measurement script alternates MSAA / CONV / point / CONV / MSAA on the Neo,
then captures identical explicit times 0, 1, 1.04 and 2.5 seconds. Capture time
requires `--paused`; it is a deterministic diagnostic, not an animation offset.
The experiment does not install a login item or change system settings.

## Measured outcome, 1408×881 on A18 Pro

The final alternating run uses 19-second lifetimes, four seconds of warmup and
12-second CPU observation windows. Every submitted frame was timed after the
collector fix. GPU means include startup; achieved submission rate includes the
few placement/startup draws, and is not a separate display-presentation counter.

| Mode | Mean GPU command interval | Frames / wall second | Metal allocation |
| --- | ---: | ---: | ---: |
| 4× MSAA, before | 23.30 ms | 24.01 | 322.44 MiB |
| Optimized CONV, first | 36.48 ms | 19.29 | 274.12 MiB |
| Single-sample control | 10.23 ms | 24.08 | 197.59 MiB |
| Optimized CONV, second | 36.24 ms | 19.31 | 274.12 MiB |
| 4× MSAA, after | 22.01 ms | 24.07 | 322.44 MiB |

The experimental adapter saves 48.31 MiB (15.0%) of total Metal allocation
relative to current MSAA, but is slower. It does not satisfy the product target.
The logical single-sample camera cache is 40.22 MiB rather than 160.89 MiB;
boundary/vertex/topology resources cost another 71.63 MiB, plus private depth.
RSS overlaps with Metal on unified memory and must not be added to that total.
No power measurement or battery-life claim is made.

Earlier sequential development probes measured 88.31 ms for the unprepared
adapter, 80.27 ms after retaining vertices, 42.17 ms after boundary filtering,
and 34.23 ms after four-vertex strips. These are useful development observations,
not randomized isolated ablations. The final matched run is the comparison to
use. Its last CONV frame had 447,868 boundary candidates out of 1,437,106 source
triangles, and 21 total static preparation updates (one per fixed batch) over
369 frames. The four moving batches updated each frame. The source scene was
built once.

Against the MSAA image at four explicit times, average absolute RGB differences
are 5.74–5.76/255 for CONV, versus 6.11–6.12/255 for the single-sample control.
That modest improvement is not a quality pass: MSAA is a comparison reference,
not an exact area/color oracle. Subpixel overlap and material opacity remain
visibly different. Corrected side-by-side details are in
`artifacts/conv-aa/final/comparison-detail.png`.

## Validation and retained failures

Both normal 2× and 4× MSAA pass all 17 native retained/full-render cases and their
image gates (maximum difference 5/255). The CONV mode passes the 17 native scene
mutation, identity-query, camera, resize and cache invalidation checks, but fails
the stricter retained/full image gate: maximum 27/255 and worst mean about
0.0116/255, versus limits 8/255 and 0.01/255. The receipt is retained; those limits
are not relaxed. Adding full world coordinates to each prepared vertex cost
another 12 bytes per record and did not remove that difference, so that variant
was rejected. The failure is not established to come from that reconstruction.

The two MSAA and the CONV native verification runs used Metal API validation.
Release and sanitized core checks, a native sanitized CONV tap smoke and repeated
explicit-time image determinism are recorded in the accompanying evidence.
These validate implementation boundaries; they do not turn the experimental
coverage compositor into an exact overlap/transparency method.

Next work should preserve the reduced boundary workload while replacing the
approximate composition with visible color-area and a defined material opacity
contract. Simply adding more parallel triangle work recreates the original cost.

## Camera-local continuation

The next implementation changes the retained view instead of adding more work
to this world-boundary overlay. See [CAMERA_REGISTRY.md](CAMERA_REGISTRY.md).
It preserves MSAA acquisition and quality while sharing camera-visible surface
records. It is now the default MSAA cache; the CONV trial described here remains
optional and retains its documented visibility/opacity limitations.
