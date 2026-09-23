#include "stillwater/scene.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numbers>
#include <zlib.h>

namespace stillwater {
namespace {
// Disk format is little-endian IEEE754 with no implicit padding.
struct HabitatHeader {
    char magic[8]{};
    std::uint32_t vertices{}, indices{}, instances{}, objects{}, batches{}, actors{};
};
struct HabitatObject {
    std::uint32_t kind{}, instance{};
    float x{}, y{}, z{}, radius{};
    std::int32_t actor{};
    std::uint32_t reserved{};
};
class HabitatInput final {
  public:
    explicit HabitatInput(const std::string& path) : file_(gzopen(path.c_str(), "rb")) {}
    ~HabitatInput() {
        if (file_ != nullptr)
            gzclose(file_);
    }
    HabitatInput(const HabitatInput&) = delete;
    HabitatInput& operator=(const HabitatInput&) = delete;
    bool read(void* destination, std::size_t bytes) {
        if (file_ == nullptr || bytes > 256U * 1024U * 1024U)
            return false;
        const int count = gzread(file_, destination, static_cast<unsigned int>(bytes));
        return count >= 0 && static_cast<std::size_t>(count) == bytes;
    }
    bool finished() {
        char extra{};
        if (file_ == nullptr)
            return false;
        const int count = gzread(file_, &extra, 1);
        int status{};
        gzerror(file_, &status);
        return count == 0 && status == Z_OK;
    }

