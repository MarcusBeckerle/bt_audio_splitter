#pragma once
#include "core.hpp"
#include <array>
#include <complex>
#include <numbers>

namespace splitter {
constexpr std::size_t spectrumBands = 48;
using Spectrum = std::array<float, spectrumBands>;
// Log-frequency bands from 40 Hz to 20 kHz, displayed over -72..0 dBFS.
// Both stereo channels are analyzed independently so opposite-phase audio is visible.
class SpectrumAnalyzer {
    static constexpr std::size_t size = 2048;
    std::array<std::array<float, size>, 2> samples_{};
    std::array<std::complex<float>, size> transform_{};
    std::size_t filled_ = 0;
    unsigned rate_;
    Spectrum bands_{};
    void analyze() {
        std::array<float, size / 2 + 1> magnitudes{};
        for (int channel = 0; channel < 2; ++channel) {
            for (std::size_t i = 0; i < size; ++i) {
                float hann = .5f - .5f * std::cos(2 * std::numbers::pi_v<float> * i / (size - 1));
                transform_[i] = samples_[channel][i] * hann;
            }
            for (std::size_t i = 1, j = 0; i < size; ++i) {
                std::size_t bit = size >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit; if (i < j) std::swap(transform_[i], transform_[j]);
            }
            for (std::size_t length = 2; length <= size; length <<= 1) {
                auto step = std::polar(1.0f, -2 * std::numbers::pi_v<float> / length);
                for (std::size_t i = 0; i < size; i += length) {
                    std::complex<float> rotation{1, 0};
                    for (std::size_t j = 0; j < length / 2; ++j) {
                        auto a = transform_[i + j], b = transform_[i + j + length / 2] * rotation;
                        transform_[i + j] = a + b; transform_[i + j + length / 2] = a - b; rotation *= step;
                    }
                }
            }
            for (std::size_t bin = 1; bin <= size / 2; ++bin)
                magnitudes[bin] = std::max(magnitudes[bin], std::abs(transform_[bin]) * (4.0f / (size - 1)));
        }
        for (std::size_t band = 0; band < spectrumBands; ++band) {
            float low = 40.0f * std::pow(500.0f, static_cast<float>(band) / spectrumBands);
            float high = 40.0f * std::pow(500.0f, static_cast<float>(band + 1) / spectrumBands);
            auto first = std::max<std::size_t>(1, static_cast<std::size_t>(std::ceil(low * size / rate_)));
            auto end = std::min<std::size_t>(size / 2 + 1, std::max(first + 1, static_cast<std::size_t>(std::ceil(high * size / rate_))));
            float peak = 0;
            for (auto bin = first; bin < end; ++bin) peak = std::max(peak, magnitudes[bin]);
            bands_[band] = peak > 0 ? std::clamp((20 * std::log10(peak) + 72) / 72, 0.0f, 1.0f) : 0;
        }
    }
public:
    explicit SpectrumAnalyzer(unsigned rate) : rate_(rate) { if (!rate) throw std::invalid_argument("Spectrum sample rate must be positive."); }
    bool push(const float* stereo, std::size_t frames) {
        bool updated = false;
        for (std::size_t i = 0; i < frames; ++i) {
            for (int c = 0; c < 2; ++c) {
                float value = stereo[i * 2 + c]; samples_[c][filled_] = std::isfinite(value) ? value : 0;
            }
            if (++filled_ == size) { analyze(); filled_ = 0; updated = true; }
        }
        return updated;
    }
    const Spectrum& bands() const { return bands_; }
};
inline Spectrum outputSpectrum(const Spectrum& input, int volume, bool playing) {
    if (!playing || volume <= 0) return {};
    Spectrum output{};
    float attenuation = 20 * std::log10(volumeGain(volume)) / 72;
    for (std::size_t i = 0; i < input.size(); ++i) output[i] = std::clamp(input[i] + attenuation, 0.0f, 1.0f);
    return output;
}
}
