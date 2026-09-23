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
double Ambience::random() {
    random_state_ = random_state_ * 1664525U + 1013904223U;
    return (static_cast<double>(random_state_) + 0.5) / 4294967296.0;
}
Ambience::Ambience() {
    // A subdued 60 Hz motor/diaphragm character, strongest at 120 Hz, with
    // cabinet-damped higher harmonics. An aesthetic model, not a pump recording.
    constexpr std::array<double, 5> gains{0.0020, 0.0045, 0.0014, 0.0007, 0.0003};
    for (std::size_t index = 0; index < motor_.size(); ++index) {
        Resonance& mode = motor_[index];
        const double step = tau * 60.0 * static_cast<double>(index + 1) / audio_sample_rate;
        mode.coefficient = 2 * std::cos(step);
        mode.current = std::sin(step);
        mode.gain = gains[index];
    }
}
void Ambience::bubble() {
    for (Resonance& voice : bubbles_) {
        if (voice.remaining != 0)
            continue;
        // Minnaert's shallow-water radius relation, f ~= 3.26/r(m).
        // Each formation excites a short fixed resonance, never a musical sweep.
        const double radius = 0.0007 + 0.0025 * std::pow(random(), 2);
        const double frequency = 3.26 / radius;
        const double damping = 75 + 180 * random();
        const double step = tau * frequency / audio_sample_rate;
        voice.decay = std::exp(-damping / audio_sample_rate);
        voice.coefficient = 2 * voice.decay * std::cos(step);
        voice.previous = 0;
        voice.current = std::sin(step);
        voice.gain = 0.003 + 0.009 * random() * random();
        voice.pan = 0.25 + 0.40 * random();
        voice.remaining = static_cast<unsigned int>(audio_sample_rate * 0.10);
        break;
    }
    // Exponential waiting times, with slowly wandering flow density.
    until_bubble_ = 1U + static_cast<unsigned int>(
        -std::log(random()) * audio_sample_rate / (24 + 9 * flow_));
}
void Ambience::render(std::span<float> stereo) {
    for (std::size_t index = 0; index + 1 < stereo.size(); index += 2) {
        const double left_noise = 2 * random() - 1, right_noise = 2 * random() - 1;
        flow_ += 0.000018 * (left_noise - flow_);
        if (until_bubble_ == 0)
            bubble();
        --until_bubble_;
        if (until_surface_ == 0) {
            surface_ += 0.18 + 0.55 * random();
            until_surface_ = 1U + static_cast<unsigned int>(
                -std::log(random()) * audio_sample_rate * 0.65);
        }
        --until_surface_;
        surface_ *= 0.99935;
        double bubbles_left = 0, bubbles_right = 0;
        for (Resonance& voice : bubbles_) {
            if (voice.remaining == 0)
                continue;
            const double value = voice.current * voice.gain;
            bubbles_left += value * (1 - voice.pan);
            bubbles_right += value * voice.pan;
            const double next = voice.coefficient * voice.current -
                                voice.decay * voice.decay * voice.previous;
            voice.previous = voice.current;
            voice.current = next;
            --voice.remaining;
        }
        double pump = 0;
        for (Resonance& mode : motor_) {
            pump += mode.current * mode.gain;
            const double next = mode.coefficient * mode.current - mode.previous;
            mode.previous = mode.current;
            mode.current = next;
        }
        motor_air_ += 0.025 * (left_noise - motor_air_);
        water_left_ += 0.16 * (left_noise - water_left_);
        water_right_ += 0.16 * (right_noise - water_right_);
        water_low_left_ += 0.012 * (water_left_ - water_low_left_);
        water_low_right_ += 0.012 * (water_right_ - water_low_right_);
        bubble_left_ += 0.22 * (bubbles_left - bubble_left_);
        bubble_right_ += 0.22 * (bubbles_right - bubble_right_);
        const double water_gain = 0.006 + 0.012 * std::min(surface_, 1.5);
        const double motor = pump * (1 + 0.08 * motor_air_) + 0.004 * motor_air_;
        stereo[index] = static_cast<float>(fade_ * (motor + bubble_left_ +
            water_gain * (water_left_ - water_low_left_)));
        stereo[index + 1] = static_cast<float>(fade_ * (motor * 0.94 + bubble_right_ +
            water_gain * (water_right_ - water_low_right_)));
        fade_ = std::min(1.0, fade_ + 1.0 / (audio_sample_rate * 0.5));
    }
}
Sound make_ambience() {
    // A preview export only. Runtime streams the generator without repeating.
    Sound result{};
    result.stereo.resize(audio_sample_rate * 30U * 2U);
    Ambience ambience{};
    ambience.render(result.stereo);
    constexpr std::size_t fade_frames = audio_sample_rate / 2U;
    const std::size_t frames = result.stereo.size() / 2;
    for (std::size_t index = 0; index < fade_frames; ++index) {
        const float gain = static_cast<float>(fade_frames - index - 1) /
                           static_cast<float>(fade_frames);
        result.stereo[(frames - fade_frames + index) * 2] *= gain;
        result.stereo[(frames - fade_frames + index) * 2 + 1] *= gain;
    }
    return result;
}
Sound make_glass_tap(double pan) {
    Sound result{};
    constexpr std::size_t frames = audio_sample_rate / 5U;
    result.stereo.resize(frames * 2U);
    pan = std::clamp(pan, -1.0, 1.0);
    const double left = std::sqrt(0.5 * (1.0 - pan));
    const double right = std::sqrt(0.5 * (1.0 + pan));
    std::uint32_t state = 29173U;
    double contact = 0, low = 0;
    for (std::size_t index = 0; index < frames; ++index) {
        const double time = static_cast<double>(index) / audio_sample_rate;
        state = state * 1664525U + 1013904223U;
        const double noise = static_cast<double>(state) / 2147483648.0 - 1;
        contact += 0.12 * (noise - contact);
        low += 0.015 * (contact - low);
        const double attack = 1.0 - std::exp(-time * 2200.0);
        // A fingertip against water-loaded glass: brief, dull and close to the
        // ambient level. No long bright panel ring or loud low-frequency slam.
        const double body = 0.009 * std::exp(-time * 55.0) * std::sin(tau * 220.0 * time);
        const double glass = 0.002 * std::exp(-time * 120.0) * std::sin(tau * 920.0 * time) +
                             0.001 * std::exp(-time * 160.0) * std::sin(tau * 1770.0 * time);
        const double touch = 0.012 * (contact - low) * std::exp(-time * 95.0);
        const double tail = std::min(1.0, static_cast<double>(frames - index - 1) / 256.0);
        const double value = (body + glass + touch) * attack * tail;
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
