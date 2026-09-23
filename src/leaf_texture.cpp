#include "stillwater/leaf_texture.hpp"
#include <algorithm>
#include <cmath>

namespace stillwater {
namespace {
float grain_hash(unsigned int x, unsigned int y, unsigned int seed) {
    std::uint32_t bits = x * 0x9e3779b9U + y * 0x85ebca6bU + seed;
    bits ^= bits >> 16;
    bits *= 0x7feb352dU;
    bits ^= bits >> 15;
    bits *= 0x846ca68bU;
    bits ^= bits >> 16;
    return static_cast<float>(bits >> 8) / 16777215.0F - 0.5F;
}
float grain_band(unsigned int x, unsigned int y, unsigned int spacing, unsigned int seed) {
    const unsigned int cells = leaf_grain_size / spacing;
    const unsigned int ix = x / spacing, iy = y / spacing;
    float fx = static_cast<float>(x % spacing) / static_cast<float>(spacing);
    float fy = static_cast<float>(y % spacing) / static_cast<float>(spacing);
    fx = fx * fx * (3 - 2 * fx);
    fy = fy * fy * (3 - 2 * fy);
    const float a = grain_hash(ix, iy, seed);
    const float b = grain_hash((ix + 1) % cells, iy, seed);
    const float c = grain_hash(ix, (iy + 1) % cells, seed);
    const float d = grain_hash((ix + 1) % cells, (iy + 1) % cells, seed);
    return (a + (b - a) * fx) * (1 - fy) + (c + (d - c) * fx) * fy;
}
}
std::vector<std::uint8_t> make_leaf_grain() {
    std::vector<std::uint8_t> pixels(leaf_grain_size * leaf_grain_size);
    for (unsigned int y = 0; y < leaf_grain_size; ++y) {
        for (unsigned int x = 0; x < leaf_grain_size; ++x) {
            // Fine stochastic grain plus two correlated microscopic scales.
            // All bands wrap; no coarse pigment patches or directional veins.
            const float value = 0.35F * grain_hash(x, y, 0x51a7U) +
                                0.40F * grain_band(x, y, 4, 0x710bU) +
                                0.25F * grain_band(x, y, 16, 0x9831U);
            const int byte = static_cast<int>(std::lround(128 + 255 * value));
            pixels[y * leaf_grain_size + x] = static_cast<std::uint8_t>(std::clamp(byte, 0, 255));
        }
    }
    return pixels;
}
}
