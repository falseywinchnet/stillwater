# Render cost audit on the MacBook Neo

The default renderer now uses dedicated foliage vertex and fragment entry points.
The known Riverscape foliage material is made explicit to the Metal compiler,
allowing the general fish, rock and prototype paths to be removed from this hot
batch. Geometry, animation formulas, material formulas, coverage, shadow quality,
resolution and frame cap are unchanged. `--generic-foliage` restores the previous
shader for controlled comparisons.

## Where the work goes

The camera registry had removed duplicate storage, but the old restore still
shaded each coverage sample independently. More importantly, the animated pass
was using one general shader across very different material families. The scene
has 502,066 foliage triangles in one batch, plus 190,656 instanced fish-body
triangles and 90,112 instanced fin triangles. These are submitted every frame.
They are source triangle counts, not counts of visible pixels or measured GPU
shader invocations.

Diagnostic shader ablations measured about 20–21 ms/frame normally, about 19 ms
with cheap fixed-view restoration, and about 9 ms with a trivial moving-material
shader. The last experiment deliberately changes alpha/lighting and can also
remove unused vertex work; it is not an isolated fragment-pass timing. It locates
a hot path, not a shippable quality setting or an additive decomposition of cost.
Dedicated foliage entry points address that path without changing its formulas.

## Framebuffers and storage

At 1408×881, the current device reports roughly:

| Resource group | MiB |
| --- | ---: |
| Geometry, instance and actor buffers | 81.07 |
| Compact fixed camera view, logical payload | 80.56 |
| Six material textures, allocated | 32.34 |
| Two 2048² shadow maps, allocated | 32.25 |
| Total Metal allocation, including remaining resources/alignment | 238.88 |

The main MSAA color/depth attachments already use memoryless tile storage on this
Apple GPU. Removing their host descriptor objects will not recover backed
framebuffer storage. The persistent camera depth and shadow maps have actual
cross-pass consumers. The fixed and combined shadow maps avoid rerasterizing
fixed casters during each moving-shadow update; the copy is not an accidental
duplicate camera image.

Normal animation uses one main render pass for fixed restoration plus moving
geometry. Shadow refreshes also update the combined shadow depth and retained
receiver visibility. Fixed acquisition attachments are temporary and appear
only when the fixed view is invalidated. The storage totals above are not
process RSS and must not be added to RSS on unified memory.

## Optional per-record shading

`--record-shading` computes fixed lighting once per distinct camera record,
retains half-precision receiver visibility per record, and writes one RGB8 color
per record for sample restoration. It removes the multisample receiver-visibility
texture and its render pass, but adds a compute pass each frame. It retains
all sample identities and raster depths. Color quantization matches the existing
BGRA8 render target before MSAA resolve; it does not introduce a lower-resolution
image or a reduced sample count.

This variant saves about 0.70 MiB of allocated Metal storage in the measured view.
Timing results vary; its extra pass and color-buffer traffic can offset shared
shading. It is retained as a reproducible option, not made the default on the
strength of framebuffer count alone.

## Rejected submission-order experiment

A 64-bin front-to-back foliage ordering reused the existing index buffer and
reduced some measured frame times. It also changed sparse pixels by as much as
63/255 against the previous renderer, despite low average error. The experiment
was rejected and removed from the app. No triangles were removed and the image
limits were not relaxed. Its measurements and rejection are retained in the
cost-audit evidence; the public default keeps original triangle order.


## Final matched measurements

| Run | GPU ms/frame | Frames/s | Metal MiB | Warm CPU, one core |
| --- | ---: | ---: | ---: | ---: |
| generic-before | 21.10 | 23.57 | 238.88 | 2.66% |
| specialized-first | 18.45 | 23.89 | 238.88 | 3.49% |
| record-shading | 17.63 | 23.86 | 238.17 | 3.24% |
| specialized-second | 18.58 | 23.91 | 238.88 | 3.33% |
| generic-after | 21.20 | 23.85 | 238.88 | 3.16% |

The final two-sided comparison averages 21.15 → 18.52 ms/frame,
about 12.5% less GPU command time with no additional backed GPU
storage. An earlier short sweep showed larger gains; the final matched receipt
is the conservative headline.

[Full evidence and hashes](evidence/neo-render-costs.json) preserve all cases,
including the rejected ordering experiment and its failed image comparison.

## Verification and reproduction

At 2× and 4×, the specialized shader passes all 17 native retained/full cases,
the unchanged image gates, and exact camera-record reconstruction. Comparing
with the previous shader preserves every sampled camera identity, coordinate
and depth; RGB differs by at most 1/255. The old storage-only comparator reports
non-identical full-redraw RGB, correctly: this is a shader change, not a
storage-only rewrite. `compare_renderer.py` applies the existing visual bounds
to both full and retained images and separately requires exact visibility probes.

Run from a clone after building and installing `requirements-dev.txt`:

```sh
python3 tools/verify_native.py
python3 tools/measure_render_costs.py
```

The benchmark runs generic/specialized/record/specialized/generic at identical
4× MSAA, 24 fps and internal dimensions, with a four-second warmup and a
12-second CPU interval. All submitted frame commands are drained for GPU timing.
The evidence receipt includes exact executable/shader hashes and raw cases.
Performance is hardware-, scene- and load-dependent; it is not a battery-life
measurement or a claim that the two renderers do identical work.
