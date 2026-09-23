# Metal storage on the MacBook Neo

Stillwater now uses a smaller retained position representation and memoryless
main-pass attachments on Apple-family GPUs. The Neo continuation preserves the
scene, internal image dimensions, 24 fps scheduling, four-sample antialiasing,
material coefficients and shadow-update schedule.

The authoritative project is `/Users/ultimussecundai/stillwater`; the original
M4 source and context are recorded in [NEO_HANDOFF.md](NEO_HANDOFF.md).

## Representation

| Per-sample resource | Original | Current |
| --- | ---: | ---: |
| Base light and direct weight, RGBA16F | 8 bytes | 8 bytes |
| Attenuated albedo and caustic weight, RGBA16F | 8 bytes | 8 bytes |
| World XYZ and object identity, RGBA32F | 16 bytes | — |
| Linear camera distance and identity, RG32F | — | 8 bytes |
| Shading-center correction, RG16F | — | 4 bytes |
| Raster depth, depth32F | 4 bytes | 4 bytes |
| Receiver visibility, R16F | 2 bytes | 2 bytes |
| **Retained total** | **38 bytes** | **34 bytes** |
| Temporary main-pass color and depth backing | 8 bytes | **0 on Apple GPUs** |

For a shading position `p`, camera acquisition stores the camera-space distance
`d = (projection * view * p).w`. It also stores the difference between the
reprojected shading center and the ideal pixel-center ray in normalized device
coordinates. Reconstruction applies the inverse view and the projection's X/Y
scales to `((ndc + correction) * d, -d, 1)`.

The camera inverse is prepared only when camera state or image dimensions change.
The full redraw path does not read these caches or use reconstruction.

Two precision distinctions matter:

- MSAA raster depth describes each coverage sample. Material shading uses the
  pixel center. Unprojecting sample depth would move the retained shadow receiver
  and caustic evaluation point, especially at polygon edges.
- Even a center-interpolated world position is not perfectly on the ideal ray
  after floating-point raster interpolation. The first eight-byte candidate
  omitted this correction and failed the original two-sample image limit at one
  hard-shadow edge (9/255 against a limit of 8/255). That candidate is not the
  shipped implementation. Adding the four-byte correction restores the original
  image tolerance and greatly improves world-query agreement.

Object identity remains a float32 with the original identity range. Both
lighting coefficient textures and the independent depth32F values are unchanged.
The position representation is numerically close to the original; it is not
claimed bitwise identical.

