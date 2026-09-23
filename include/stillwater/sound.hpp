#pragma once
#include <cstddef>
#include <array>
#include <cstdint>
#include <span>
#include <vector>
namespace stillwater {
constexpr unsigned int audio_sample_rate = 22050;
struct Sound {
    std::vector<float> stereo;
};
// Audio-thread owned, continuous synthesis. No loop buffer or callback allocation.
class Ambience final {
  public:
    Ambience();
    void render(std::span<float> stereo);
  private:
    struct Resonance {
        double previous{}, current{}, coefficient{}, decay{}, gain{}, pan{};
        unsigned int remaining{};
    };
    double random();
    void bubble();
    std::array<Resonance, 24> bubbles_{};
    std::array<Resonance, 5> motor_{};
    std::uint32_t random_state_{71137U};
    unsigned int until_bubble_{}, until_surface_{};
    double motor_air_{}, water_left_{}, water_right_{}, water_low_left_{}, water_low_right_{};
    double bubble_left_{}, bubble_right_{}, surface_{}, flow_{}, fade_{};
};
Sound make_ambience();
Sound make_glass_tap(double pan);
bool write_wave(const Sound& sound, const char* path);
} // namespace stillwater
