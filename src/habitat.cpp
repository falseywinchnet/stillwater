#include "stillwater/scene.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
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
