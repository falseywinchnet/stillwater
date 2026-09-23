# Native prototype verification

Verified on Apple M4 / macOS 26.5 on 2026-09-23. This is a local development build.

- Release and ASan/UBSan builds completed; `scene_and_sound` passed in both.
  Checks cover retained identities, valid indices, local tap effects, continuity,
  one-record mutations, malformed-archive rejection without state loss, reload,
  individual imported plant identities, and finite/bounded sound samples.
- A seven-second native ASan/UBSan run rendered, played sound and exercised a tap.
  Four nearby creatures changed: 320 bytes beyond the initial 1440-byte actor
  upload. Geometry built once, static geometry stayed resident, and the fixed
  shadow map built once. No sanitizer error was reported. Leak detection was
  disabled; this is not a leak-free certification.
- Native UI clicks verified pause/resume, sound, desktop placement and tap mode.
  The separate top control panel remained reachable. Its Quit button exited the
  process after the native action returned. Both panels reported that they cannot
  become key windows and were not key windows. No keyboard handler or menu shortcut
  is registered. Desktop tap mode accepts mouse events above Finder icons;
  disabling it restores click-through desktop placement.
- The automated tap uses the same scene/audio response function, but does not
  replace an end-to-end physical desktop mouse test. Earlier native aquarium
  clicks reached the handler; a moving fish was not hit by those clicks. Exact
  triangle/occlusion picking is not implemented.
- Native GPU render-target captures were inspected for composition, mesh winding,
  texture appearance and optional quality tradeoffs. A deterministic time-zero
  comparison checked the closed-mesh back-face change; see PERFORMANCE.md.
- Offline geometry regeneration reproduced the archive and manifest SHA-256
  checksums. All 15 vendored upstream files matched their provenance manifest.
  Both application bundles matched every packaged asset hash.
- Invalid MSAA, scale, frame rate and non-finite timer arguments were rejected.
  C++ sources passed the repository's clang-format check.

Receipts: [native sanitizer](evidence/native-sanitizer.json),
[widget and desktop tap placement](evidence/widget-final.json),
[performance](evidence/performance.json), and
[quality comparison](evidence/gpu-comparison.json).

The prototype remains single-display. Display changes, multi-display placement,
reliable partial-window coverage scheduling, exact picking, obstacle-aware fish,
incremental screen-region repair, and a complete physical lighting model remain
open work. Native sleep notification handling exists but was not tested by putting
the user's computer to sleep. No battery-life or total-power result is claimed.
