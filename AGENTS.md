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
  input monitoring. The aquarium and control widget must not capture keyboard
  focus or implement keyboard shortcuts. Every action, including disabling desktop
  taps and quitting, must remain available through the top control widget.
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

- The authoritative working tree is `/Users/ultimussecundai/stillwater`; read
  `docs/NEO_HANDOFF.md` and `docs/METAL_STORAGE.md` before further optimization.
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
