#include "stillwater/leaf_shadow.hpp"
#include <cmath>
#include <limits>

namespace stillwater {
namespace {
std::uint32_t root_of(std::vector<std::uint32_t>& parents, std::uint32_t index) {
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}
}
bool assign_ribbon_shadow_ids(std::vector<Vertex>& vertices,
                              std::span<const std::uint32_t> indices,
                              std::span<const Instance> instances, std::string& error) {
    if (vertices.size() > std::numeric_limits<std::uint32_t>::max() || indices.size() % 3 != 0) {
        error = "Invalid ribbon topology size";
        return false;
    }
    std::vector<std::uint32_t> parents(vertices.size()), labels(vertices.size());
    std::vector<unsigned char> ribbon(vertices.size());
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        parents[index] = static_cast<std::uint32_t>(index);
        const Vertex& vertex = vertices[index];
        if (vertex.binding.y < 0.5F || vertex.anchor.w < 0.7F)
            continue;
        const float object = vertex.binding.x;
        if (!std::isfinite(object) || object < 0 || std::floor(object) != object ||
            static_cast<double>(object) >= static_cast<double>(instances.size())) {
            error = "Invalid ribbon object binding";
            return false;
        }
        ribbon[index] = instances[static_cast<std::size_t>(object)].behavior.x == 10 ? 1 : 0;
    }
    for (std::size_t index = 0; index < indices.size(); index += 3) {
        for (std::size_t corner = 0; corner < 3; ++corner) {
            if (indices[index + corner] >= vertices.size()) {
                error = "Invalid ribbon triangle index";
                return false;
            }
        }
        const std::uint32_t first = indices[index];
        for (std::size_t corner = 1; corner < 3; ++corner) {
            const std::uint32_t other = indices[index + corner];
            if (ribbon[first] && ribbon[other] &&
                vertices[first].binding.x == vertices[other].binding.x) {
                const std::uint32_t a = root_of(parents, first), b = root_of(parents, other);
                parents[b] = a;
            }
        }
    }
    std::uint32_t count = 0;
    for (std::size_t index = 0; index < vertices.size(); ++index) {
        if (!ribbon[index])
            continue;
        const std::uint32_t root = root_of(parents, static_cast<std::uint32_t>(index));
        if (labels[root] == 0) {
            if (count == 65535) {
                error = "Ribbon shadow identity capacity exceeded";
                return false;
            }
            labels[root] = ++count;
        }
    }
    for (std::size_t index = 0; index < vertices.size(); ++index)
        vertices[index].binding.w = ribbon[index]
            ? static_cast<float>(labels[root_of(parents, static_cast<std::uint32_t>(index))]) : 0;
    return true;
}
}
