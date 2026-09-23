#pragma once
#include "apple_runtime.hpp"
#include "stillwater/scene.hpp"
#include <string>
namespace stillwater {
struct RenderStatistics {
    std::uint64_t frames{};
    std::uint64_t static_upload_bytes{};
    std::uint64_t actor_upload_bytes{};
    std::uint64_t instance_patch_bytes{};
    std::uint64_t static_shadow_builds{}, dynamic_shadow_frames{};
    double cpu_submit_seconds{};
    double gpu_seconds{};
    std::uint64_t gpu_timed_frames{};
    double gpu_shadow_frame_seconds{}, gpu_reuse_frame_seconds{};
    std::uint64_t gpu_shadow_frames{}, gpu_reuse_frames{};
};
class Renderer final {
  public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    bool initialize(id layer, const Scene& scene, const std::string& shader_path,
                    unsigned int samples);
    void resize(unsigned int width, unsigned int height);
    bool draw(const Scene& scene, double time, const char* capture_path = nullptr);
    const RenderStatistics& statistics() const {
        return statistics_;
    }
    const std::string& error() const {
        return error_;
    }

  private:
    id load_material(const std::string& path, bool srgb);
    void upload_actors(const Scene& scene);
    void upload_edits(id command, const Scene& scene);
    id make_texture(unsigned long format, unsigned int width, unsigned int height,
                    unsigned long usage, unsigned long samples = 1);
    enum class DrawSet { all, fixed, moving };
    void encode_geometry(id encoder, const Scene& scene, id pipeline, const void* uniforms,
                         unsigned long uniform_size, DrawSet draw_set = DrawSet::all);
    id layer_{nil}, device_{nil}, queue_{nil}, pipeline_{nil}, shadow_pipeline_{nil},
        background_pipeline_{nil}, depth_state_{nil};
    id vertices_{nil}, indices_{nil}, instances_{nil}, actors_{nil}, depth_{nil}, shadow_{nil},
        color_{nil};
    std::array<id, 6> materials_{};
    id fixed_shadow_{nil};
    bool fixed_shadow_ready_{};
    std::uint64_t shadow_instance_edits_{}, shadow_revision_{};
    double shadow_time_{-1};
    id previous_command_{nil};
    bool previous_updated_shadows_{};
    unsigned int samples_{4};
    unsigned int width_{}, height_{};
    std::uint64_t revision_{};
    RenderStatistics statistics_{};
    std::string error_{};
};
} // namespace stillwater
