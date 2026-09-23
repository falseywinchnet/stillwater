#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace stillwater {
struct Vec3 {
    double x{};
    double y{};
    double z{};
};
struct alignas(16) Float4 {
    float x{};
    float y{};
    float z{};
    float w{};
};
struct alignas(16) Matrix {
    std::array<float, 16> values{};
};
struct Vertex {
    Float4 position{};
    Float4 normal{};
    Float4 color{1, 1, 1, 1};
    Float4 uv{};
    Float4 anchor{};
    Float4 bend{};
    Float4 along{};
    Float4 binding{}; // index, uses per-vertex object, imported geometry, reserved
};
struct Ray {
    Vec3 origin{};
    Vec3 direction{};
};
enum class ObjectKind : std::uint32_t { sand, rock, kelp, fern, fish, crab, bubble, wood };
enum class MeshKind : std::uint32_t { sphere, leaf, frond, fin, ground, imported, count };
struct Mesh {
    std::uint32_t first_index{};
    std::uint32_t index_count{};
};
// Layout is deliberately identical to aquarium.metal; all fields are explicit.
struct alignas(16) Instance {
    Matrix transform{};
    Float4 color{};
    Float4 behavior{}; // material, phase, sway, actor index (-1 if static)
    Float4 anatomy{};  // joint type/side; leaf pearls: source vertex, parent instance, radius, period
};
struct alignas(16) Actor {
    Float4 center{};        // xyz and scale
    Float4 cruise{};        // horizontal radius, vertical radius, angular speed, phase
    Float4 startled_from{}; // xyz and start time
    Float4 escape{};        // target xyz and duration (zero until first interaction)
    Float4 traits{};        // fish=0, crab=1
};
struct Object {
    std::uint32_t identity{};
    ObjectKind kind{ObjectKind::rock};
    MeshKind mesh{MeshKind::sphere};
    std::uint32_t instance_index{};
    Vec3 center{};
    double radius{};
    int actor{-1};
};
struct Batch {
    Mesh mesh{};
    std::uint32_t first_instance{};
    std::uint32_t instance_count{};
};
struct QueryHit {
    bool found{};
    std::uint32_t identity{};
    ObjectKind kind{};
    double distance{};
};
struct TapResult {
    std::size_t count{};
    std::array<std::uint32_t, 20> actors{};
};
struct Statistics {
    std::uint64_t revision{};
    std::uint64_t geometry_builds{};
    std::uint64_t actor_edits{};
    std::uint64_t instance_edits{};
};
Matrix multiply(const Matrix& left, const Matrix& right);
Matrix perspective(double aspect);
Matrix view_matrix();
Vec3 actor_position(const Actor& actor, double time);
Matrix leaf_bubble_transform(const Instance& bubble, const Vertex& leaf,
                             const Instance& parent, double time);
Vec3 project(Vec3 point, double aspect);
double ground_height(double x, double z);

class Scene final {
  public:
    Scene();
    bool load_habitat(const std::string& path, std::string& error);
    const std::vector<Vertex>& vertices() const {
        return vertices_;
    }
    const std::vector<std::uint32_t>& indices() const {
        return indices_;
    }
    const std::vector<Instance>& instances() const {
        return instances_;
    }
    const std::vector<Actor>& actors() const {
        return actors_;
    }
    const std::vector<Object>& objects() const {
        return objects_;
    }
    const std::vector<Batch>& batches() const {
        return batches_;
    }
    const Statistics& statistics() const {
        return statistics_;
    }
    const std::vector<std::uint64_t>& actor_revisions() const {
        return actor_revisions_;
    }
    const std::vector<std::uint64_t>& instance_revisions() const {
        return instance_revisions_;
    }
    bool move_object(std::uint32_t identity, Vec3 position);
    TapResult tap(double x, double y, double aspect, double time);
    TapResult pointer_motion(double x, double y, double aspect, double time, double speed);
    QueryHit query(const Ray& ray, double time) const;

  private:
    TapResult disturb(double x, double y, double aspect, double time, double radius,
                      double strength, bool pointer);
    void build_geometry();
    void build_habitat();
    void build_animals();
    void compile_batches();
    void add(ObjectKind kind, MeshKind mesh, Vec3 position, Vec3 scale, double yaw, Float4 color,
             Float4 behavior, Float4 anatomy = {});
    std::vector<Vertex> vertices_{};
    std::vector<std::uint32_t> indices_{};
    std::array<Mesh, 5> meshes_{};
    std::vector<Instance> instances_{};
    std::vector<Object> objects_{};
    std::vector<Actor> actors_{};
    std::vector<Batch> batches_{5};
    Statistics statistics_{};
    std::vector<std::uint64_t> actor_revisions_{};
    std::vector<std::uint64_t> instance_revisions_{};
};
} // namespace stillwater
