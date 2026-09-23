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
    std::uint64_t visibility_builds{}, visibility_reuses{}, retained_bytes{};
    std::uint64_t transient_backing_bytes{};
    bool memoryless_targets{};
    std::uint64_t light_visibility_builds{}, light_visibility_reuses{};
    std::uint64_t fixed_camera_draws{}, moving_camera_draws{};
    unsigned int render_width{}, render_height{};
};
struct VisibilityProbe {
    std::array<Float4, 4> samples{};
    Float4 depths{};
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
    void set_retained(bool enabled);
    void set_camera(const Matrix& view, Float4 eye);
    bool set_light_intensity(float intensity);
    bool query_fixed_visibility(unsigned int x, unsigned int y, VisibilityProbe& result);
    std::uint64_t allocated_gpu_bytes() const;
    bool retained() const {
        return retained_;
    }
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
    bool upload_edits(id command, const Scene& scene);
    id make_texture(unsigned long format, unsigned int width, unsigned int height,
                    unsigned long usage, unsigned long samples = 1, bool transient = false);
    void update_reconstruction();
    enum class DrawSet { all, fixed, moving, retained_surface, uncached_surface };
    void encode_geometry(id encoder, const Scene& scene, id pipeline, const void* uniforms,
                         unsigned long uniform_size, DrawSet draw_set = DrawSet::all);
    id layer_{nil}, device_{nil}, queue_{nil}, pipeline_{nil}, shadow_pipeline_{nil},
        background_pipeline_{nil}, depth_state_{nil};
    id vertices_{nil}, indices_{nil}, instances_{nil}, actors_{nil}, depth_{nil}, shadow_{nil},
        color_{nil};
    std::array<id, 6> materials_{};
    std::array<id, 4> visibility_{};
    id visibility_depth_{nil}, visibility_pipeline_{nil}, restore_pipeline_{nil},
        restore_depth_state_{nil};
    id light_visibility_{nil}, light_visibility_pipeline_{nil}, query_pipeline_{nil};
    bool light_visibility_ready_{};
    float light_intensity_{1};
    bool retained_{true}, visibility_ready_{};
    Matrix view_{view_matrix()};
    Matrix reconstruction_{};
    bool memoryless_supported_{};
    Float4 eye_{0, 4.65F, 20.5F, 0};
    id fixed_shadow_{nil};
    bool fixed_shadow_ready_{};
    std::uint64_t shadow_revision_{};
    double shadow_time_{-1};
    id previous_command_{nil};
    bool previous_updated_shadows_{};
    unsigned int samples_{4};
    unsigned int width_{}, height_{};
    std::uint64_t revision_{};
    RenderStatistics statistics_{};
    std::string error_{};
    const Vertex* source_vertices_{};
    const std::uint32_t* source_indices_{};
    std::size_t source_instances_{}, source_actors_{};
};
} // namespace stillwater
