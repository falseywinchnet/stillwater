#pragma once
#include "stillwater/sound.hpp"
#include <AudioToolbox/AudioToolbox.h>
#include <array>
namespace stillwater {
class Audio final {
  public:
    Audio() = default;
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    bool initialize();
    bool set_ambience(bool enabled);
    void tap(double pan);
    bool enabled() const {
        return enabled_;
    }

  private:
    static void refill(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer);
    static void tap_finished(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer);
    void fill(AudioQueueBufferRef buffer);
    Sound ambience_{};
    Sound tap_sound_{};
    std::size_t cursor_{};
    AudioQueueRef ambient_queue_{nullptr}, tap_queue_{nullptr};
    std::array<AudioQueueBufferRef, 3> buffers_{};
    AudioQueueBufferRef tap_buffer_{nullptr};
    bool enabled_{};
    bool initialized_{};
};
} // namespace stillwater
