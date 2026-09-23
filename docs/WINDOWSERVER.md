# WindowServer cost investigation

The strongest observation is a visibility-dependent compositor workload with
Stillwater stopped. Minimizing all windows reduced WindowServer CPU to 13.6%
and the whole-device GPU counter to 0%; restoring windows raised them to 44.8%
and 21.5%. A subsequently saved WindowServer CPU sample catches Core Animation
layer preparation, Metal command submission and backdrop blur preparation.
These observations narrow the mechanism but do not establish an OS bug or
attribute GPU time to a particular effect or application.

The device is an A18 Pro MacBook Neo, macOS 26.6.1 (25G76), internal 2408×1506
panel. The accepted two-view build was used. Stillwater was stopped at the start
and left stopped at the end. Tests were muted. Production code and system
preferences were not changed.

## Visibility control and stack sample

The user minimized all windows, then restored them at approximately 18:46 local
time. Stillwater was stopped throughout this follow-up. The stable comparison
excludes the restoration transition and later computer-use interactions.

| State | Local interval (end exclusive) | Samples | WindowServer CPU | Whole-device GPU |
|---|---|---:|---:|---:|
| All windows minimized | 18:45:20–18:45:55 | 34 | 13.6% | 0.0% |
| Windows restored | 18:46:10–18:46:35 | 24 | 44.8% | 21.5% |

CPU is on a one-core percentage scale. GPU is a whole-device integer counter;
zero means below its reported resolution, not zero energy use. This is one
visibility transition, not a repeated randomized test. It does not separate
application rendering from compositor GPU work. It substantially weakens the
prior interpretation that the GPU remained permanently busy after Stillwater
exited. Activity Monitor alone was also reported by the user to raise load;
that individual-window observation was not independently quantified.

Activity Monitor successfully sampled WindowServer at 18:49:46.905. The saved
CPU call graph contains 129 observations of the main thread, of which 99 are
in its service-message wait and 25 under the display-update callback. Within
that update path are layer-tree traversal, visible-region calculations,
`CompositorMetal::CompositeLayersToDestination`, and Metal command submission.
One sampled stack passes through `capture_backdrop`, `prepare_blur_mipmap`,
`MetalContext::create_variable_blur_mip_surface`, and a compute encoder.

This directly observes backdrop blur preparation even though Reduce Transparency
was enabled when preferences were inspected. It does not identify the surface,
prove blur dominates, or supply GPU timings. The `CA::OGL` namespace in some
symbols is not evidence of an OpenGL fallback: the same stacks explicitly use
Metal. Deep recursive layer-preparation stacks must not be mistaken for many
independent samples. External-display-named threads were waiting throughout the
sample; their names do not establish an active phantom display.

The display-mode query reports 1408×881 logical points, 2816×1762 backing pixels
at 60 Hz, versus the 2408×1506 physical panel: 36.8% more backing pixels. This
is a possible cost multiplier, not a measurement of each compositor pass or
proof of the CPU cause. A window inventory found 107 records, 27 onscreen,
with five owned by WindowServer (three onscreen). The inspected GPU recovery
counter was zero. Neither check establishes a leaked-window or GPU-reset cause.
Recent logs include cursor-surface errors, invalid-window constraints and a few
render-fence timeouts; concurrent display transitions and screen capture prevent
attributing those errors as the cause of sustained load.

A relevant historical precedent is Electron's
[macOS 26 window-mask/shadow fix](https://github.com/electron/electron/pull/48376):
removing a private corner-mask override fixed excessive WindowServer GPU use.
That establishes that window configuration can trigger disproportionate
compositor work. It does not establish that this machine has that already-fixed
bug; the fix author's cache-identity explanation is a hypothesis.

Sanitized counters, sample fingerprint and interpretation are in
[evidence/windowserver-visibility-2026-09-23.json](evidence/windowserver-visibility-2026-09-23.json).
The full OS sample stays in local artifacts, outside the public repository.

## Earlier exploratory observations

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

## Remaining attribution limits

The immediate discriminating tests concern the desktop: repeat individual-window
visibility controls with stationary input and fixed content; then compare native
and scaled display modes with the user's agreement. These separate the cost of
window composition from the scaling multiplier. A GPU timeline is still needed
to rank backdrop, shadow, blend and presentation passes by execution time.

Command-line sampling lacked privilege, but Activity Monitor's helper supplied
the CPU sample above. Noninteractive powermetrics access was unavailable and
Instruments/xctrace was not installed. No credentials were requested. The CPU
sample does not replace a per-process GPU trace. Production rendering and
system preferences were not changed by this investigation.

Earlier exploratory data remains in
[evidence/windowserver-2026-09-23.json](evidence/windowserver-2026-09-23.json).
