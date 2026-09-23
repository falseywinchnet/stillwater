# BFFT renderer review

The existing local `/Users/joshuahkuttenkuler/code/bfft` checkout predates the renderer.
The investigation therefore read the remote photonic-transport experiment at commit
`6e64386f2da658a90493733c08b01dea6425ecdd`, specifically:

- `experiments/photonic_transport_field/README.md`
- `experiments/photonic_transport_field/RETAINED_SCENE_UPDATES.md`
- `experiments/photonic_transport_field/VIEWER_INCREMENTAL_FIELD.md`
- `experiments/photonic_transport_field/city_rain/README.md`

The useful architectural ideas are persistent scene/source state, stable primitive
identities, BVH refitting, and correcting retained source responses when objects move.
These fit the aquarium's dependency model. They do not require adopting the entire
transport solver or changing its authoritative repository.

The reviewed report lists integrated retained geometry/light work around 333–395 ms
per 800×600 frame. That is fast for the computation it performs; it is not a low-CPU
continuous wallpaper solution at 24 fps. The city/rain playback freezes optical
responses and avoids new scene evaluation, but the reviewed field was about 3.105 GiB
with roughly 7 ms of CPU playback work per frame. Those are upstream reported results,
not measurements reproduced on this laptop, and not evidence for arbitrary moving
fish, camera edits, or unchanged optical dependencies.

Stillwater currently has no BFFT dependency. Retain its source-response and update
ideas as candidates for selective direct-light/caustic experiments. Establish the
correctness and cost of each imported method against the raster baseline before
coupling it to the aquarium.
