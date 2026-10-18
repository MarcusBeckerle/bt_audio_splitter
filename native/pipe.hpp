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
    bool closed_ = false;
public:
    std::atomic<int> volume{70};
    explicit AudioPipe(std::size_t frames, int gain = 70) : queue_(frames), volume(gain) {}
    void push(const float* data, std::size_t frames) {
        std::lock_guard lock(mutex_); if (!closed_) queue_.push(data, frames);
    }
    void pop(float* data, std::size_t frames) {
        std::lock_guard lock(mutex_); queue_.pop(data, frames, std::clamp(volume.load(), 0, 100) / 100.0f);
    }
    void close() { std::lock_guard lock(mutex_); closed_ = true; queue_.clear(); }
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
