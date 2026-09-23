# Performance evidence

Measured locally on an Apple M4 running macOS 26.5, 2026-09-23. These are short
development measurements, not battery-life or power measurements.

`tools/measure.py` takes process CPU-time deltas after four seconds of warmup.
CPU percentages are fractions of one logical CPU core. Memory is resident memory
reported by `ps`. Metal command-buffer start/end timestamps report GPU execution
intervals; they are not Activity Monitor GPU utilization or watts. GPU averages
include launch frames. The last command and commands not completed by the next
frame may be absent from the timing sample; timed-frame counts are preserved.

The test window is 1180 by 728 image pixels. The desktop may render at a different
size (up to 1600 pixels wide), so these values do not certify full-screen cost.
The top native control widget is present. Other applications remain running.

The retained renderer supersedes the full-redraw measurements below. See
[retained visibility and lighting](RETAINED_VISIBILITY.md) for its implementation,
image checks and memory tradeoff. The full-redraw mode remains available for
comparisons at identical visual settings.

The latest 1600×900 desktop comparison measured 8.14–8.31 ms/GPU frame for full
redraw and 5.53–5.64 ms for retention, a 32.1% reduction in the two-run averages.
CPU was 2.70–2.80% versus 3.05–3.10%; Metal allocation was 204.7 versus 418.0 MiB.
The retained result trades extra memory and a small observed CPU increase for less
GPU execution. Resolution, 24 fps animation and antialiasing were unchanged.

## Original full-redraw build

After adding closed-mesh back-face culling, three runs used 20-second warm sampling
windows. [The receipt](evidence/performance.json) records executable and asset hashes.

| State | App CPU | Resident memory | Mean GPU ms/frame |
| --- | ---: | ---: | ---: |
| Animated, muted | 2.70% | 366.7 MiB | 6.99 |
| Animated, sound enabled | 3.50% | 370.4 MiB | 7.00 |
| Paused, muted | 0.05% | 366.6 MiB | Not sampled |

The paused run made three startup/presentation draws, then no animation frames.
Its zero timed GPU frames do not mean startup had no GPU cost. Every run retained
one geometry build and one fixed shadow build. No instances or creature routes
were changed or re-uploaded during undisturbed animation.

WindowServer baseline was 10.09% of one core, concurrent animated readings were
19.58–19.87%, and the paused reading was 9.59%. These include other applications.
The separate quality sweep had substantially different background compositor load.
This is not a controlled power experiment, and the runs do not establish a speedup
from culling. The unchanged default remains roughly 7 ms per GPU frame here.

## Quality comparison

Four sequential runs used 15-second CPU sampling windows. Each scene rendered for
about 22 seconds; a render-target screenshot was taken before the CPU sample began.
The table reports measured total command duration divided by elapsed scene time.
This is a useful within-session comparison, not a GPU utilization percentage.

| Setting | App CPU | Mean GPU ms/frame | GPU ms per elapsed second |
| --- | ---: | ---: | ---: |
| Default: 24 fps, four samples, full internal size | 2.66% | 6.62 | 158.8 |
| Two samples, otherwise unchanged | 2.60% | 5.74 | 137.5 |
| 75% width and height, otherwise unchanged | 2.40% | 5.35 | 128.3 |
| 18 fps, otherwise unchanged | 1.93% | 6.49 | 117.1 |

Two samples reduce GPU duration by about 13%, but native captures show noticeably
coarser coverage on narrow leaves. The reduced image dimensions save about 19% but
discard detail. The lower frame rate saves about 26% of accumulated GPU duration
with fewer motion updates. None of these tradeoffs became the default.

The reference's frames reusing shadows averaged 6.33 ms; frames refreshing shadows
averaged 7.40 ms. These are whole-frame groups, not isolated pass timings, and their
difference is only an estimate of additional shadow-refresh cost. The main visible
draw is the larger target for further work.

WindowServer averaged about 42.36% in the no-aquarium baseline and 41.62–41.95%
alongside these runs. Those numbers include unrelated applications and desktop
activity. They cannot be attributed to Stillwater or used to claim compositor savings.

The comparison preceded the closed-mesh culling change. Its exact executable,
shader and archive hashes are in [the receipt](evidence/gpu-comparison.json).

## Closed-mesh culling

The renderer now rejects back faces of the imported closed rock shells and fish
bodies. Leaves, fins and other materials keep two-sided rendering. A deterministic
time-zero capture before and after changed 56 of 2,577,120 RGB channel values;
mean absolute difference was 0.000129 on the 0–255 scale, maximum 18. The images
are almost identical, not byte-identical. See [the comparison](evidence/cull-comparison.json).

## Further work

Retaining mesh buffers and object state already avoids recurring CPU construction.
It does not skip repeated vertex deformation, rasterization and surface shading.
The next candidates are visibility/depth reuse for fixed geometry, reuse of common
plant-root deformation terms, measured mesh simplification for distant foliage,
and screen-region repair with correct disocclusion and shadow dependencies.

A camera move, dense swaying plants or animated lighting may invalidate most of the
image. Sparse repair must be compared against this full-frame fallback for both
image correctness and total work; it is not automatically cheaper. No near-zero
animated GPU or whole-system power claim is established.

Reproduce the standard and optional-quality runs with:

```sh
python3 tools/measure.py --seconds 20
python3 tools/measure.py --compare-gpu --seconds 15
```
