#include "renderer_verification.hpp"
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
namespace stillwater {
namespace {
struct Verification {
    Renderer& renderer;
    Scene& scene;
    std::filesystem::path directory;
    unsigned int failures{};
    unsigned int comparisons{};
    bool save_probes(const char* name) {
        const RenderStatistics& statistics = renderer.statistics();
        const std::filesystem::path path = directory / (std::string(name) + "-probes.json");
        std::ofstream output(path);
        output << std::setprecision(9) << "[\n";
        for (unsigned int row = 0; row < 8; ++row) {
            for (unsigned int column = 0; column < 12; ++column) {
                const unsigned int x = (2 * column + 1) * statistics.render_width / 24;
                const unsigned int y = (2 * row + 1) * statistics.render_height / 16;
                VisibilityProbe probe{};
                if (!renderer.query_fixed_visibility(x, y, probe))
                    return false;
                if (row != 0 || column != 0)
                    output << ",\n";
                output << "{\"pixel\":[" << x << ',' << y << "],\"samples\":[";
                for (std::size_t sample = 0; sample < probe.samples.size(); ++sample) {
                    const Float4& value = probe.samples[sample];
                    if (sample != 0)
                        output << ',';
                    output << '[' << value.x << ',' << value.y << ',' << value.z << ',' << value.w
                           << ']';
                }
                output << "],\"depths\":[" << probe.depths.x << ',' << probe.depths.y << ','
                       << probe.depths.z << ',' << probe.depths.w << "]}";
            }
        }
        output << "\n]\n";
        return output.good();
    }
    void check(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "Retained verification: " << message << '\n';
        }
    }
    bool compare(const char* name, double time, std::uint64_t builds) {
        renderer.set_retained(true);
        const std::string cached = (directory / (std::string(name) + "-retained.png")).string();
        if (!renderer.draw(scene, time, cached.c_str())) {
            check(false, renderer.error().c_str());
            return false;
        }
        check(renderer.statistics().visibility_builds == builds, "unexpected visibility rebuild");
        check(save_probes(name), "cannot save visibility probes");
        renderer.set_retained(false);
        const std::string full = (directory / (std::string(name) + "-full.png")).string();
        if (!renderer.draw(scene, time, full.c_str())) {
            check(false, renderer.error().c_str());
            return false;
        }
        ++comparisons;
        return true;
    }
};
} // namespace
bool verify_retained_renderer(Renderer& renderer, Scene& scene, const std::string& directory) {
    std::error_code filesystem_error{};
    std::filesystem::create_directories(directory, filesystem_error);
    if (filesystem_error)
        return false;
    Verification test{renderer, scene, directory};
    // The host has not entered its animation loop: all times and changes are explicit.
    renderer.resize(960, 600);
    VisibilityProbe probe{};
    test.check(!renderer.query_fixed_visibility(480, 590, probe), "unprepared query accepted");
    std::uint64_t builds = renderer.statistics().visibility_builds + 1;
    if (!test.compare("initial", 0, builds))
        return false;
    test.check(renderer.query_fixed_visibility(480, 590, probe), "cached visibility query failed");
    bool identified = false;
    for (const Float4& sample : probe.samples) {
        if (sample.w == 0)
            continue;
        test.check(std::isfinite(sample.x) && std::isfinite(sample.y) && std::isfinite(sample.z),
                   "non-finite cached world position");
        const std::uint32_t identity = static_cast<std::uint32_t>(sample.w);
        test.check(identity > 0 && identity <= scene.objects().size(),
                   "cached identity is not a scene object");
        identified = true;
    }
    test.check(identified, "floor probe should identify a retained surface");
    test.check(!renderer.query_fixed_visibility(960, 600, probe), "out-of-bounds query accepted");
    if (!test.compare("moving", 1, builds))
        return false;
    const std::uint64_t lights = renderer.statistics().light_visibility_builds;
    if (!test.compare("reuse-light", 1.04, builds))
        return false;
    test.check(renderer.statistics().light_visibility_builds == lights,
               "unchanged shadow map rebuilt receiver visibility");
    const Vec3 fish = project(actor_position(scene.actors()[7], 1.05), 1.6);
    const TapResult tap = scene.tap(fish.x, fish.y, 1.6, 1.05);
    test.check(tap.count > 0, "tap did not disturb creatures");
    if (!test.compare("tap", 1.05, builds))
        return false;
    test.check(renderer.statistics().light_visibility_builds == lights + 1,
               "tap did not refresh light visibility immediately");
    if (!test.compare("escape", 2.5, builds))
        return false;
    Object rock{};
    Object plant{};
    for (const Object& object : scene.objects()) {
        if (rock.identity == 0 && object.kind == ObjectKind::rock && object.radius > 1 &&
            object.center.z > 0)
            rock = object;
        if (plant.identity == 0 && scene.instances()[object.instance_index].behavior.x == 10)
            plant = object;
    }
    test.check(rock.identity != 0 && plant.identity != 0, "missing edit fixtures");
    const std::uint64_t fixed_shadows = renderer.statistics().static_shadow_builds;
    test.check(
        scene.move_object(rock.identity, {rock.center.x + 2, rock.center.y + 1, rock.center.z}),
        "rock mutation failed");
    if (!test.compare("rock-moved", 2.55, ++builds))
        return false;
    test.check(renderer.statistics().static_shadow_builds == fixed_shadows + 1,
               "rock edit left fixed shadow cache stale");
    test.check(scene.move_object(rock.identity, rock.center), "rock restoration failed");
    if (!test.compare("rock-restored", 2.6, ++builds))
        return false;
    test.check(
        scene.move_object(plant.identity, {plant.center.x + 1, plant.center.y, plant.center.z}),
        "plant mutation failed");
    if (!test.compare("plant-moved", 2.65, builds))
        return false;
    Matrix view = view_matrix();
    view.values[12] -= 0.8F;
    renderer.set_camera(view, {0.8F, 4.65F, 20.5F, 0});
    test.check(!renderer.query_fixed_visibility(480, 590, probe), "camera edit exposed stale query");
    if (!test.compare("camera-shift", 3, ++builds))
        return false;
    renderer.set_camera(view, {0.8F, 4.65F, 20.5F, 0});
    if (!test.compare("camera-unchanged", 3.05, builds))
        return false;
    test.check(renderer.set_light_intensity(0.55F), "valid light intensity rejected");
    if (!test.compare("light-dimmed", 3.1, builds))
        return false;
    test.check(!renderer.set_light_intensity(std::numeric_limits<float>::quiet_NaN()),
               "non-finite intensity accepted");
    test.check(renderer.set_light_intensity(0.55F), "unchanged intensity rejected");
    if (!test.compare("light-unchanged", 3.15, builds))
        return false;
    renderer.resize(813, 517);
    test.check(!renderer.query_fixed_visibility(400, 500, probe), "resize exposed stale query");
    if (!test.compare("resized", 3.2, ++builds))
        return false;
    // Exercise shadows advancing through the full path while the retained path is off.
    renderer.set_retained(false);
    test.check(renderer.draw(scene, 7), "full-path interlude failed");
    if (!test.compare("return-to-cache", 7, builds))
        return false;
    const std::uint64_t shadows_before_rewind = renderer.statistics().dynamic_shadow_frames;
    if (!test.compare("time-rewound", 0, builds))
        return false;
    test.check(renderer.statistics().dynamic_shadow_frames == shadows_before_rewind + 1,
               "time rewind reused future shadows");
    // Exercise the inverse camera with rotation, not only a translated default view.
    const float cosine = std::cos(0.08F), sine = std::sin(0.08F);
    const Matrix rotation{{cosine, 0, -sine, 0, 0, 1, 0, 0, sine, 0, cosine, 0, 0, 0, 0, 1}};
    renderer.set_camera(multiply(view_matrix(), rotation),
                        {-20.5F * sine, 4.65F, 20.5F * cosine, 0});
    if (!test.compare("camera-rotated", 0.4, ++builds))
        return false;
    renderer.set_camera(view_matrix(), {0, 4.65F, 20.5F, 0});
    if (!test.compare("camera-restored", 0.5, ++builds))
        return false;
    const Scene replacement{};
    test.check(!renderer.draw(replacement, 0), "different scene silently reused resident geometry");
    std::ofstream receipt(std::filesystem::path(directory) / "native-verification.json");
    receipt << "{\n  \"failures\": " << test.failures
            << ",\n  \"comparisons\": " << test.comparisons
            << ",\n  \"visibility_builds\": " << renderer.statistics().visibility_builds
            << ",\n  \"light_visibility_builds\": " << renderer.statistics().light_visibility_builds
            << ",\n  \"queried_identity\": " << probe.samples[0].w << "\n}\n";
    std::cout << "Native retained verification: " << test.comparisons << " image pairs, "
              << test.failures << " failures.\n";
    return test.failures == 0 && receipt.good();
}
} // namespace stillwater
