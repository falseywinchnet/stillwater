#pragma once
#include <cmath>

namespace stillwater {
constexpr double shadow_refresh_rate = 16.0;
// Phase-locked slots avoid rounding a 16 Hz request down to 12 Hz at 24 fps.
// Edits can still force a refresh independently; missed slots never cause a burst.
inline bool shadow_refresh_due(double time, double previous) {
    return previous < 0 || time < previous ||
           std::floor(time * shadow_refresh_rate) > std::floor(previous * shadow_refresh_rate);
}
} // namespace stillwater
