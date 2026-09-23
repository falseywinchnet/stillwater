# Aquarium materials

The recorded entropy color tuning is the selected direction: decorrelation 0.09,
allocation 0.11, gain cap 16 and overall strength 1. The separate entropy contrast
stage was rejected after comparison. Neither color LUT nor contrast postprocessing
is currently integrated into the native renderer; the color comparisons remain
offline studies.

## Existing detail and the first enrichment

Sand, rocks and wood use matching 1024×1024 Poly Haven diffuse and OpenGL normal
maps. The diffuse maps are sampled as sRGB, and normals as linear data. Trilinear
mip filtering and up to 8× anisotropy are already enabled. These are six GPU
textures with measured allocation of 33,914,880 bytes on the Neo.

Fish and leaves do not have bitmap skins to upscale. Fish have procedural pigment,
scale relief, scale roughness, gill and fin details, plus an approximate environment
reflection. Leaves have procedural midribs, veins, edge variation and transmission.
The leaf follow-up below adds bounded normal relief and smooth pigment variation.

The first material enrichment adds the matching source ambient-occlusion maps to
rock and wood. They describe small cavities in those material scans. The existing
diffuse/normal files match the provider's 1K MD5 checksums exactly, establishing that
these AO maps belong to the same source sets and UV layouts.

The normal-map alpha channel was unused. After decoding both opaque images with
the same orientation, the loader copies the AO red channel into normal alpha,
then generates the existing mip chain. Packing happens after image decoding so
alpha cannot premultiply and corrupt the normal's RGB values. Missing maps or
mismatched dimensions fail material loading. Sand retains alpha 1.

Ambient access is `mix(1, AO, 0.45)`. It multiplies only the existing ambient/fill
lighting term, before water attenuation. Direct lighting, moving depth shadows,
caustics, base color and normal strength are unchanged. This is texture-scale
ambient occlusion, not screen-space AO or a solution for light transport through
the whole tank. Moss inherits the underlying surface's cavity attenuation.

The result is deliberately modest: shaded grain and pores gain definition without
an image-wide contrast curve. It does not improve fish skin or leaf anatomy.

## Cost and verification

- Two new JPEG inputs total 924,950 bytes on disk. Decoding adds temporary startup
  work; the auxiliary RGBA buffer is released before texture upload.
- The number, dimensions, formats and allocation of GPU material textures are
  unchanged: 33,914,880 bytes before and after. No extra material texture sample,
  render pass or framebuffer is introduced.
- AO is folded into the existing retained ambient coefficient during camera
  acquisition. The steady retained restoration shader is unchanged.
- Release core tests and 17 retained/full native comparisons pass at both 2× and
  4× MSAA. Generic/specialized foliage agreement is checked at both sample counts.
- Replacing both AO inputs with white reproduces the previous native screenshot
  pixel for pixel, checking that the decoder refactor and alpha packing preserve
  the old normal/color path. An 8×8 AO fixture against a 1024×1024 normal is rejected.

Evidence is in `docs/evidence/neo-material-cavities.json`. Captures are at exact
animation time 8 seconds in a 1180×639 preview. Total camera-record allocation can
change because altered half-float lighting values change record deduplication;
this is not evidence of a generally faster renderer. No steady-state GPU-time or
power improvement is claimed.

## Leaf relief and pigment variation

The midrib and side-vein pattern now perturb the shading normal. Their authored
height amplitudes are 0.0012 and 0.00018 scene units. This follows the upstream
Riverscape leaf-relief idea but uses analytic UV height gradients, explicit
resolution fades and a bounded world-space slope. There is no displacement of
geometry: outlines, leaf motion, opacity, shadows and bubble attachment stay put.

The normal tilt is limited to atan(0.18), about 10.2 degrees. The height gradient
reverses on the underside so the two sides follow the same thin blade. A degenerate
surface derivative returns the original normal. Analytic gradients avoid taking
screen derivatives of the fade itself, which would introduce artificial bumps
where detail changes resolution.

The narrow side-vein profile fades between screen footprints of 0.08 and 0.24
cycles per pixel. Its normal relief tends to zero; its color tends to the exact
profile mean, 0.139949934, rather than disappearing into a brightness step. The
midrib retains its existing width-dependent fade. This also replaces the old
unfiltered high-frequency painted vein/mottling pattern.

Pigment varies smoothly in stable leaf UV coordinates with a deterministic object
phase, on top of existing vertex colors. Peak channel multipliers are ±5.5% red,
±2.5% green and ∓2.5% blue. Broadleaf variations therefore remain small warm/cool
green shifts. The pattern has no time or world-position input and moves with the
leaf. It fades at small screen footprints. Existing underside tint remains.

There are no new textures, vertex attributes, framebuffers or draw calls. Unlike
fixed wood/rock AO, animated leaves are shaded each frame, so the extra arithmetic
has a recurring cost. Matched Neo measurements and source hashes are retained in
`docs/evidence/neo-leaf-materials.json`; do not describe this change as free.

Release core tests, retained/full rendering at 2× and 4×, and generic/specialized
foliage comparisons pass. An independent central-difference check of the authored
height function agrees with 1,000 analytic-gradient samples to within 1e-8.
The normal and pigment changes do not alter the queried geometry or raster depth.
These checks and the resolution fades do not certify absence of shimmer under
every camera angle, display scale or animation state.

## Further material work

For fish, preserve directional scale reflection and fin transparency at the small
desktop footprint; sharper painted scale outlines alone would look artificial.

Wood and rock also have matching roughness maps available. Using them meaningfully
requires a reflected-light term in the fixed material path, which currently shades
these surfaces diffusely. Their unused diffuse alpha channels could carry another
scalar without another texture, but its lighting and retained representation need
to be designed and measured. Increasing all textures to 2K would quadruple their
texel storage and would not address those missing material responses.
