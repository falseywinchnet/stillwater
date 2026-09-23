# Aquarium atmosphere and interaction

This revision replaces the repeating tonal ambience, restores omitted fish-material
features, and adds leaf pearling while preserving the 24 fps / 4× MSAA default.
The appearance intentionally changes; old-frame RGB equivalence is not its goal.

## Sound

The live audio queue owns a continuous C++ generator with five motor modes and
24 reusable bubble voices. It allocates no memory in its callback and stores no
ambience loop. The previous 16-second stereo float loop occupied 2.69 MiB.
The motor uses a quiet 60 Hz harmonic series weighted toward 120 Hz, with filtered
mechanical noise. Bubble formation uses exponential waiting times, a distribution
of radii, short damped resonances, and low-pass filtering. A very quiet continuous filtered
noise bed supplies the return-water layer. The initial random splash envelopes
(0.65 seconds mean spacing) were removed after the listener reported recurring
mechanical clatter. The listener confirmed that removing this layer eliminated the clatter. There are no rising pitch
sweeps or scheduled repeating phrases. Restarting sound applies a half-second fade.

The approximate shallow-water bubble relation f ≈ 3.26 / radius-in-metres follows
[resonant bubble acoustics](https://pmc.ncbi.nlm.nih.gov/articles/PMC6014985/).
This is physically inspired synthesis, not a recording of a specific pump or a
calibrated simulation of the tank's coupled acoustic field. Audible events are
not individually synchronized to visible bubbles. Convincing sound still requires
listening on the intended speakers or headphones.

The glass tap is now a 200 ms, heavily damped fingertip contact. The previous
730/1931/3173 Hz metallic ring and 157 Hz knock were much too prominent: the new
centered export peaks 34.1 dB below the old one. The burst-free ambience peaks at -38.40 dBFS
and has RMS 0.00359; the new centered tap peaks at −42.70 dBFS. These are digital
sample levels, not measured loudness at the listener's ears. Runtime stays muted
when launched with `--muted`; sound is enabled through the menu.

`--export-ambience audition.wav` writes a 30-second fade-ended excerpt of the
same generator. It is not the live playback buffer.

## Water and fish

The main emitter contains 84 small bubbles of different sizes, phases and rise
speeds. Wobble widens with height, a mild current carries the column sideways,
and the shells grow slightly on ascent. A thin rim and overhead glint use the
existing MSAA alpha-to-coverage path. They are geometry, not image sprites.
This inexpensive shell appearance does not solve optical refraction.

Leaf pearling adds 1,728 tiny bubbles beneath near-horizontal leaf surfaces,
three times the preceding 576-instance budget. World-space normals admit surfaces
within about 20 degrees of horizontal; vertical blades and steep leaf faces cannot
hold pearls. The animated transform fades the pearl between 20 and 25 degrees
of support tilt and suppresses it beyond 25 degrees. Bubbles sit directly below
the supporting surface, without the previous outward margin offset.

Groups contain up to six pearls, with a cap of 54 per plant. Half the placement
budget is reserved for foreground planting. A temporary 64×40 planting-depth guide
at load time favors front leaves; it is a placement heuristic for the default
view, not an exact visibility solution. Candidate ordering is deterministic and
covers the full foliage archive. Radii remain 0.014–0.026 world units, with 70–100%
growth. The broader specular footprint survives desktop sampling.
Each instance references its source leaf and parent plant; the existing strand
motion carries it until release. A staggered 24–43 second cycle grows a pearl,
releases it for six seconds, and fades it before reattachment. This is an artistic
cycle, not simulated gas production or adhesion. A shared 63-vertex, 96-triangle
mesh supplies all pearls: at the cap, 165,888 triangles and one additional main
camera draw. The pearl batch is omitted from shadows. No framebuffer, texture,
per-frame CPU particle update, or new per-pixel cache is added. Parent transforms
remain live references, so moving a plant moves its attached pearls as well.

Far water is dark green with a broad, subdued overhead gradient. Distance haze
now converges toward this darker water, increasingly dimming rear planting beyond
the foreground. Fish, sand, and wood close to the camera keep their direct light.
This is an artistic depth cue applied to existing geometry; it adds no texture,
framebuffer, or volumetric transport pass.

The upstream fish anatomy mesh was already present. This revision restores
selected upstream features that the simplified port omitted: scale and lateral
line detail with derivative-based fading, gill and mouth detail, subdued flank
reflection, thin-tissue warmth, and red fin bases that clear toward the margins.
The eye gets a tighter highlight instead of the body's broad silver treatment.
The imported fish are brought back to upstream's 0.83–1.08 size range and spread
through a deeper water column. It is still an analytic lighting approximation,
not Three.js's complete environment/clearcoat/iridescence model. The native school
still has 16 fish and retained analytic routes, not upstream's 24-fish behavioral
simulation, feeding, collision avoidance, and changing swimming modes.

## Desktop interaction

The aquarium always sits below Finder icons and passes all desktop clicks and
drags through. **Fish react to pointer** is enabled by default and independently
switchable from the menu. The host samples `NSEvent.mouseLocation` at most about
eight times a second while animating. It does not install an event tap, global
input monitor, or keyboard handler. Motion above 0.03 screen-heights per second
near a fish causes a gentler local escape; a resting/very slow pointer does nothing, and a 2.5-second per-fish
cooldown prevents repeatedly restarting flight. Foreground window rectangles
suppress reactions under other apps, the Dock, and menus. This conservative
rectangle check also suppresses reactions through transparent parts of a window.
The inactive Dock's full-display layer-20 helper is excluded: its invisible
rectangle previously blocked the whole desktop. The actual Dock panel, active
Dock UI, and foreground application windows still block reactions. Use
`--trace-pointer` to log coordinates and window-blocking decisions locally.

Desktop clicks continue to belong to Finder. Glass tapping is available in the
preview. Controls stay in the menu-bar dropdown and preview context menu.

## Shadow cadence

Moving shadows refresh on 16 Hz phase boundaries, while rendering remains at
24 fps. This alternates one- and two-frame gaps and averages 16 refreshes per
second. A simple minimum 1/16-second delay would quantize down to 12 Hz at 24 fps.
Geometry edits, light changes, and time rewinds retain their invalidation rules.
The higher cadence adds shadow work but no shadow textures.

## Verification

- Release and ASan/UBSan core checks pass, including audio bounds, callback block
  continuity, lack of the old 16-second repeat, quiet tap limits, pointer locality
  and cooldown, and leaf attachment/release continuity.
- All 17 retained/full-render comparisons pass at both 2× and 4× MSAA. The
  independent camera-record inverse check passes. Generic and specialized foliage
  paths remain within the existing shader comparison bounds with exact probes.
- Native Finder icon selection was checked while the aquarium was on the desktop.
  The native run confirms mouse pass-through and refusal of keyboard focus.
  Computer-use automation could not execute a background drag; end-to-end desktop
  pointer movement remains a manual acceptance check, separate from the core tests.
- The original atmosphere revision's 25-second muted desktop smoke at 1408×881 submitted 601 frames, reused one
  fixed camera acquisition, and allocated 250,494,976 bytes of Metal resources
  (238.89 MiB). This is a smoke receipt, not an isolated performance comparison
  or a GPU-utilization measurement.

The follow-up's core checks include 16 Hz scheduling at 24, 30, and 60 fps.
The follow-up desktop smoke submitted 594 frames and 401 moving-shadow updates
in 25.23 seconds, with one camera acquisition and 250,544,128 bytes (238.94 MiB)
of Metal resources: 48 KiB above the earlier atmosphere revision. This includes
startup and normal desktop activity, not an isolated GPU-utilization benchmark.
Native preview input recorded one pointer reaction separately from its glass tap.
A desktop trace confirms that the invisible Dock helper is ignored and exposed
space is accepted; the automation backend still cannot drag the Finder desktop.

See the [original receipt](evidence/neo-atmosphere.json) and
[depth/pearling follow-up receipt](evidence/neo-depth-pearling.json). Reproduce with
`ctest --test-dir build --output-on-failure` and `python3 tools/verify_native.py`.

The horizontal-surface correction is recorded in
[its own verification receipt](evidence/neo-horizontal-pearling.json). Its core
checks require all 1,728 instances, reject vertical and parent-rotated supports,
and verify that a pearl sits directly below a horizontal surface.
