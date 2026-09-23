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
mechanical clatter. That identifies a candidate cause, pending a listening check. There are no rising pitch
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

Leaf pearling adds at most 144 bubbles, selected beneath actual leaf vertices.
Each instance references its source leaf and parent plant; the existing strand
motion carries it until release. A staggered 24–43 second cycle grows a pearl,
releases it for six seconds, and fades it before reattachment. This is an artistic
cycle, not simulated gas production or adhesion. A shared 63-vertex, 96-triangle
mesh supplies all pearls: at the cap, 13,824 triangles and one additional main
camera draw. The pearl batch is omitted from shadows. No framebuffer, texture,
per-frame CPU particle update, or new per-pixel cache is added. Parent transforms
remain live references, so moving a plant moves its attached pearls as well.

Water has a vertical light gradient and reduced distance haze, making more of
the existing rear planting visible. The hemispheric fill is cooler and the lower
water darker. This changes lighting and separation of existing geometry rather
than adding a background image.

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
input monitor, or keyboard handler. Fast motion near a fish causes a gentler
local escape; a resting/slow pointer does nothing, and a 2.5-second per-fish
cooldown prevents repeatedly restarting flight. Foreground window rectangles
suppress reactions under other apps, the Dock, and menus. This conservative
rectangle check also suppresses reactions through transparent parts of a window.

Desktop clicks continue to belong to Finder. Glass tapping is available in the
preview. Controls stay in the menu-bar dropdown and preview context menu.

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
- A 25-second muted desktop smoke at 1408×881 submitted 601 frames, reused one
  fixed camera acquisition, and allocated 250,494,976 bytes of Metal resources
  (238.89 MiB). This is a smoke receipt, not an isolated performance comparison
  or a GPU-utilization measurement.

See [the verification receipt](evidence/neo-atmosphere.json). Reproduce with
`ctest --test-dir build --output-on-failure` and `python3 tools/verify_native.py`.