The main color attachment resolves to the drawable in the same render pass;
main depth is discarded. Neither requires storage after that pass. Apple-family
GPUs therefore use memoryless tile storage for those two targets. Camera cache,
receiver visibility and shadow maps keep private backing because later passes
read them. A GPU-family check preserves private main targets on other GPUs;
that fallback was not exercised on the Neo. This follows Apple's
[memoryless storage contract](https://developer.apple.com/documentation/metal/mtlstoragemode/memoryless).

## Verification

Both 2× and 4× MSAA pass 17 exact-time native image comparisons, cache build/reuse
assertions, and fixed-surface queries. Cases include animation, tap/escape,
rock and plant edits, translation and rotation of the camera, camera restoration,
light-intensity edits, odd-size resize, returning from full redraw and time rewind.
Unprepared queries, stale queries after camera/size changes, out-of-bounds queries,
and replacement scene storage are rejected by the existing boundaries/tests.

The compact renderer is compared both with its independent full redraw and with
an original-renderer build from commit `4a80ee3` carrying only the expanded
verification harness. At 96 probe locations in each of the 17 scenes, all queried
identities and raster depths match the original exactly. The maximum observed
world-coordinate component difference is 0.00000382 scene units. The comparison
requires less than 0.00002 and separately checks finite values, absent samples
and background zeros.

Full-redraw images match the original pixel for pixel. Retained versus full
redraw has a maximum RGB channel difference of 5/255 at either sample count;
the largest mean channel difference across the two suites is approximately
0.001513/255. The existing acceptance limits remain mean below 0.01/255 and
maximum at most 8/255. These are comparisons for the tested scenes, not proof
for all possible geometry or camera transforms.

Release and ASan/UBSan core tests pass. The native four-sample 17-case harness also
passes ASan/UBSan with Metal API validation. Leak detection is disabled for the
native sanitizer run, matching the earlier verification practice. These checks
do not certify third-party frameworks as leak-free.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j2
ctest --test-dir build --output-on-failure
MTL_DEBUG_LAYER=1 ./build/Stillwater.app/Contents/MacOS/Stillwater --preview --muted --msaa 4 --verify-retained artifacts/verify-4x
python3 tools/compare_retained.py artifacts/verify-4x
MTL_DEBUG_LAYER=1 ./build/Stillwater.app/Contents/MacOS/Stillwater --preview --muted --msaa 2 --verify-retained artifacts/verify-2x
python3 tools/compare_retained.py artifacts/verify-2x
python3 tools/compare_storage.py artifacts/neo-baseline/verification artifacts/verify-4x
python3 tools/compare_storage.py artifacts/neo-baseline/verification-2x artifacts/verify-2x
python3 tools/measure_storage.py --baseline artifacts/neo-baseline/Stillwater.app --seconds 20
```

Original and current image/query captures remain under `artifacts/neo-baseline/`
and `artifacts/neo-final/`. The tracked evidence receipt preserves comparisons,
measurements and hashes without checking the large PNG sets into Git.

## On-device measurements

The Neo uses an A18 Pro and 8 GiB unified memory. Four sequential 20-second CPU
sampling windows used original, compact, compact, original ordering at the same
1408×881 desktop dimensions, 24 fps, 4× MSAA and muted audio. Each process had a
four-second initial wait. Other applications remained active.

| Run | Metal allocation | GPU ms/timed frame | App CPU, one core | Timed/submitted frames |
| --- | ---: | ---: | ---: | ---: |
| baseline-before | 380.44 MiB | 21.98 | 2.45% | 590/640 |
| compact-first | 322.44 MiB | 18.94 | 5.44% | 580/584 |
| compact-second | 322.44 MiB | 16.80 | 2.25% | 643/647 |
| baseline-after | 380.44 MiB | 19.34 | 2.50% | 646/647 |

The repeatable result is **58.0 MiB less Metal allocation**, from 380.44 to
322.44 MiB: **15.25% lower**. The retained cache's logical size falls from
179.81 to 160.89 MiB. Temporary main-pass backing falls from 37.86 MiB to zero;
the remainder of the measured saving is allocation padding/overhead. Memoryless
tile working storage still exists, so zero backing does not mean zero GPU memory
or zero work.

GPU intervals were lower in these compact runs, but this is not a controlled
power or universal throughput result. CPU varied materially, including one 5.44%
compact sample; the runs do not establish a CPU reduction. The timing collector
omits the last command and commands not complete at its next observation. The
counts above expose that limitation, and warm process RSS is preserved separately
in the receipt. Do not sum process RSS and Metal allocation on unified memory.

[The evidence receipt](evidence/neo-metal-storage.json) contains complete timings,
resource counts, executable/resource/source hashes and the verification results.

## Antialiasing integration target

The owner identified the active [Investigate fast CONV antialiasing task](codex://threads/01a0cd6e-9c70-7731-ad34-87888a07ac6e).
Its local source is `/Users/ultimussecundai/bfft/experiments/conv_fast_aa/`.
As reviewed on 2026-09-23, the strongest result is analytic geometric boundary
coverage, not the rejected image-only filters. The sibling is refining visible
surface composition, thin-line behavior and Metal throughput. Do not treat the
older `FINDINGS.md` as the final state of that active work; read the new results
before importing an implementation.

The current single-sample equivalent of this 34-byte cache would be about
40.22 MiB at 1408×881, versus 160.89 MiB at four samples. The approximately
120.67 MiB difference is a storage opportunity, not a measured integration saving:
new boundary/visibility metadata and passes must be counted.

A future integration should retain the current 4× MSAA renderer as a selectable
reference and test these tank-specific requirements:

- Layered opaque rocks, plants and fish need visible-area composition; independent
  triangle areas cannot simply add through occlusion.
- Shared triangle edges must cancel without dark seams. Fine stems and subpixel
  leaf tips need motion tests, not just a flattering still capture.
- Leaf and fin alpha currently use alpha-to-coverage. The new path must preserve
  their partial coverage together with front/back leaf shading.
- Textured/normal-mapped materials, animated caustics and filtered shadows need
  varying-shading tests beyond constant-color triangles.
- Fixed surfaces must reappear correctly when moving objects uncover them.
  Cache queries and invalidation still need meaningful surface identities/depths.
- Measure complete native GPU time, CPU work and allocation on the A18 Pro,
  including conservative support geometry, overlap processing, metadata and any
  augmentation or resolve pass.

No BFFT source or dependency was changed, and the unfinished antialiasing method
has not replaced MSAA in this build. This storage work and its reference captures
remain useful when that integration is ready.


## Local interaction check

Native widget clicks verified pause/resume, window/desktop placement and the
Desktop taps toggle. The final app is left muted, in desktop placement with taps
off, so normal desktop clicks reach Finder. The automatic desktop tap smoke
produced one tap and changed nearby actor records without rebuilding geometry.
A prior preview smoke was occluded before its timed tap (zero taps); it is retained
in the receipt as an incomplete tap exercise rather than counted as a pass.
The paused desktop run made four startup/placement draws and no animation frames.
Neither panel reported accepting keyboard focus.
