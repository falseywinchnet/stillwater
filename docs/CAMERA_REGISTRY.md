# Camera-local surface registration

The Neo continuation now retains distinct visible fixed-surface records per pixel.
It is the default cache for 2×/4× MSAA. `--dense-camera` selects the preceding
four-texture cache; `--compact-camera` selects the new registry explicitly.
Single-sample `--aa point` and the earlier `--aa conv-fast` experiment retain their
existing cache and behavior.

This change is a camera representation, not a completed CONV coverage renderer.
MSAA still acquires coverage, retains individual raster depths, draws moving
geometry and resolves the final image. The world scene remains queryable and
available for edits, disocclusion and light-space shadows.

## Representation and lifetime

Acquisition renders the existing fixed surfaces with unchanged materials and
sample positions. A GPU classification pass compares all seven 32-bit storage
words of each sample's surface record. Identical records within a pixel share one
entry. A record contains the existing half-precision lighting coefficients,
32-bit camera distance and object identity, and half-precision interpolation
correction; registration introduces no additional quantization.

Each pixel stores an eight-byte map: a record offset and two-bit selectors for
its samples. Each distinct record occupies 28 bytes. Depth32F and R16F shadow
visibility stay per sample, preserving their different meanings. Empty pixels
currently retain one zero record as well. There is no assumption that matching
object IDs alone imply matching shading or geometry.

Classification uses 256-thread local prefix scans. At camera acquisition only,
the CPU reads one count per group (about 19 KiB at the default dimensions),
constructs the group offsets and allocates the exact record count. A second GPU
pass packs the records. The dense acquisition textures are released after the
command buffer has finished using them. Animation performs no registration,
count readback or traversal of fixed world geometry.

Camera changes, dimensions and relevant fixed-instance edits invalidate the
whole registered view. The existing full acquisition fallback is retained;
this is not sparse tile repair. Moving objects continue to draw over the fixed
view and uncover its existing surfaces. Camera acquisition is synchronous and
uses temporary dense storage, so the steady-state saving is not a reduction in
peak rebuild storage. Frequent camera motion will need a different allocation
and repair policy.

For N pixels, S samples and R distinct records, the retained-view payload is
`N*(8 + 6*S) + 28*R` bytes, compared with `34*N*S` previously. At 1408×881 and
four samples, this tank has 1,599,411 records for 1,240,448 pixels: about 1.29
records per pixel instead of four. All buffers are included in the allocation
measurements; there is no world-boundary geometry cache in this path.

## Neo measurements

Final matched runs on the A18 Pro, 1408×881, 4× MSAA, requested 24 fps:

| Run | Mean GPU ms/frame | Frames/s | Metal MiB | Warm CPU, one core |
| --- | ---: | ---: | ---: | ---: |
| dense-before | 24.05 | 24.06 | 322.44 | 3.08% |
| camera-first | 22.74 | 24.03 | 238.88 | 3.82% |
| camera-second | 24.65 | 23.98 | 238.88 | 3.91% |
| dense-after | 24.65 | 24.06 | 322.44 | 3.74% |

Total Metal allocation falls from **322.44 to 238.88 MiB**, saving **83.56 MiB
(25.9%)**. The retained-view payload falls from **160.89 to 80.56 MiB**. Both
paths sustain 24 fps; the timing spread does not support claiming a meaningful
speedup. The camera view was registered once in each 458-frame run. No CONV
world-boundary storage or updates were allocated.

The acquisition command took 27.4 and 62.0 ms in the two camera runs, including
initial shadow work. That cost is included above, not hidden behind warmup.
Process RSS varies with memory residency and compression; the raw warm and peak
values are retained in the receipt, and must not be added to Metal allocation.

[Evidence and source hashes](evidence/neo-camera-registry.json) include every run,
all verification reports and the independent sample reconstruction counts.

## Validation

The native 17-case suite passes at both 2× and 4×, exercising moving actors,
fixed edits, camera translation and rotation, light changes, time rewind,
resize to a non-threadgroup-aligned extent and cached identity/depth queries.
`--verify-retained` additionally runs an independent GPU inverse check before
releasing acquisition textures. Across the two suites, 21,389,778 reconstructed
samples matched their original seven words exactly.

The queried object identities, world coordinates and raster depths match the
saved pre-registry renderer exactly. Full-redraw reference images are unchanged.
Retained images differ from that renderer by at most 2/255, within the existing
8/255 maximum and 0.01/255 mean limits. Exact stored data does not establish
bit-identical subsequent floating-point shading across different shader
programs; the image test is separate. Four additional 1408×881 captures at times 0, 1, 1.04 and 2.5 seconds differ
from the saved MSAA renderer by at most 3/255 and mean 0.000016/255.
The ordinary retained-versus-full-render
image gates also pass without tolerance changes.

The evidence receipt records final GPU, CPU, allocation and capture comparisons.
`tools/measure_camera_cache.py` runs dense/camera/camera/dense at the same native
dimensions, 4× samples and 24 fps. It includes the acquisition command in total
GPU time, drains all submitted frame commands, and samples warm application and
concurrent WindowServer CPU separately. GPU duration is not a power measurement.
The `camera_acquisition_command_gpu_seconds` field includes the other work in
that command, including initial shadows; it is not an isolated classify-kernel
benchmark. Diagnostic inverse-check work is excluded from production benchmarks.

## Camera-based CONV continuation

The failed world-boundary overlay visited and shaded many candidates that could
not contribute to the camera image. Parallelizing those candidates did not fix
the visibility or partial-opacity model. The new default avoids that experiment's
additional geometry preparation entirely, while preserving current MSAA quality.

A geometric CONV replacement still needs conservative projected primitive
support in camera tiles, visible edge/plane relationships, and composition of
partial coverage in depth order. Candidate registration must include a subpixel
feature even when no MSAA point hits it. A nearest-surface depth buffer or these
four sampled identities alone cannot reconstruct such a feature. Shared edges,
intersections, leaves and fin opacity need explicit treatment; averaging an
image edge is not equivalent.

Keep the fixed camera registration persistent. Moving silhouettes should update
their old/new affected regions and resolve against the fixed registry, with an
honest fallback for complex overlap. The expensive world-space data remains the
source for edits and shadows; the recurring AA evaluator should operate on the
camera's registered visibility. That geometric registration/evaluator is still
unimplemented; this commit supplies and validates the camera-local storage step.