  private:
    gzFile file_{};
};
bool finite_floats(const void* data, std::size_t bytes) {
    const unsigned char* source = static_cast<const unsigned char*>(data);
    for (std::size_t offset = 0; offset < bytes; offset += sizeof(float)) {
        float value{};
        std::memcpy(&value, source + offset, sizeof(value));
        if (!std::isfinite(value))
            return false;
    }
    return true;
}
} // namespace
bool Scene::load_habitat(const std::string& path, std::string& error) {
    static_assert(std::endian::native == std::endian::little);
    static_assert(sizeof(HabitatHeader) == 32 && sizeof(HabitatObject) == 32);
    static_assert(sizeof(Vertex) == 128 && sizeof(Instance) == 112 && sizeof(Batch) == 16);
    HabitatInput input(path);
    HabitatHeader header{};
    error = "Invalid, missing or truncated habitat archive";
    if (!input.read(&header, sizeof(header)) || std::memcmp(header.magic, "STWSCN1\0", 8) != 0)
        return false;
    // Bound decompression and allocations before reading bulk payloads.
    if (header.vertices == 0 || header.vertices > 1500000 || header.indices == 0 ||
        header.indices > 9000000 || header.instances == 0 || header.instances > 50000 ||
        header.objects != header.instances || header.batches == 0 || header.batches > 256 ||
        header.actors != 16)
        return false;
    std::vector<Vertex> vertices(header.vertices);
    std::vector<std::uint32_t> indices(header.indices);
    std::vector<Instance> instances(header.instances);
    std::vector<HabitatObject> disk_objects(header.objects);
    std::vector<Batch> batches(header.batches);
    std::vector<Actor> actors(header.actors);
    if (!input.read(vertices.data(), vertices.size() * sizeof(Vertex)) ||
        !input.read(indices.data(), indices.size() * sizeof(std::uint32_t)) ||
        !input.read(instances.data(), instances.size() * sizeof(Instance)) ||
        !input.read(disk_objects.data(), disk_objects.size() * sizeof(HabitatObject)) ||
        !input.read(batches.data(), batches.size() * sizeof(Batch)) ||
        !input.read(actors.data(), actors.size() * sizeof(Actor)) || !input.finished())
        return false;
    if (!finite_floats(vertices.data(), vertices.size() * sizeof(Vertex)) ||
        !finite_floats(instances.data(), instances.size() * sizeof(Instance)) ||
        !finite_floats(actors.data(), actors.size() * sizeof(Actor)))
        return false;
    for (const Vertex& vertex : vertices) {
        if (vertex.binding.y != 0 &&
            (vertex.binding.x < 0 || vertex.binding.x >= static_cast<float>(instances.size()) ||
             std::floor(vertex.binding.x) != vertex.binding.x))
            return false;
    }
    for (const std::uint32_t index : indices) {
        if (index >= vertices.size())
            return false;
    }
    for (const Batch& batch : batches) {
        if (batch.mesh.first_index > indices.size() ||
            batch.mesh.index_count > indices.size() - batch.mesh.first_index ||
            batch.first_instance >= instances.size() || batch.instance_count == 0 ||
            batch.instance_count > instances.size() - batch.first_instance)
            return false;
    }
    for (const Instance& instance : instances) {
        if (instance.behavior.w < -1 || instance.behavior.w >= static_cast<float>(actors.size()) ||
            std::floor(instance.behavior.w) != instance.behavior.w)
            return false;
    }
    std::vector<Object> objects{};
    objects.reserve(disk_objects.size() + 128);
    for (const HabitatObject& entry : disk_objects) {
        if (entry.kind > static_cast<std::uint32_t>(ObjectKind::wood) ||
            entry.instance != objects.size() || !finite_floats(&entry.x, 4 * sizeof(float)) ||
            entry.radius < 0 || entry.actor < -1 ||
            entry.actor >= static_cast<int>(actors.size()) ||
            static_cast<float>(entry.actor) != instances[entry.instance].behavior.w)
            return false;
        objects.push_back({static_cast<std::uint32_t>(objects.size() + 1),
                           static_cast<ObjectKind>(entry.kind),
                           MeshKind::imported,
                           entry.instance,
                           {entry.x, entry.y, entry.z},
                           entry.radius,
                           entry.actor});
    }
    // The original translation enlarged every fish and flattened its depth range.
    // Restore upstream's 0.83..1.08 body scale, distributed across the water column.
    for (std::size_t index = 0; index < actors.size(); ++index) {
        Actor& actor = actors[index];
        const double seed = std::fmod(static_cast<double>(index) * 0.61803398875 + 0.23, 1.0);
        actor.center.w = static_cast<float>(0.83 + 0.25 * seed);
        actor.center.z = static_cast<float>(2.2 - 5.8 * std::fmod(seed * 3.71, 1.0));
    }
    // Keep our C++ crabs and bubble emitter alongside the translated planting and fish.
    // The original scene is untouched until the complete replacement is valid.
    const std::uint32_t sphere_base = static_cast<std::uint32_t>(vertices.size());
    const Scene native_scene{};
    const Mesh sphere = native_scene.meshes_[0];
    const std::uint32_t sphere_end =
        *std::max_element(native_scene.indices_.begin() + sphere.first_index,
                          native_scene.indices_.begin() + sphere.first_index + sphere.index_count) +
        1;
    vertices.insert(vertices.end(), native_scene.vertices_.begin(),
                    native_scene.vertices_.begin() + sphere_end);
    Batch extras{{static_cast<std::uint32_t>(indices.size()), sphere.index_count},
                 static_cast<std::uint32_t>(instances.size()),
                 0};
    for (std::uint32_t index = 0; index < sphere.index_count; ++index)
        indices.push_back(sphere_base + native_scene.indices_[sphere.first_index + index]);
    actors.push_back(native_scene.actors_[16]);
    actors.push_back(native_scene.actors_[17]);
    for (const Object& object : native_scene.objects_) {
        if (object.kind != ObjectKind::crab && object.kind != ObjectKind::bubble)
            continue;
        Object copy = object;
        copy.identity = static_cast<std::uint32_t>(objects.size() + 1);
        copy.instance_index = static_cast<std::uint32_t>(instances.size());
        copy.mesh = MeshKind::imported;
        objects.push_back(copy);
        instances.push_back(native_scene.instances_[object.instance_index]);
        ++extras.instance_count;
    }
    batches.push_back(extras);
    // One tiny shared mesh for leaf pearls: 96 triangles, with no particle framebuffer.
    const std::uint32_t pearl_base = static_cast<std::uint32_t>(vertices.size());
    Batch pearls{{static_cast<std::uint32_t>(indices.size()), 0},
                 static_cast<std::uint32_t>(instances.size()), 0};
    constexpr std::uint32_t rings = 6, segments = 8;
    for (std::uint32_t row = 0; row <= rings; ++row) {
        for (std::uint32_t column = 0; column <= segments; ++column) {
            const double latitude = std::numbers::pi * row / rings;
            const double longitude = 2 * std::numbers::pi * column / segments;
            const Float4 point{static_cast<float>(std::sin(latitude) * std::cos(longitude)),
                               static_cast<float>(std::cos(latitude)),
                               static_cast<float>(std::sin(latitude) * std::sin(longitude)), 1};
            vertices.push_back({point, {point.x, point.y, point.z, 0}});
        }
    }
    for (std::uint32_t row = 0; row < rings; ++row) {
        for (std::uint32_t column = 0; column < segments; ++column) {
            const std::uint32_t first = pearl_base + row * (segments + 1) + column;
            const std::uint32_t next = first + segments + 1;
            indices.insert(indices.end(), {first, next, first + 1, first + 1, next, next + 1});
        }
    }
    pearls.mesh.index_count = static_cast<std::uint32_t>(indices.size()) - pearls.mesh.first_index;
    std::vector<unsigned int> per_plant(header.instances, 0);
    std::uint32_t seed = 81731U;
    for (std::uint32_t index = 0; index < header.vertices && pearls.instance_count < 144; ++index) {
        const Vertex& leaf = vertices[index];
        if (leaf.binding.y < 0.5F)
            continue;
        const std::size_t parent = static_cast<std::size_t>(leaf.binding.x);
        if (instances[parent].behavior.x != 10 || per_plant[parent] >= 2 ||
            leaf.uv.x < 0.30F || leaf.uv.x > 0.70F || leaf.uv.y < 0.30F || leaf.uv.y > 0.85F)
            continue;
        const Matrix& matrix = instances[parent].transform;
        const Vec3 point{
            matrix.values[0]*leaf.position.x + matrix.values[4]*leaf.position.y + matrix.values[8]*leaf.position.z + matrix.values[12],
            matrix.values[1]*leaf.position.x + matrix.values[5]*leaf.position.y + matrix.values[9]*leaf.position.z + matrix.values[13],
            matrix.values[2]*leaf.position.x + matrix.values[6]*leaf.position.y + matrix.values[10]*leaf.position.z + matrix.values[14]};
        if (point.x < -7 || point.x > 7 || point.z < -7 || point.z > 3 || point.y < 0.6 || point.y > 7.4)
            continue;
        seed = seed * 1664525U + 1013904223U;
        if (seed % 41U != 0)
            continue;
        const float random = static_cast<float>(seed & 65535U) / 65536;
        const float radius = 0.022F + 0.026F * random;
        Instance pearl{};
        pearl.transform.values = {radius,0,0,0, 0,radius,0,0, 0,0,radius,0,
                                  static_cast<float>(point.x),static_cast<float>(point.y),static_cast<float>(point.z),1};
        pearl.color = {0.5F,0.7F,0.65F,1};
        pearl.behavior = {15, random * 37, 0.65F + random * 0.35F, -1};
        // Leaf vertex and parent instance remain references, so plant edits and
        // the exact upstream strand deformation carry the attached pearl with them.
        pearl.anatomy = {static_cast<float>(index), static_cast<float>(parent), radius, 24 + random * 19};
        objects.push_back({static_cast<std::uint32_t>(objects.size() + 1), ObjectKind::bubble,
                           MeshKind::imported, static_cast<std::uint32_t>(instances.size()),
                           point, radius, -1});
        instances.push_back(pearl);
        ++pearls.instance_count;
        ++per_plant[parent];
    }
    if (pearls.instance_count > 0)
        batches.push_back(pearls);
    vertices_ = std::move(vertices);
    indices_ = std::move(indices);
    instances_ = std::move(instances);
    objects_ = std::move(objects);
    batches_ = std::move(batches);
    actors_ = std::move(actors);
    statistics_ = {1, 1, 0, 0};
    actor_revisions_.assign(actors_.size(), 1);
    instance_revisions_.assign(instances_.size(), 1);
    error.clear();
    return true;
}
} // namespace stillwater
