# Retained aquarium engine

The model follows GUI.Forms' retained, persistent, event-driven paradigm without
making GUI.Forms itself a 3D engine or modifying Paint. This repository owns the
scene and aquarium policy. GPU rasterization is a separate backend choice.

| Principle | Meaning here |
| --- | --- |
| Retained | Rocks, plants, animals and their records survive between frames. |
| Declarative | Prepared mesh/material/current data describes the constructed scene. |
| Imperative | Named operations mutate existing objects: move an object, tap the glass. |
| Persistent | Geometry, textures, instance buffers and static direct-light visibility are cached. |
| Event loop | Input and state changes schedule work. Active animation has a timer; paused rendering does not poll. |

## Implemented

`Scene` owns contiguous vertex/index data, instances, objects and creature routes.
Objects have stable IDs and bounds. Rendering batches are independent of object
identity: a batched foliage vertex addresses its particular retained plant record.
The offline translator preserves each plant attachment root as a distinct object,
as well as individual gravel, moss and other instances.

`move_object` moves the object's query center to a requested world position and
patches its transform. Per-record revision numbers let the renderer copy exactly
changed 112-byte instances or 80-byte creature routes. Metal blit commands order
writes after earlier submitted frames, avoiding unsynchronized shared-buffer edits.
Geometry does not change during animation or a tap. No per-frame CPU fish simulation
or whole-scene reconstruction is performed.

`query` is explicitly a bounding-sphere candidate query, including current analytic
creature and bubble poses. It is not an exact first-visible-triangle picker. Broad
foliage bounds include room for sway. Tap response uses projected creature locations;
it does not yet test plant occlusion or infer distance through the glass.

The C++ scene loader validates the archive's magic, bounded counts, byte completeness,
finite attributes, triangle indices, instance references and actor references before
publishing a replacement. A failed replacement leaves the existing scene intact.
Its little-endian IEEE754 data layout matches the shader records explicitly.

Textures are decoded and mipmapped once. Four-sample coverage approximates thin leaf
and fin transmission. This is coverage transparency, not physically solved refraction.
The moving caustic pattern is a shader approximation, not baked ray-traced illumination
or a solved refracting water surface. Direct shadows use filtered raster depth maps.
The diffuse/specular/fill model is a partial translation of the upstream materials,
not its full physically based pipeline.

A static depth map retains fixed occluders. A moving-shadow refresh copies that map
and draws moving casters, so previous fish positions cannot remain as stale shadows.
An edit to a fixed instance rebuilds the fixed map. Moving maps refresh at 8 Hz
or on a scene edit, while the visible scene is normally drawn at 24 Hz. These rates
are deliberate approximations, not a claim that all optical changes are tracked.

The default path now retains fixed camera visibility, lighting coefficients and
receiver shadow visibility, as described in [the retained experiment](RETAINED_VISIBILITY.md).
Moving surfaces still rasterize every frame and composition covers the entire view.
`--full-redraw` selects the full-frame reference. General spatial queries still scan
retained bounds. There is no BVH or camera tile dependency graph yet.

## Concrete algorithms and cost

- Rendering uses indexed triangles, instanced batches, perspective projection and
  a depth buffer. Four-sample MSAA with alpha-to-coverage represents thin leaves and
  fins. Closed imported rock shells and fish bodies cull back faces; foliage and
  other materials remain two-sided. There is no CPU ray tracer in this path.
- Plant bending combines four slow current waves with root-dependent phase,
  compliance, a saturating distance-squared bend and traveling ripples. The shader
  also adjusts the normal by the bend slope. This is analytic motion, not fluid
  dynamics or an elastic rod solver.
- Creature routes are analytic trigonometric loops. A tap uses cubic ease-out
  along a short escape segment, then resumes a continuous loop from that endpoint.
  Body/tail motion is separate sinusoidal deformation. There is no flocking,
  navigation mesh or obstacle avoidance yet.
- Shadows use a 2048-square depth map, orthographic light projection, depth bias
  and a 3-by-3 percentage-closer filter (each comparison uses linear filtering).
  The fixed map persists; moving-caster passes load a copy of it. This is a soft-edge
  approximation, not area-light integration or emitted shadow rays.
- Surface appearance combines texture albedo, derivative-based tangent normal
  mapping, hemisphere/fill illumination, Lambert diffuse, selected specular and
  leaf-backlighting terms, exponential color absorption, distance fog, ACES-fit
  tone mapping and gamma encoding. It is not the original full PBR implementation.
- Caustics use nested sine waves in world-space surface coordinates, sharpened
  into bright ridges and modulated by orientation and direct-light visibility.
- `query` scans object bounding spheres: O(number of objects) per query. A glass
  tap scans projected creatures: O(number of creatures) per tap. Neither runs every
  animation frame. Revision changes cause a record scan; only changed records upload.
- Audio uses prepared sample buffers. Ambient synthesis combines harmonics, filtered
  noise and damped bubble resonances; the tap uses damped glass resonances. AudioQueue
  callbacks copy loop segments rather than resynthesizing the sound.

CPU persistence does not imply persistence of the final image. The GPU still
transforms/rasterizes moving triangles, shades them, composes retained fixed surfaces
and resolves every animated frame. Fixed-surface material work and camera acquisition
now persist. Lowering MSAA, resolution or frame rate is a separate quality tradeoff.

## Incremental visibility and lighting to add

The fixed-layer acquisition pass now stores depth, object identity, world shading
position and lighting coefficients per covered sample. It does not sweep rays through
every object. Extending this to changing local screen regions is the next step.

A moved object invalidates its old and new projected footprints. Re-render those
regions against candidate geometry, including newly exposed background. Its influence
on light-to-receiver visibility can extend outside its own screen footprint, so shadow
receivers need a separate dependency/invalidation mechanism. Retain conservative
spatial memberships; rediscover candidates when bounds cross cells. Previously known
edges alone cannot discover a newly introduced occluder.

For direct soft shadows, either cached raster visibility or finite shadow-ray queries
can estimate the visible fraction of an area source. A shadow ray is a ray query, even
without a recursive path tracer. Neither strategy requires tracing arbitrary bounces.
A retained source-to-water-to-receiver caustic response is a possible next experiment;
call it physically derived only after actually implementing and validating that solve.

Moving the camera can invalidate most visibility. Animated caustics and water effects
can invalidate large areas even when geometry is fixed. Tile bookkeeping must beat the
GPU's full raster path in measured total cost. Keep the full-frame fallback and compare
image correctness, GPU work, CPU submissions, memory and compositor cost separately.

## Host boundary

The macOS host is C++ calling typed Objective-C runtime functions. AppKit owns the
window/event services, CAMetalLayer presents Metal results, and AudioQueue plays
original prepared PCM. No Swift, Objective-C++ translation unit or browser runs here.
All scene mutations happen on the main event context. Audio callbacks read their own
prepared sound and cursor; synchronous queue stop precedes reuse or disposal.

Normal desktop placement is below desktop icons and ignores input. Explicit tap
mode is above those icons and below ordinary apps. The separate control widget
turns taps off. Both panels reject keyboard focus, and all application keyboard
handlers and menu key equivalents are absent. There is no global
mouse hook. Main-display support is the current scope; display reconfiguration and
multi-display lifecycle need a dedicated follow-up.
