#pragma once
#include <string>
namespace stillwater {
struct Options {
    bool desktop{true};
    bool muted{};
    bool paused{};
    bool smoke_tap{};
    bool retained{true};
    bool conv_fast{};
    bool compact_camera{true};
    bool record_shading{};
    bool specialize_foliage{true};
    double capture_time{};
    std::string verify_retained{};
    unsigned int fps{24};
    unsigned int samples{4};
    double render_scale{1};
    double quit_after{};
    std::string capture{};
    std::string metrics{};
};
int run_macos(const Options& options);
} // namespace stillwater
