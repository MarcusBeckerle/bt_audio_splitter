#pragma once
#include "core.hpp"
#include <atomic>
#include <memory>
#include <mutex>

namespace splitter {
// One pipe per output. Driver calls never run under these locks. A slow/failed output
// can fill only its own bounded queue, without stopping capture or other devices.
class AudioPipe {
    std::mutex mutex_;
    FrameQueue queue_;
    std::vector<float> delay_;
    std::size_t delayPosition_ = 0;
    bool closed_ = false;
public:
    std::atomic<int> volume{70};
    explicit AudioPipe(std::size_t frames, int gain = 70) : queue_(frames), volume(gain) {}
    void delayFrames(std::size_t frames) {
        // Allocate outside the audio lock; unchanged settings are cheap to apply.
        { std::lock_guard lock(mutex_); if (delay_.size() == frames * 2) return; }
        std::vector<float> next(frames * 2, 0.0f);
        std::lock_guard lock(mutex_); delay_.swap(next); delayPosition_ = 0;
    }
    void clearPending() { std::lock_guard lock(mutex_); queue_.clear(); }
    void push(const float* data, std::size_t frames) {
        std::lock_guard lock(mutex_); if (!closed_) queue_.push(data, frames);
    }
    void pop(float* data, std::size_t frames) {
        std::lock_guard lock(mutex_);
        queue_.pop(data, frames, 1.0f);
        const float gain = volumeGain(volume.load());
        for (std::size_t i = 0; i < frames * 2; ++i) {
            if (!delay_.empty() && !closed_) {
                std::swap(data[i], delay_[delayPosition_]);
                delayPosition_ = (delayPosition_ + 1) % delay_.size();
            }
            data[i] *= gain; // Apply gain after the delay: mute/volume changes are immediate.
        }
    }
    void close() { std::lock_guard lock(mutex_); closed_ = true; queue_.clear(); std::fill(delay_.begin(), delay_.end(), 0.0f); }
};
class AudioBus {
    using Pipes = std::vector<std::shared_ptr<AudioPipe>>;
    std::mutex mutex_;
    std::shared_ptr<const Pipes> pipes_ = std::make_shared<const Pipes>();
public:
    // Only the control thread edits membership; the capture thread reads immutable snapshots.
    void set(Pipes pipes) {
        std::lock_guard lock(mutex_);
        if (*pipes_ == pipes) return;
        pipes_ = std::make_shared<const Pipes>(std::move(pipes));
    }
    void push(const float* data, std::size_t frames) {
        std::shared_ptr<const Pipes> pipes;
        { std::lock_guard lock(mutex_); pipes = pipes_; }
        for (const auto& pipe : *pipes) pipe->push(data, frames);
    }
};
}
