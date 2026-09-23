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
