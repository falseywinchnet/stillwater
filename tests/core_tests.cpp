#include "stillwater/scene.hpp"
#include "stillwater/sound.hpp"
#include "stillwater/timing.hpp"
#include "stillwater/leaf_texture.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <set>
#include <zlib.h>
namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void leaf_grain_contract() {
    const std::vector<std::uint8_t> pixels = stillwater::make_leaf_grain();
    const unsigned int size = stillwater::leaf_grain_size;
    require(pixels.size() == static_cast<std::size_t>(size) * size, "2K scalar leaf grain extent");
    require(pixels == stillwater::make_leaf_grain(), "leaf grain reproducible without frame state");
    double mean = 0, energy = 0, coarse_energy = 0, seam = 0, interior = 0;
    for (const std::uint8_t value : pixels) {
        mean += value;
        energy += (static_cast<double>(value) - 128) * (static_cast<double>(value) - 128);
    }
    mean /= static_cast<double>(pixels.size());
    energy /= static_cast<double>(pixels.size());
    require(std::abs(mean - 128) < 0.5, "leaf grain does not introduce a broad brightness shift");
    require(energy > 100 && energy < 2000, "leaf grain has bounded fine variation");
    for (unsigned int y = 0; y < size; y += 64) {
        for (unsigned int x = 0; x < size; x += 64) {
            double average = 0;
            for (unsigned int j = 0; j < 64; ++j)
                for (unsigned int i = 0; i < 64; ++i)
                    average += pixels[(y + j) * size + x + i];
            average = average / 4096 - 128;
            coarse_energy += average * average;
        }
    }
    coarse_energy /= static_cast<double>((size / 64) * (size / 64));
    require(coarse_energy < energy * 0.0625, "unresolved grain averages away instead of becoming blotches");
    for (unsigned int i = 0; i < size; ++i) {
        seam += std::abs(int(pixels[i * size]) - int(pixels[i * size + size - 1]));
        seam += std::abs(int(pixels[i]) - int(pixels[(size - 1) * size + i]));
        interior += std::abs(int(pixels[i * size + 512]) - int(pixels[i * size + 511]));
        interior += std::abs(int(pixels[512 * size + i]) - int(pixels[511 * size + i]));
    }
    require(seam > interior * 0.8 && seam < interior * 1.2,
            "wrapped texture boundary has ordinary neighboring-pixel variation");
}
void scene_contract() {
    stillwater::Scene scene{};
    require(scene.objects().size() > 1000, "scene contains a real populated habitat");
    std::set<std::uint32_t> identities{};
    std::size_t rocks = 0, plants = 0;
    for (const stillwater::Object& object : scene.objects()) {
        require(identities.insert(object.identity).second, "stable IDs are unique");
        require(object.instance_index < scene.instances().size(),
                "query object maps to render instance");
        if (object.kind == stillwater::ObjectKind::rock)
            ++rocks;
        if (object.kind == stillwater::ObjectKind::kelp ||
            object.kind == stillwater::ObjectKind::fern)
            ++plants;
    }
    require(rocks >= 400 && plants >= 800, "hundreds of separate rocks and plants");
    for (const std::uint32_t index : scene.indices())
        require(index < scene.vertices().size(), "all geometry indices are valid");
    std::size_t drawn = 0;
    for (const stillwater::Batch& batch : scene.batches()) {
        require(batch.mesh.first_index + batch.mesh.index_count <= scene.indices().size(),
                "batch index range");
        require(batch.first_instance + batch.instance_count <= scene.instances().size(),
                "batch instance range");
        drawn += batch.instance_count;
    }
    require(drawn == scene.instances().size(), "each retained instance occurs in one batch");
    const stillwater::Vertex* vertices = scene.vertices().data();
    const std::uint64_t revision = scene.statistics().revision;
    const stillwater::TapResult far = scene.tap(-100, -100, 1.6, 10);
    require(far.count == 0 && scene.statistics().revision == revision,
            "distant tap performs no mutation");
    const std::vector<stillwater::Actor> before = scene.actors();
    const stillwater::Vec3 at = stillwater::actor_position(before[7], 10);
    const stillwater::Vec3 screen = stillwater::project(at, 1.6);
    const stillwater::TapResult near = scene.tap(screen.x, screen.y, 1.6, 10);
    require(near.count > 0 && near.count < scene.actors().size(),
            "tap affects nearby creatures only");
    for (std::size_t index = 0; index < before.size(); ++index) {
        const bool affected =
            std::find(near.actors.begin(),
                      near.actors.begin() + static_cast<std::ptrdiff_t>(near.count),
                      index) != near.actors.begin() + static_cast<std::ptrdiff_t>(near.count);
        if (!affected)
            require(
                std::memcmp(&before[index], &scene.actors()[index], sizeof(stillwater::Actor)) == 0,
                "unaffected actor bytes conserved");
        else {
            const stillwater::Vec3 old = stillwater::actor_position(before[index], 10),
                                   now = stillwater::actor_position(scene.actors()[index], 10);
            require(std::hypot(old.x - now.x, old.y - now.y) < 1e-5,
                    "flight begins continuously at current pose");
            const stillwater::Actor& actor = scene.actors()[index];
            const double end = 10 + actor.escape.w;
            const stillwater::Vec3 last = stillwater::actor_position(actor, end - 1e-5),
                                   next = stillwater::actor_position(actor, end + 1e-5);
            require(std::hypot(last.x - next.x, last.y - next.y) < 1e-4,
                    "flight joins the new retained route continuously");
        }
    }
    require(scene.vertices().data() == vertices && scene.statistics().geometry_builds == 1,
            "interaction never rebuilds geometry");
    const std::vector<stillwater::Instance> before_move = scene.instances();
    const stillwater::Object moved = scene.objects()[1];
    require(scene.move_object(moved.identity, {9, 1, -4}), "retained rock accepts named mutation");
    require(!scene.move_object(0, {0, 0, 0}), "invalid identity rejected");
    std::size_t changed_instances = 0;
    for (std::size_t index = 0; index < before_move.size(); ++index) {
        if (std::memcmp(&before_move[index], &scene.instances()[index],
                        sizeof(stillwater::Instance)) != 0)
            ++changed_instances;
    }
    require(changed_instances == 1 && scene.statistics().instance_edits == 1,
            "one object edit changes only its retained render record");
    require(scene.instance_revisions()[moved.instance_index] == scene.statistics().revision,
            "renderer can identify exact changed record");
    require(scene.tap(std::numeric_limits<double>::quiet_NaN(), 0, 1.6, 0).count == 0,
            "nonfinite input rejected");
    require(!scene.query({{0, 0, 0}, {0, 0, 0}}, 0).found, "zero ray rejected");
    const stillwater::QueryHit hit = scene.query({{at.x, at.y, at.z + 4}, {0, 0, -1}}, 10);
    require(hit.found && hit.distance >= 0, "scene supports broad-phase spatial queries");
}
void pointer_contract() {
    stillwater::Scene scene{};
    const stillwater::Vec3 p = stillwater::actor_position(scene.actors()[8], 10);
    const stillwater::Vec3 screen = stillwater::project(p, 1.6);
    require(scene.pointer_motion(screen.x, screen.y, 1.6, 10, 0.01).count == 0,
            "slow or resting hand does not repeatedly frighten fish");
    const stillwater::TapResult response = scene.pointer_motion(screen.x, screen.y, 1.6, 10, 1);
    require(response.count > 0, "nearby fish respond to a moving hand");
    require(scene.pointer_motion(screen.x, screen.y, 1.6, 10.1, 1).count == 0,
            "pointer response has a cooldown instead of restarting flight every frame");
    require(scene.pointer_motion(screen.x, screen.y, 1.6, 11,
            std::numeric_limits<double>::quiet_NaN()).count == 0, "invalid pointer speed rejected");
    require(scene.statistics().geometry_builds == 1, "pointer response retains geometry");
}
void habitat_contract() {
    stillwater::Scene scene{};
    std::string error{};
    const std::string path = std::string(STILLWATER_ASSETS) + "/riverscape.swscene.gz";
    require(scene.load_habitat(path, error), "translated habitat loads in portable C++");
    require(scene.objects().size() > 7000 && scene.actors().size() == 18,
            "translated geometry retains objects and native creatures");
    std::size_t plants = 0;
    std::uint32_t plant_identity = 0;
    for (const stillwater::Object& object : scene.objects()) {
        require(object.identity > 0 && object.instance_index < scene.instances().size(),
                "imported object identity and instance are valid");
        if (object.kind == stillwater::ObjectKind::kelp) {
            ++plants;
            plant_identity = object.identity;
        }
    }
    require(plants > 500, "batched planting retains individual attachment roots");
    for (const std::uint32_t index : scene.indices())
        require(index < scene.vertices().size(), "imported triangle index is bounded");
    for (const stillwater::Vertex& vertex : scene.vertices()) {
        if (vertex.binding.y > 0.5F) {
            const std::size_t instance = static_cast<std::size_t>(vertex.binding.x);
            require(instance < scene.instances().size() &&
                        scene.instances()[instance].behavior.x == 10,
                    "each foliage vertex addresses its retained plant");
        }
    }
    std::size_t pearls = 0;
    for (const stillwater::Instance& pearl : scene.instances()) {
        if (pearl.behavior.x != 15)
            continue;
        ++pearls;
        const std::size_t leaf_index = static_cast<std::size_t>(pearl.anatomy.x);
        const std::size_t parent_index = static_cast<std::size_t>(pearl.anatomy.y);
        require(leaf_index < scene.vertices().size() && parent_index < scene.instances().size(),
                "leaf pearl references retained source geometry");
        const stillwater::Vertex& leaf = scene.vertices()[leaf_index];
        stillwater::Instance parent = scene.instances()[parent_index];
        require(parent.behavior.x == 10, "pearl attaches to a real plant");
        require(std::abs(leaf.normal.y) >= 0.94F, "pearls collect only on near-horizontal leaf surfaces");
        const stillwater::Matrix original = stillwater::leaf_bubble_transform(pearl, leaf, parent, 8);
        parent.transform.values[12] += 2;
        const stillwater::Matrix translated = stillwater::leaf_bubble_transform(pearl, leaf, parent, 8);
        require(std::abs(translated.values[12] - original.values[12] - 2) < 1e-5F,
                "parent edits carry attached bubbles without rebuilding geometry");
        for (const float value : original.values)
            require(std::isfinite(value), "pearl deformation stays finite");
        const double release = pearl.anatomy.w - 6 - pearl.behavior.y + pearl.anatomy.w;
        const stillwater::Matrix just_before = stillwater::leaf_bubble_transform(pearl, leaf, parent, release - 1e-5);
        const stillwater::Matrix just_after = stillwater::leaf_bubble_transform(pearl, leaf, parent, release + 1e-5);
        require(std::abs(just_before.values[13] - just_after.values[13]) < 0.0001F,
                "leaf attachment joins a rising bubble continuously");
    }
    stillwater::Vertex flat{};
    flat.normal = {0, 1, 0, 0};
    flat.position = {0, 2, 0, 1};
    stillwater::Instance support{};
    support.transform.values = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    stillwater::Instance trapped{};
    trapped.anatomy = {0, 0, 0.02F, 36};
    require(stillwater::leaf_bubble_supported(flat, support), "horizontal surface can trap a bubble");
    const stillwater::Matrix under = stillwater::leaf_bubble_transform(trapped, flat, support, 0);
    require(under.values[13] < 2 && under.values[12] == 0 && under.values[14] == 0,
            "attached bubble sits directly below the horizontal leaf, without an edge offset");
    flat.normal = {1, 0, 0, 0};
    require(!stillwater::leaf_bubble_supported(flat, support), "vertical blade cannot trap a bubble");
    const stillwater::Matrix vertical = stillwater::leaf_bubble_transform(trapped, flat, support, 0);
    require(vertical.values[0] <= 0.000011F, "vertical animated support cannot retain a visible pearl");
    flat.normal = {0, 1, 0, 0};
    support.transform.values = {0,1,0,0, -1,0,0,0, 0,0,1,0, 0,0,0,1};
    require(!stillwater::leaf_bubble_supported(flat, support), "parent rotation controls world-space trapping eligibility");
    require(pearls == 1728, "leaf pearling triples the 576-instance budget");
    const std::vector<stillwater::Instance> before = scene.instances();
    const stillwater::Vertex* vertices = scene.vertices().data();
    const stillwater::Vec3 destination{1, 2, 3};
    require(scene.move_object(plant_identity, destination),
            "translated plant accepts a local edit");
    const stillwater::Object& plant = scene.objects()[plant_identity - 1];
    require(std::abs(plant.center.x - 1) < 1e-8 && std::abs(plant.center.y - 2) < 1e-8 &&
                std::abs(plant.center.z - 3) < 1e-8,
            "world-space edit preserves query/render agreement");
    std::size_t changed = 0;
    for (std::size_t index = 0; index < before.size(); ++index) {
        if (std::memcmp(&before[index], &scene.instances()[index], sizeof(stillwater::Instance)) !=
            0)
            ++changed;
    }
    require(changed == 1 && scene.vertices().data() == vertices,
            "moving an imported plant patches one record without touching geometry");
    const std::filesystem::path corrupt_path =
        std::filesystem::temp_directory_path() / "stillwater-invalid-habitat.gz";
    // A valid gzip containing a hostile decompressed allocation count must fail before allocation.
    std::array<unsigned char, 32> bad{};
    std::memcpy(bad.data(), "STWSCN1", 7);
    std::fill(bad.begin() + 8, bad.begin() + 12, static_cast<unsigned char>(255));
    gzFile corrupt = gzopen(corrupt_path.string().c_str(), "wb");
    require(corrupt != nullptr, "create temporary malformed habitat");
    require(gzwrite(corrupt, bad.data(), static_cast<unsigned int>(bad.size())) ==
                static_cast<int>(bad.size()),
            "write malformed header");
    gzclose(corrupt);
    require(!scene.load_habitat(corrupt_path.string(), error), "oversized archive rejected");
    require(scene.vertices().data() == vertices && scene.statistics().instance_edits == 1,
            "failed replacement conserves the existing scene");
    std::filesystem::remove(corrupt_path);
    require(scene.load_habitat(path, error), "repeated loading replaces a scene safely");
    require(scene.objects().size() == before.size(), "reload does not accumulate native extras");
}
void shadow_cadence_contract() {
    for (const int fps : {24, 30, 60}) {
        double previous = 0;
        unsigned int refreshes = 0;
        for (int frame = 1; frame <= fps * 10; ++frame) {
            const double time = static_cast<double>(frame) / fps;
            if (stillwater::shadow_refresh_due(time, previous)) {
                previous = time;
                ++refreshes;
            }
        }
        require(refreshes == 160, "shadow cadence averages 16 Hz without frame quantization loss");
    }
    require(stillwater::shadow_refresh_due(0, 4), "time rewind invalidates shadow schedule");
    require(!stillwater::shadow_refresh_due(1, 1), "same-time redraw reuses shadows");
}
void sound_contract() {
    const stillwater::Sound ambience = stillwater::make_ambience();
    const stillwater::Sound left = stillwater::make_glass_tap(-1),
                            right = stillwater::make_glass_tap(1);
    double energy = 0;
    for (const float sample : ambience.stereo) {
        require(std::isfinite(sample) && std::abs(sample) < 0.5F,
                "ambient samples finite and bounded");
        energy += sample * sample;
    }
    require(energy > 1, "ambient sound has signal");
    require(std::abs(ambience.stereo[0] - ambience.stereo[ambience.stereo.size() - 2]) < 1e-7F,
            "preview export fades at both ends");
    stillwater::Ambience whole{}, blocked{};
    std::vector<float> reference(22050U * 4U), chunks(reference.size());
    whole.render(reference);
    for (std::size_t offset = 0; offset < chunks.size();) {
        const std::size_t count = std::min<std::size_t>(514, chunks.size() - offset);
        blocked.render(std::span<float>(chunks.data() + offset, count));
        offset += count;
    }
    require(reference == chunks, "continuous audio is independent of callback block boundaries");
    const double rms = std::sqrt(energy / static_cast<double>(ambience.stereo.size()));
    require(rms > 0.001 && rms < 0.015, "ambient level remains subdued");
    double repeat_error = 0;
    for (std::size_t index = 22050U * 2U; index < 22050U * 4U; ++index) {
        const double difference = ambience.stereo[index] - ambience.stereo[index + 16U * 22050U * 2U];
        repeat_error += difference * difference;
    }
    require(repeat_error > 0.01, "water no longer repeats the old sixteen-second phrase");
    for (std::size_t index = 0; index < left.stereo.size(); index += 2) {
        require(left.stereo[index + 1] == 0 && right.stereo[index] == 0,
                "pan isolates selected channel");
        require(left.stereo[index] == right.stereo[index + 1], "pan preserves tap waveform");
        require(std::isfinite(left.stereo[index]) && std::abs(left.stereo[index]) < 0.025,
                "tap stays close to ambience instead of a loud metallic knock");
    }
    require(left.stereo.front() == 0 && left.stereo[left.stereo.size() - 2] == 0,
            "tap endpoints are silent");
}
} // namespace
int main() {
    static_assert(sizeof(stillwater::Vertex) == 128);
    static_assert(sizeof(stillwater::Instance) == 112);
    static_assert(sizeof(stillwater::Actor) == 80);
    leaf_grain_contract();
    scene_contract();
    pointer_contract();
    habitat_contract();
    shadow_cadence_contract();
    sound_contract();
    std::cout << "Retained scene, local interaction, geometry bounds and audio contracts passed.\n";
}
