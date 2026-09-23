# Stillwater

A rich, interactive desktop aquarium with very low recurring CPU work.

- Follow docs/PROGRAMMING_HOUSE_STYLE.md. C++20, explicit types, named functions,
  visible ownership, dot access, no lambdas/auto/coroutines/ranges pipelines.
- All application source is C++. Keep Apple's Objective-C runtime calls inside
  the typed macOS host boundary. No Swift, SwiftUI, CloudKit or browser runtime.
- Retain queryable 3D objects and prepared GPU resources. No CPU scene rebuild
  per frame. Submit raster work while animation is active. Measure total cost.
- Aquarium logic, trajectories and audio generation remain portable C++.
- Do not add GUI.Forms or BFFT as dependencies without a demonstrated need.
- This is a separate product. Do not modify Paint, GUI.Forms or BFFT.
- Never claim zero power from zero application CPU. Report app CPU, memory and
  compositor measurements separately. Pause on sleep and explicit suspension.
- Preserve desktop access. Desktop interactions must not require hidden global
  input monitoring. The aquarium must not capture keyboard
  focus or implement keyboard shortcuts. Every action, including disabling desktop
  taps and quitting, must remain available through the menu-bar dropdown. Do not
  add a floating control strip over other applications.
- No imagegen source objects, textures, background plates or sprites. Sand,
  rocks, plants and animals must be actual programmatically built 3D geometry.
  Any generated reference may guide composition only, never ship as content.
- Retain geometry, visibility and lighting dependencies. Changes invalidate
  old/new projected bounds, disocclusions and affected shadow receivers. A full
  raster fallback must be named honestly, never described as sparse tile repair.
- Direct-light shadow queries are admitted. Caustic approximations must be
  labeled; no claim of physically solved optical transport from an image effect.
- Test behavioral boundaries and sound/sample safety; inspect the native app.

## MacBook Neo continuation

- Work from this repository's root; read `docs/NEO_HANDOFF.md` and
  `docs/METAL_STORAGE.md` before further optimization. The handoff's absolute
  paths are historical provenance, not required locations for a clone.
- The target is an A18 Pro with 8 GiB unified memory. Measure on this device;
  do not substitute the M4 timings or reduce quality without naming that tradeoff.
- Keep per-sample raster depth distinct from the shading-center distance and its
  interpolation correction. Removing the correction fails an existing 2× MSAA
  image tolerance at a shadow boundary.
- Only pass-local color/depth attachments may be memoryless. Camera retention,
  light visibility and shadow maps need storage across render passes.
- Run `ctest --test-dir build --output-on-failure`, native `--verify-retained` at
  both `--msaa 2` and `--msaa 4`, and `tools/compare_retained.py` after renderer
  edits. Use `tools/compare_storage.py` against the saved original renderer when
  changing the visibility representation. Preserve object identities and depth.

## CONV coverage trial

- Read `docs/CONV_AA.md` before changing `--aa conv-fast`. Its single-sample
  boundary overlay is not the sibling's exact four-primitive visibility solver.
- Preserve deterministic GPU compaction order; alpha composition cannot depend
  on atomic append scheduling. Never silently truncate boundary candidates.
- Static prepared vertices/lists invalidate with camera, dimensions or fixed
  instance edits. Moving vertices update once per frame. Include all GPU cache
  and topology storage in measurements.
- Preserve the normal 4× MSAA default until overlap, material opacity and motion
  quality are accepted. Use opaque RGB PNG capture to match the opaque layer.

## Camera registry

- Read `docs/CAMERA_REGISTRY.md`. Compact camera registration is the default for
  2×/4× MSAA; `--dense-camera` retains the preceding reference representation.
- Preserve all seven words of each registered surface and every raster depth.
  Match complete records, never object identity alone. Keep sample selectors.
- Acquire/classify/pack only on camera-view invalidation. Count readback and exact
  allocation happen at that boundary, never during steady animation.
- `--verify-retained` checks every reconstructed sample before acquisition
  textures are released. Preserve this independent inverse check and the image,
  identity, world-coordinate and depth gates at both sample counts.
- This is camera-local storage, not analytic CONV coverage or sparse tile repair.
  A future geometric registry must conservatively include unsampled thin features.

## Render-cost specialization

- The default foliage pipeline specializes the imported Riverscape leaf material;
  preserve its geometry, animation and opacity formulas. `--generic-foliage`
  selects the previous general shader. Read `docs/RENDER_COSTS.md`.
- `--record-shading` is optional: fewer framebuffers did not consistently mean
  lower GPU time. Keep its light-cache invalidation and pre-resolve RGB8 contract.
- Original triangle order is deliberate. A front-to-back experiment changed
  sparse pixels beyond tolerance and was rejected; do not silently reintroduce it.
- Run native 2×/4× tests and compare shader changes with `tools/compare_renderer.py`.
  Continue using the stricter `compare_storage.py` for storage-only changes.

## Standalone public repository

- This repository is `falseywinchnet/stillwater`; `paymenottowork` is not a source
  dependency. Build from the checked-in assets with no sibling checkout.
- Original code is MIT; keep the upstream and CC0 notices in `THIRD_PARTY.md`.
- `tools/check_assets.py` verifies `assets/SHA256SUMS`; regenerate checksums after
  intentional resource edits. Do not commit build directories, app bundles or
  temporary browser profiles. Keep reproducible measurements under docs/evidence.
