#pragma once
#include <cstddef>
#include <vector>
namespace stillwater {
constexpr unsigned int audio_sample_rate = 22050;
struct Sound {
    std::vector<float> stereo;
};
Sound make_ambience();
Sound make_glass_tap(double pan);
bool write_wave(const Sound& sound, const char* path);
} // namespace stillwater
