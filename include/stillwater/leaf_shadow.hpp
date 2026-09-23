#pragma once
#include "scene.hpp"
#include <span>

namespace stillwater {
// Assign separate, nonzero R16 identities to connected thin ribbon blades.
// Other geometry remains zero. Failure leaves vertex metadata unchanged.
bool assign_ribbon_shadow_ids(std::vector<Vertex>& vertices,
                              std::span<const std::uint32_t> indices,
                              std::span<const Instance> instances, std::string& error);
}
