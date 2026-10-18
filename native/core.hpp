#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace splitter {
constexpr std::size_t maxOutputs = 5;
constexpr int maxOutputDelayMs = 1000;
// A percentage controls attenuation in dB, not linear amplitude. Keep an exact
// unity endpoint and digital silence at zero; the quiet range stays usable.
inline float volumeGain(int percent) {
    percent = std::clamp(percent, 0, 100);
    return percent == 0 ? 0.0f : std::pow(10.0f, (-60.0f * (100 - percent) / 99.0f) / 20.0f);
}
struct Output {
    std::string id, name;
    int volume = 70;
    bool enabled = true;
    int delayMs = 0;
    bool muted = false;
};
struct Settings {
    std::string source;
    std::vector<Output> outputs;
    bool muted = false;
    void add(Output output) {
        if (outputs.size() >= maxOutputs) throw std::runtime_error("Maximal fünf Ausgaben möglich.");
        if (output.id.empty() || std::any_of(outputs.begin(), outputs.end(), [&](const auto& o) { return o.id == output.id; }))
            throw std::runtime_error("Dieses Gerät wurde bereits hinzugefügt.");
        output.volume = std::clamp(output.volume, 0, 100);
        if (output.delayMs < 0 || output.delayMs > maxOutputDelayMs) throw std::runtime_error("Ungültiger Audio-Zeitabgleich.");
        outputs.push_back(std::move(output));
    }
    void remove(const std::string& id) {
        std::erase_if(outputs, [&](const auto& o) { return o.id == id; });
    }
};

// Master mute and device mute are independent gates; neither changes the saved slider.
inline int effectiveVolume(const Settings& settings, const Output& output) {
    return settings.muted || output.muted ? 0 : output.volume;
}

// Versioned, UTF-8 text with quoted strings. Endpoint IDs, not friendly names, are identities.
inline std::string serialize(const Settings& s) {
    std::ostringstream out;
    out << "BT_AUDIO_SPLITTER 3\n" << std::quoted(s.source) << '\n' << s.outputs.size() << '\n';
    for (const auto& o : s.outputs)
        out << std::quoted(o.id) << ' ' << std::quoted(o.name) << ' ' << o.volume << ' ' << o.enabled << ' ' << o.delayMs << ' ' << o.muted << '\n';
    out << s.muted << '\n';
    return out.str();
}
inline Settings deserialize(const std::string& data) {
    if (data.size() > 100000) throw std::runtime_error("Einstellungsdatei zu groß.");
    std::istringstream in(data);
    std::string marker;
    int version = 0;
    std::size_t count = 0;
    Settings s;
    if (!(in >> marker >> version) || marker != "BT_AUDIO_SPLITTER" || (version < 1 || version > 3) ||
        !(in >> std::quoted(s.source) >> count) || count > maxOutputs)
        throw std::runtime_error("Ungültige Einstellungsdatei.");
    for (std::size_t i = 0; i < count; ++i) {
        Output o;
        int enabled = 0;
        if (!(in >> std::quoted(o.id) >> std::quoted(o.name) >> o.volume >> enabled) ||
            (enabled != 0 && enabled != 1) || o.volume < 0 || o.volume > 100)
            throw std::runtime_error("Ungültige Geräteeinstellung.");
        o.enabled = enabled != 0;
        if (version >= 2 && (!(in >> o.delayMs) || o.delayMs < 0 || o.delayMs > maxOutputDelayMs))
            throw std::runtime_error("Ungültiger Audio-Zeitabgleich.");
        if (version >= 3) {
            int muted = 0;
            if (!(in >> muted) || (muted != 0 && muted != 1)) throw std::runtime_error("Ungültige Geräte-Stummschaltung.");
            o.muted = muted != 0;
        }
        s.add(std::move(o));
    }
    if (version >= 2) {
        int muted = 0;
        if (!(in >> muted) || (muted != 0 && muted != 1)) throw std::runtime_error("Ungültige Stummschaltung.");
        s.muted = muted != 0;
    }
    in >> std::ws;
    if (!in.eof()) throw std::runtime_error("Unerwartete Daten in Einstellungen.");
    return s;
}

enum class RouteState { Paused, Blocked, WaitingDevice, WaitingSource, Playing };
inline RouteState decide(bool enabled, bool allowed, bool available, bool sourceAvailable) {
    if (!enabled) return RouteState::Paused;
    if (!allowed) return RouteState::Blocked;
    if (!available) return RouteState::WaitingDevice;
    if (!sourceAvailable) return RouteState::WaitingSource;
    return RouteState::Playing;
}

// The audio owner thread is the sole reader/writer. All quantities are stereo frames.
// Drop the oldest complete frames on overflow: reconnects cannot replay old movie audio.
class FrameQueue {
    std::vector<float> data_;
    std::size_t head_ = 0, count_ = 0;
public:
    explicit FrameQueue(std::size_t capacity) : data_(capacity * 2) {
        if (!capacity) throw std::invalid_argument("Queue capacity must be positive.");
    }
    std::size_t capacity() const { return data_.size() / 2; }
    std::size_t size() const { return count_; }
    void clear() { head_ = count_ = 0; }
    void push(const float* samples, std::size_t frames) {
        if (frames > capacity()) { samples += (frames - capacity()) * 2; frames = capacity(); }
        if (count_ + frames > capacity()) {
            auto drop = count_ + frames - capacity();
            head_ = (head_ + drop) % capacity(); count_ -= drop;
        }
        for (std::size_t f = 0; f < frames; ++f) {
            auto at = ((head_ + count_ + f) % capacity()) * 2;
            data_[at] = samples[f * 2]; data_[at + 1] = samples[f * 2 + 1];
        }
        count_ += frames;
    }
    void pop(float* samples, std::size_t frames, float gain) {
        auto read = std::min(frames, count_);
        for (std::size_t f = 0; f < read; ++f) {
            auto at = ((head_ + f) % capacity()) * 2;
            for (int c = 0; c < 2; ++c) {
                float value = data_[at + c] * gain;
                samples[f * 2 + c] = std::isfinite(value) ? std::clamp(value, -1.0f, 1.0f) : 0.0f;
            }
        }
        std::fill(samples + read * 2, samples + frames * 2, 0.0f);
        head_ = (head_ + read) % capacity(); count_ -= read;
    }
};
} // namespace splitter
