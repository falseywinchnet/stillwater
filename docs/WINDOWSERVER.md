# WindowServer cost investigation

The 2026-09-23 investigation concerns macOS compositor overhead, using Stillwater
as a workload that exposes it. It confirms substantial GPU work attributed to
WindowServer, but does not establish which OS mechanism causes it. The
last stopped control remained expensive after all aquarium processes exited.
This makes the initial stopped/animated difference insufficient for causal
attribution. Other applications and user activity were left running.

The device is an A18 Pro MacBook Neo, macOS 26.6.1 (25G76), internal 2408×1506
panel. The accepted two-view build was used. Stillwater was stopped at the start
and left stopped at the end. Tests were muted. Production code and system
preferences were not changed.

## Observations

Each row is a sequential 12–15 second exploratory observation. WindowServer CPU
is cumulative process CPU time divided by elapsed wall time, on a one-core
percentage scale. GPU is the mean of IOAccelerator's whole-device utilization
counter sampled once per second; it is not WindowServer-only utilization.

| State | WindowServer CPU | Whole-device GPU |
|---|---:|---:|
| Initially stopped | 46.1% | 19.7% |
| Paused aquarium | 41.6% | 17.8% |
| Animated, requested 24 fps | 42.6% | 80.7% |
| Identical scene redrawn, requested 24 fps | 33.9% | 74.3% |
| Identical scene, view explicitly opaque | 28.4% | 87.0% |
| Identical scene, requested 12 fps | 41.6% | 79.1% |
| Aquarium exited | 48.3% | 85.5% |
| Later stopped observation | 49.8% | 77.2% |

Activity Monitor independently showed WindowServer GPU snapshots of 45.4% while
Stillwater animated and 75.1% after its processes exited. Snapshot percentages
use their own sampling windows and must not be added to or subtracted from the
whole-device averages. An initial paused snapshot showed 0% WindowServer GPU.
The later high load cannot be explained solely by aquarium shaders still running.
It could reflect continuing compositor state, another changing desktop workload,
or both. No WindowServer restart or logout was attempted.

Under this competing workload the 40-second animated run delivered 769 frames
in 40.1176 seconds (19.2 fps), with mean GPU command elapsed time about 41.9 ms.
The previous 24 fps / 27.2 ms result was under different desktop conditions.
GPU command elapsed time and its reciprocal do not establish system-wide frame
capacity, GPU utilization, or battery cost.

## Presentation audit

Runtime properties were checked in an isolated diagnostic build:

- NSWindow opaque: true; CAMetalLayer opaque: true; window shadow: disabled.
- The containing NSView initially reports opaque: false. Explicitly overriding
  it to true did not demonstrate a reduction; it is not a promoted fix.
- Window: 1408×881 points; screen backing scale: 2; layer contents scale: 1.
  The application drawable is 1408×881, while the panel is 2408×1506.
  This establishes a scaling mismatch; it does not measure the exact internal
  compositor pass dimensions or cost.
- Display synchronization is enabled; transaction-based presentation is disabled.
- Drawable framebufferOnly is false. The paired-view implementation needs this
  because its final compute kernel writes the averaged image into the drawable.
- Two offscreen views are averaged, then **one** drawable is presented per app
  frame. The app is not submitting two independent desktop windows per frame.
- Pausing stops its recurring frame timer. Identical-frame diagnostics instead
  freeze scene time at 14 while continuing to redraw and present. They still
  execute scene shaders, so they do not isolate presentation alone.
- Pointer exposure queries run only on appreciable pointer movement or explicit
  tracing. Typing alone does not cause this window-list query in Stillwater.

Apple documents that [framebufferOnly](https://developer.apple.com/documentation/quartzcore/cametallayer/framebufferonly)
permits display optimizations when true, and that direct pixel read/write access
requires disabling it at a performance cost. A normal final render pass with
framebufferOnly enabled is therefore a concrete candidate to compare. This
investigation did not perform that comparison or prove that it explains the
WindowServer behavior.

Apple also documents that [behind-window visual effects](https://developer.apple.com/documentation/appkit/nsvisualeffectview/blendingmode-swift.enum/behindwindow)
blend and blur desktop/windows behind them. Its [AppKit design presentation](https://developer.apple.com/videos/play/wwdc2025/310/)
describes adaptive glass and the cost advantage of sharing a sampling pass.
An animated desktop can therefore create dependencies in translucent foreground
surfaces, even when those applications do little CPU work themselves. This is a
plausible mechanism here, not a measured attribution to a particular application.

## Next discriminating tests

1. Establish a repeatable quiet baseline, then repeat stopped / paused / animated /
   stopped with the same foreground windows and no changing external content.
   Persistence after exit must reproduce before calling this an OS regression.
2. Compare the existing compute presentation with a single fullscreen render-pass
   average and framebufferOnly=true, preserving both views, resolution and colors.
3. In a separately authorized display-preference experiment, compare transparency
   enabled/disabled and a native display scaling configuration. Restore settings.
4. Use a privileged per-process GPU trace or Instruments to attribute compositor
   passes. Noninteractive powermetrics access was unavailable in this session;
   no credentials were requested or settings changed.

Raw sample series and Activity Monitor observations are in
[evidence/windowserver-2026-09-23.json](evidence/windowserver-2026-09-23.json).
The local artifact directory contains the diagnostic source, logs and sampler.
