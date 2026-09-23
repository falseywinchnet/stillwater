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
reflection. Leaves have procedural midribs, veins, edge variation and transmission;
their fine vein pattern currently modifies color, not surface relief.

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

## Further material work

The current leaf trial is fine surface grain on resolved broad foreground blades,
described below. The combined relief and pigment treatment was rejected.
For fish, preserve directional scale reflection and fin transparency at the small
desktop footprint; sharper painted scale outlines alone would look artificial.

Wood and rock also have matching roughness maps available. Using them meaningfully
requires a reflected-light term in the fixed material path, which currently shades
these surfaces diffusely. Their unused diffuse alpha channels could carry another
scalar without another texture, but its lighting and retained representation need
to be designed and measured. Increasing all textures to 2K would quadruple their
texel storage and would not address those missing material responses.


## Rejected leaf-material trial

The owner judged the leaf relief and pigment trial in commit `3ef5a7e` visually
worse. It was reverted to the leaf material in `47c2739`; the wood/rock cavity
maps remain. Passing image-consistency tests and a bounded performance cost did
not establish an aesthetic improvement. The trial changed normal relief, pigment
and fine-detail filtering together, so the cause of the regression is not isolated.
Any future leaf trial should compare one change at a time against this accepted
baseline rather than reintroducing the combined treatment.

The rollback audit also found that the trial's preview captures had mismatched
heights (1180×642 before, 1180×639 after). Its side-by-side crops and whole-image
difference are not controlled A/B evidence. The restored 1180×639 capture matches
the accepted wood/rock baseline at the same dimensions pixel for pixel. Future
visual comparisons must assert matching capture dimensions before rendering them.

## Leaf microtexture

The owner identified fine surface noise as the next direction and asked to work
on the large nearby leaves first. The initial whole-leaf 2K sampling trial was
visually ineffective: most of its grain averaged away at the native pixel budget.
The revised trial placed a visibly scaled grain patch on broad, resolved blades.
The owner accepted that appearance. The same mapping now extends at half strength
to other resolved leaves, keeping the accepted broadleaf treatment unchanged.
It does not reintroduce the rejected normal relief, color shifts or vein filtering.

`src/leaf_texture.cpp` builds a deterministic 2048×2048 scalar texture once during
initialization. It mixes 35% independent fine noise, 40% periodic noise on a
four-texel grid and 25% on a sixteen-texel grid. There are no coarse pigment bands,
photographic inputs, external assets or imagegen content. Integer hashing and
wrapped lattice coordinates make the source reproducible and tileable. Values
center on byte 128. Core checks cover extent, determinism, mean, variance, boundary
continuity statistics and suppression of large-scale blotches through averaging.

The GPU stores R8Unorm with a complete mip chain, trilinear filtering and up to 8×
anisotropy. The leaf UV extent uses 0.08×0.12 of the tile, offset by stable object
identity. This samples microscopic variation at a scale that remains visible on
the foreground blades rather than mapping the entire 2K tile onto a small leaf.

The multiplier is `1 + 0.8 * (0.5 + 0.5 * broad) * width_fade * (grain - 128/255)`.
`broad` fades from 1 to 0 as the existing tissue-thinness parameter goes from 0.5
to 0.7; `width_fade` goes from 1 to 0 as the horizontal UV footprint goes from
0.035 to 0.08 per pixel. This favors broad, resolved surfaces without inventing
object identities or relying on a fixed camera-distance cutoff. It preserves hue,
the existing color pattern, surface normals, silhouette, coverage and motion.
Grain is stationary in leaf coordinates and has no time-dependent random input.

The menu-bar checkbox **Leaf surface grain** allows direct comparison; it
redraws immediately while paused, at the same scene time. `--no-leaf-grain` is
available for reproducible captures. Turning it off bypasses the visible effect,
but does not free the resident texture. Very thin or distant foliage whose width cannot resolve the grain still fades to
the unchanged base material.

The texture adds 5,783,552 measured GPU bytes (5.52 MiB). There is one additional
filtered leaf texture sample and no new framebuffer, render pass or geometry.
Generation adds temporary CPU storage/work at startup. The current render policy
still uses 1408×881 pixels on this display: a 2K material texture is not a higher
resolution scene render, and subpixel detail must still average away.

The native suite now includes 19 retained/full pairs at both 2× and 4× MSAA,
including disabling and restoring grain at one exact time without rebuilding
the fixed camera cache. The image check requires a visible toggle difference and
pixel-exact restoration. Generic/specialized foliage is also compared at both
sample counts. Evidence and measurements are in `docs/evidence/neo-leaf-grain.json`.
These checks establish rendering behavior, not aesthetic acceptance.
