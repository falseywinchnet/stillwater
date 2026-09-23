#include "stillwater/sound.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <numbers>

namespace stillwater {
namespace {
constexpr double tau = 2.0 * std::numbers::pi;
void write_u16(std::ofstream& stream, std::uint16_t value) {
    const char bytes[2] = {static_cast<char>(value & 255U),
                           static_cast<char>((value >> 8U) & 255U)};
    stream.write(bytes, 2);
}
void write_u32(std::ofstream& stream, std::uint32_t value) {
    const char bytes[4] = {static_cast<char>(value & 255U), static_cast<char>((value >> 8U) & 255U),
                           static_cast<char>((value >> 16U) & 255U),
                           static_cast<char>((value >> 24U) & 255U)};
    stream.write(bytes, 4);
}
} // namespace
Sound make_ambience() {
    constexpr std::size_t frames = audio_sample_rate * 16U;
    Sound result{};
    result.stereo.resize(frames * 2U);
    std::uint32_t noise_state = 71137U;
    double filtered = 0.0;
    for (std::size_t index = 0; index < frames; ++index) {
        const double time = static_cast<double>(index) / audio_sample_rate;
        noise_state = noise_state * 1664525U + 1013904223U;
        const double noise = static_cast<double>(noise_state) / 4294967295.0 - 0.5;
        filtered = 0.965 * filtered + 0.035 * noise;
        const double pump =
            0.014 * std::sin(tau * 55.0 * time) + 0.006 * std::sin(tau * 110.0 * time);
        const double water = 0.18 * filtered;
        // Small rising resonances: a quiet air stone, with irregular event spacing.
        double bubbles_left = 0.0;
        double bubbles_right = 0.0;
        for (unsigned int bubble = 0; bubble < 31; ++bubble) {
            const double start = 0.13 + static_cast<double>(bubble) * 0.501 +
                                 0.10 * std::sin(static_cast<double>(bubble) * 7.0);
            const double age = time - start;
            if (age < 0.0 || age > 0.15)
                continue;
            const double frequency = 440.0 + static_cast<double>((bubble * 173U) % 900U);
            const double value = 0.027 * (1.0 - std::exp(-age * 700.0)) * std::exp(-age * 36.0) *
                                 std::sin(tau * (frequency * age + 800.0 * age * age));
            bubbles_left += value * (bubble % 2U == 0U ? 1.0 : 0.5);
            bubbles_right += value * (bubble % 2U == 0U ? 0.5 : 1.0);
        }
        result.stereo[index * 2U] = static_cast<float>(pump + water + bubbles_left);
        result.stereo[index * 2U + 1U] = static_cast<float>(pump + water * 0.9 + bubbles_right);
    }
    // Smoothly join the loop's noise state; periodic pump frequencies already match.
    constexpr std::size_t blend_frames = 512;
    for (std::size_t index = 0; index < blend_frames; ++index) {
        const double amount = static_cast<double>(index) / static_cast<double>(blend_frames - 1);
        for (std::size_t channel = 0; channel < 2; ++channel) {
            const std::size_t destination = (frames - blend_frames + index) * 2U + channel;
            const float target = result.stereo[channel];
            result.stereo[destination] =
                static_cast<float>((1.0 - amount) * result.stereo[destination] + amount * target);
        }
    }
    return result;
}
Sound make_glass_tap(double pan) {
    Sound result{};
    constexpr std::size_t frames = audio_sample_rate * 3U / 5U;
    result.stereo.resize(frames * 2U);
    pan = std::clamp(pan, -1.0, 1.0);
    const double left = std::sqrt(0.5 * (1.0 - pan));
    const double right = std::sqrt(0.5 * (1.0 + pan));
    for (std::size_t index = 0; index < frames; ++index) {
        const double time = static_cast<double>(index) / audio_sample_rate;
        const double attack = 1.0 - std::exp(-time * 6000.0);
        const double body = 0.24 * std::exp(-time * 19.0) * std::sin(tau * 730.0 * time);
        const double glass = 0.11 * std::exp(-time * 29.0) * std::sin(tau * 1931.0 * time) +
                             0.065 * std::exp(-time * 40.0) * std::sin(tau * 3173.0 * time);
        const double knock = 0.16 * std::exp(-time * 90.0) * std::sin(tau * 157.0 * time);
        const double tail = std::min(1.0, static_cast<double>(frames - index - 1) / 256.0);
        const double value = (body + glass + knock) * attack * tail;
        result.stereo[index * 2U] = static_cast<float>(value * left);
        result.stereo[index * 2U + 1U] = static_cast<float>(value * right);
    }
    return result;
}
bool write_wave(const Sound& sound, const char* path) {
    if (sound.stereo.size() > (UINT32_MAX - 36U) / 2U)
        return false;
    std::ofstream stream(path, std::ios::binary);
    if (!stream)
        return false;
    const std::uint32_t data_bytes = static_cast<std::uint32_t>(sound.stereo.size() * 2U);
    stream.write("RIFF", 4);
    write_u32(stream, 36U + data_bytes);
    stream.write("WAVEfmt ", 8);
    write_u32(stream, 16);
    write_u16(stream, 1);
    write_u16(stream, 2);
    write_u32(stream, audio_sample_rate);
    write_u32(stream, audio_sample_rate * 4U);
    write_u16(stream, 4);
    write_u16(stream, 16);
    stream.write("data", 4);
    write_u32(stream, data_bytes);
    for (const float sample : sound.stereo) {
        const std::int16_t value =
            static_cast<std::int16_t>(std::clamp(sample, -1.0F, 1.0F) * 32767.0F);
        write_u16(stream, static_cast<std::uint16_t>(value));
    }
    return stream.good();
}
} // namespace stillwater
