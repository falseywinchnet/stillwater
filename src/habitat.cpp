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
    std::vector<std::uint32_t> pearl_candidates;
    // A coarse, load-time planting depth guide biases pearls toward the front
    // leaves instead of spending the batch behind overlapping foliage.
    constexpr std::size_t pearl_columns = 64, pearl_rows = 40;
    std::array<double, pearl_columns * pearl_rows> front_leaf;
    front_leaf.fill(-1000);
    for (std::uint32_t index = 0; index < header.vertices; ++index) {
        const Vertex& leaf = vertices[index];
        if (leaf.binding.y < 0.5F)
            continue;
        const std::size_t parent = static_cast<std::size_t>(leaf.binding.x);
        if (instances[parent].behavior.x != 10)
            continue;
        const Matrix& matrix = instances[parent].transform;
        const Vec3 point{
            matrix.values[0]*leaf.position.x + matrix.values[4]*leaf.position.y + matrix.values[8]*leaf.position.z + matrix.values[12],
            matrix.values[1]*leaf.position.x + matrix.values[5]*leaf.position.y + matrix.values[9]*leaf.position.z + matrix.values[13],
            matrix.values[2]*leaf.position.x + matrix.values[6]*leaf.position.y + matrix.values[10]*leaf.position.z + matrix.values[14]};
        if (point.x < -9 || point.x > 9 || point.z < -5 || point.z > 6 || point.y < 0.3 || point.y > 7.4)
            continue;
        const Vec3 screen = project(point, 1.6);
        if (screen.x < 0 || screen.x >= 1 || screen.y < 0 || screen.y >= 1)
            continue;
        const std::size_t cell = static_cast<std::size_t>(screen.y * pearl_rows) * pearl_columns +
                                 static_cast<std::size_t>(screen.x * pearl_columns);
        front_leaf[cell] = std::max(front_leaf[cell], point.z);
        if (leaf.uv.y < 0.05F || leaf.uv.y > 0.85F || !leaf_bubble_supported(leaf, instances[parent]))
            continue;
        pearl_candidates.push_back(index);
    }
    // Shuffle all eligible leaves before taking the bounded batch. File order
    // puts dense rear grasses first and otherwise hides almost every attachment.
    for (std::size_t count = pearl_candidates.size(); count > 1; --count) {
        seed = seed * 1664525U + 1013904223U;
        std::swap(pearl_candidates[count - 1], pearl_candidates[seed % count]);
    }
    // Reserve half the clusters for near planting, which has far fewer mesh
    // vertices than the grass beds. Each cluster stays beneath near-horizontal leaf surfaces.
    std::vector<std::uint32_t> attachments;
    std::vector<bool> selected(header.vertices, false);
    for (unsigned int pass = 0; pass < 2; ++pass) {
        const std::size_t limit = pass == 0 ? 864 : 1728;
        for (const std::uint32_t seed_index : pearl_candidates) {
            if (attachments.size() >= limit)
                break;
            const Vertex& seed_leaf = vertices[seed_index];
            const std::size_t parent = static_cast<std::size_t>(seed_leaf.binding.x);
            const Matrix& matrix = instances[parent].transform;
            const Vec3 point{
                matrix.values[0]*seed_leaf.position.x + matrix.values[4]*seed_leaf.position.y + matrix.values[8]*seed_leaf.position.z + matrix.values[12],
                matrix.values[1]*seed_leaf.position.x + matrix.values[5]*seed_leaf.position.y + matrix.values[9]*seed_leaf.position.z + matrix.values[13],
                matrix.values[2]*seed_leaf.position.x + matrix.values[6]*seed_leaf.position.y + matrix.values[10]*seed_leaf.position.z + matrix.values[14]};
            if (selected[seed_index] || per_plant[parent] >= 54 ||
                (pass == 0 && point.z < 0))
                continue;
            const Vec3 screen = project(point, 1.6);
            const std::size_t cell = static_cast<std::size_t>(screen.y * pearl_rows) * pearl_columns +
                                     static_cast<std::size_t>(screen.x * pearl_columns);
            if (point.z < front_leaf[cell] - 0.75)
                continue;
            std::vector<std::uint32_t> cluster;
            cluster.push_back(seed_index);
            for (const std::uint32_t neighbor : pearl_candidates) {
                if (cluster.size() >= 6 || attachments.size() + cluster.size() >= limit)
                    break;
                const Vertex& leaf = vertices[neighbor];
                if (selected[neighbor] || leaf.binding.x != seed_leaf.binding.x)
                    continue;
                const float dx = leaf.position.x - seed_leaf.position.x;
                const float dy = leaf.position.y - seed_leaf.position.y;
                const float dz = leaf.position.z - seed_leaf.position.z;
                if (dx*dx + dy*dy + dz*dz > 0.26F*0.26F)
                    continue;
                bool separated = true;
                for (const std::uint32_t member : cluster) {
                    const Vertex& other = vertices[member];
                    const float x = leaf.position.x - other.position.x;
                    const float y = leaf.position.y - other.position.y;
                    const float z = leaf.position.z - other.position.z;
                    if (x*x + y*y + z*z < 0.045F*0.045F)
                        separated = false;
                }
                if (separated)
                    cluster.push_back(neighbor);
            }
            for (const std::uint32_t member : cluster) {
                if (per_plant[parent] >= 54)
                    break;
                selected[member] = true;
                attachments.push_back(member);
                ++per_plant[parent];
            }
        }
    }
    for (const std::uint32_t index : attachments) {
        if (pearls.instance_count == 1728)
            break;
        const Vertex& leaf = vertices[index];
        const std::size_t parent = static_cast<std::size_t>(leaf.binding.x);
        const Matrix& matrix = instances[parent].transform;
        const Vec3 point{
            matrix.values[0]*leaf.position.x + matrix.values[4]*leaf.position.y + matrix.values[8]*leaf.position.z + matrix.values[12],
            matrix.values[1]*leaf.position.x + matrix.values[5]*leaf.position.y + matrix.values[9]*leaf.position.z + matrix.values[13],
            matrix.values[2]*leaf.position.x + matrix.values[6]*leaf.position.y + matrix.values[10]*leaf.position.z + matrix.values[14]};
        seed = seed * 1664525U + 1013904223U;
        const float phase = static_cast<float>(seed & 65535U) / 65535.0F;
        seed = seed * 1664525U + 1013904223U;
        const float random = static_cast<float>(seed & 65535U) / 65536;
        const float radius = 0.014F + 0.012F * random;
        Instance pearl{};
        pearl.transform.values = {radius,0,0,0, 0,radius,0,0, 0,0,radius,0,
                                  static_cast<float>(point.x),static_cast<float>(point.y),static_cast<float>(point.z),1};
        pearl.color = {0.5F, 0.7F, 0.65F, 1};
        pearl.behavior = {15, phase * (24 + random * 19), 0.65F + random * 0.35F, -1};
        // Leaf vertex and parent instance remain references, so plant edits and
        // the exact upstream strand deformation carry the attached pearl with them.
        pearl.anatomy = {static_cast<float>(index), static_cast<float>(parent), radius, 24 + random * 19};
        objects.push_back({static_cast<std::uint32_t>(objects.size() + 1), ObjectKind::bubble,
                           MeshKind::imported, static_cast<std::uint32_t>(instances.size()),
                           point, radius, -1});
        instances.push_back(pearl);
        ++pearls.instance_count;
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
