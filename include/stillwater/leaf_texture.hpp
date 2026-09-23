#pragma once
#include <cstdint>
#include <vector>

namespace stillwater {
inline constexpr unsigned int leaf_grain_size = 2048;
// One deterministic, periodic scalar material texture. Neutral is byte 128.
std::vector<std::uint8_t> make_leaf_grain();
}
