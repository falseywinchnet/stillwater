#include "macos_audio.hpp"
#include <algorithm>
#include <array>
#include <cstring>
namespace stillwater {
Audio::~Audio() {
    if (ambient_queue_ != nullptr)
        AudioQueueDispose(ambient_queue_, true);
    if (tap_queue_ != nullptr)
        AudioQueueDispose(tap_queue_, true);
}
bool Audio::initialize() {
    ambience_ = make_ambience();
    tap_sound_ = make_glass_tap(0);
    AudioStreamBasicDescription format{};
    format.mSampleRate = audio_sample_rate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked;
    format.mBytesPerPacket = 8;
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 8;
    format.mChannelsPerFrame = 2;
    format.mBitsPerChannel = 32;
    if (AudioQueueNewOutput(&format, &Audio::refill, this, nullptr, nullptr, 0, &ambient_queue_) !=
        noErr)
        return false;
    if (AudioQueueNewOutput(&format, &Audio::tap_finished, this, nullptr, nullptr, 0,
                            &tap_queue_) != noErr)
        return false;
    for (AudioQueueBufferRef& buffer : buffers_)
        if (AudioQueueAllocateBuffer(ambient_queue_, 4096U * 8U, &buffer) != noErr)
            return false;
    if (AudioQueueAllocateBuffer(tap_queue_,
                                 static_cast<UInt32>(tap_sound_.stereo.size() * sizeof(float)),
                                 &tap_buffer_) != noErr)
        return false;
    initialized_ = true;
    return true;
}
void Audio::fill(AudioQueueBufferRef buffer) {
    float* output = static_cast<float*>((*buffer).mAudioData);
    constexpr std::size_t samples = 4096U * 2U;
    std::size_t filled = 0;
    while (filled < samples) {
        const std::size_t count = std::min(samples - filled, ambience_.stereo.size() - cursor_);
        std::memcpy(output + filled, ambience_.stereo.data() + cursor_, count * sizeof(float));
        filled += count;
        cursor_ = (cursor_ + count) % ambience_.stereo.size();
    }
    (*buffer).mAudioDataByteSize = static_cast<UInt32>(samples * sizeof(float));
}
void Audio::refill(void* context, AudioQueueRef queue, AudioQueueBufferRef buffer) {
    Audio& audio = *static_cast<Audio*>(context);
    audio.fill(buffer);
    AudioQueueEnqueueBuffer(queue, buffer, 0, nullptr);
}
void Audio::tap_finished(void*, AudioQueueRef queue, AudioQueueBufferRef) {
    AudioQueueStop(queue, false);
}
bool Audio::set_ambience(bool enabled) {
    if (!initialized_)
        return false;
    if (enabled == enabled_)
        return true;
    if (!enabled) {
        AudioQueueStop(ambient_queue_, true);
        AudioQueueReset(ambient_queue_);
        enabled_ = false;
        return true;
    }
    cursor_ = 0;
    for (AudioQueueBufferRef buffer : buffers_) {
        fill(buffer);
        if (AudioQueueEnqueueBuffer(ambient_queue_, buffer, 0, nullptr) != noErr)
            return false;
    }
    enabled_ = AudioQueueStart(ambient_queue_, nullptr) == noErr;
    return enabled_;
}
void Audio::tap(double pan) {
    if (tap_queue_ == nullptr || tap_buffer_ == nullptr)
        return;
    AudioQueueStop(tap_queue_, true);
    AudioQueueReset(tap_queue_);
    tap_sound_ = make_glass_tap(pan);
    const UInt32 bytes = static_cast<UInt32>(tap_sound_.stereo.size() * sizeof(float));
    std::memcpy((*tap_buffer_).mAudioData, tap_sound_.stereo.data(), bytes);
    (*tap_buffer_).mAudioDataByteSize = bytes;
    if (AudioQueueEnqueueBuffer(tap_queue_, tap_buffer_, 0, nullptr) == noErr)
        AudioQueueStart(tap_queue_, nullptr);
}
} // namespace stillwater
