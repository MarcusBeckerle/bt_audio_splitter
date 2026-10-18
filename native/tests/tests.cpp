#include "../core.hpp"
#include "../platform.hpp"
#include "../pipe.hpp"
#include "../spectrum.hpp"
#include <iostream>
#include <random>
#include <deque>
#include <array>
#include <limits>
#include <functional>
#include <thread>
using namespace splitter;
void require(bool condition) { if (!condition) throw std::runtime_error("Assertion failed"); }
template<class F> void rejects(F operation) { bool rejected = false; try { operation(); } catch (const std::exception&) { rejected = true; } require(rejected); }
int main() {
    int passed = 0, failed = 0;
    auto test = [&](const char* name, const std::function<void()>& operation) {
        try { operation(); ++passed; std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& e) { ++failed; std::cerr << "FAIL " << name << ": " << e.what() << '\n'; }
    };
    test("Five outputs accepted, sixth and duplicate rejected", [] {
        Settings s; for (int i = 0; i < 5; ++i) s.add({std::to_string(i), "Headphones", 70, true});
        rejects([&] { s.add({"six", "Headphones"}); }); s.remove("4"); rejects([&] { s.add({"0", "Duplicate"}); }); require(s.outputs.size() == 4);
    });
    test("UTF-8 names, quotes, IDs and independent gains round-trip", [] {
        Settings s{"cable", {{"one", "Kopfhörer \\\"名", 35, true}, {"two", "Kopfhörer \\\"名", 81, false}}};
        auto restored = deserialize(serialize(s)); require(serialize(restored) == serialize(s)); require(restored.outputs[1].volume == 81); require(!restored.outputs[1].enabled);
    });
    test("Power cycle retains enabled intent", [] {
        Output o{"id", "Name", 80, true};
        require(decide(o.enabled, true, true, true) == RouteState::Playing);
        require(decide(o.enabled, true, false, true) == RouteState::WaitingDevice);
        require(decide(o.enabled, true, true, true) == RouteState::Playing);
    });
    test("Pause survives power cycle, source loss and settings reload", [] {
        Settings s{"cable", {{"id", "Name", 80, false}}}; auto restored = deserialize(serialize(s));
        for (bool present : {false, true}) for (bool source : {false, true}) require(decide(restored.outputs[0].enabled, true, present, source) == RouteState::Paused);
    });
    test("Removing an output persists without resurrection", [] {
        Settings s{"cable", {{"id", "Name", 80, true}}}; s.remove("id"); require(deserialize(serialize(s)).outputs.empty());
    });
    test("Feedback and hands-free routes remain blocked", [] {
        require(decide(true, false, true, true) == RouteState::Blocked);
        require(decide(true, false, false, false) == RouteState::Blocked);
    });
    test("Missing source and suspended setup preserve intent", [] {
        require(decide(true, true, true, false) == RouteState::WaitingSource);
        require(decide(true, true, true, true) == RouteState::Playing);
    });
    test("Corrupt versions, duplicates, volumes, flags and excess data rejected", [] {
        for (const auto& data : {"bad", "BT_AUDIO_SPLITTER 3\n\"\"\n0", "BT_AUDIO_SPLITTER 1\n\"\"\n6", "BT_AUDIO_SPLITTER 1\n\"\"\n1\n\"a\" \"b\" 101 1", "BT_AUDIO_SPLITTER 1\n\"\"\n1\n\"a\" \"b\" 20 2", "BT_AUDIO_SPLITTER 1\n\"\"\n0 extra", "BT_AUDIO_SPLITTER 1\n\"\"\n2\n\"a\" \"b\" 20 1\n\"a\" \"c\" 40 1"}) rejects([&] { deserialize(data); });
    });
    test("Ring preserves stereo frames, drops oldest and pads silence", [] {
        FrameQueue q(3); float in[] = {.1f, -.1f, .2f, -.2f, .3f, -.3f, .4f, -.4f}; float out[8];
        q.push(in, 4); q.pop(out, 4, .5f); require(out[0] == .1f && out[1] == -.1f && out[4] == .2f && out[6] == 0 && out[7] == 0); require(!q.size());
    });
    test("Non-finite audio is silenced and gain is bounded", [] {
        FrameQueue q(2); float in[] = {std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN(), 2, -2}, out[4];
        q.push(in, 2); q.pop(out, 2, 1); require(out[0] == 0 && out[1] == 0 && out[2] == 1 && out[3] == -1);
    });
    test("Random ring behavior against reference deque", [] {
        FrameQueue q(13); std::deque<std::array<float, 2>> expected; std::mt19937 random(42); float in[80], out[80];
        for (int n = 0; n < 10000; ++n) {
            auto count = random() % 40;
            if (random() % 2) {
                for (unsigned i = 0; i < count; ++i) { in[i * 2] = (random() % 100) / 100.0f; in[i * 2 + 1] = -in[i * 2]; expected.push_back({in[i * 2], in[i * 2 + 1]}); }
                while (expected.size() > 13) expected.pop_front(); q.push(in, count);
            } else {
                q.pop(out, count, .5f);
                for (unsigned i = 0; i < count; ++i) {
                    auto value = expected.empty() ? std::array<float, 2>{0, 0} : expected.front(); if (!expected.empty()) expected.pop_front();
                    require(out[i * 2] == value[0] * .5f && out[i * 2 + 1] == value[1] * .5f);
                }
            }
            require(q.size() == expected.size());
        }
    });
    test("Atomic settings replacement", [] {
        auto path = std::filesystem::current_path() / "artifacts/native/settings-test.txt";
        atomicWrite(path, "old"); atomicWrite(path, "new"); require(readFile(path) == "new"); std::filesystem::remove(path);
    });
    test("Failed atomic replacement preserves previous settings", [] {
        auto path = std::filesystem::current_path() / "artifacts/native/locked-settings-test.txt";
        atomicWrite(path, "original");
        {
            Handle locked(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
            require(locked.get() != INVALID_HANDLE_VALUE); rejects([&] { atomicWrite(path, "replacement"); });
            require(readFile(path) == "original");
        }
        atomicWrite(path, "recovered"); require(readFile(path) == "recovered"); std::filesystem::remove(path);
    });
    test("Fan-out remains independent when another pipe is full or removed", [] {
        AudioBus bus; auto first = std::make_shared<AudioPipe>(8, 100), second = std::make_shared<AudioPipe>(8, 50);
        bus.set({first, second}); float in[] = {.4f, -.4f}, out[2];
        for (int i = 0; i < 20; ++i) { bus.push(in, 1); first->pop(out, 1); require(out[0] == .4f && out[1] == -.4f); }
        second->pop(out, 1); require(out[0] == .4f * volumeGain(50)); second->close(); bus.set({first});
        bus.push(in, 1); first->pop(out, 1); require(out[0] == .4f); second->push(in, 1); second->pop(out, 1); require(out[0] == 0);
    });
    test("Concurrent producer, consumer, volume changes and membership changes", [] {
        AudioBus bus; auto pipe = std::make_shared<AudioPipe>(48, 70); bus.set({pipe});
        std::atomic<bool> done{false};
        std::thread producer([&] { float in[96]{}; for (int i = 0; i < 10000; ++i) bus.push(in, 48); done = true; });
        std::thread consumer([&] { float out[96]; while (!done) pipe->pop(out, 48); });
        for (int i = 0; i < 1000; ++i) { pipe->volume = i % 101; bus.set(i % 2 ? std::vector<std::shared_ptr<AudioPipe>>{pipe} : std::vector<std::shared_ptr<AudioPipe>>{}); }
        producer.join(); consumer.join(); pipe->close(); float out[2]{1, 1}; pipe->pop(out, 1); require(out[0] == 0 && out[1] == 0);
    });
    test("Windows argument quoting and UTF-8", [] {
        require(quoteArgument(L"a b\\") == L"\"a b\\\\\""); require(quoteArgument(L"a\"b") == L"\"a\\\"b\""); require(wide(utf8(L"Kopfhörer 名")) == L"Kopfhörer 名");
    });
    test("Version 1 settings migrate without losing devices or volume; delay persists in version 2", [] {
        auto old = deserialize("BT_AUDIO_SPLITTER 1\n\"cable\"\n1\n\"id\" \"Headphones\" 70 1\n");
        require(old.outputs[0].delayMs == 0 && old.outputs[0].volume == 70 && old.outputs[0].enabled);
        old.outputs[0].delayMs = 235;
        auto restored = deserialize(serialize(old)); require(restored.outputs[0].delayMs == 235);
        for (auto delay : {"-1", "1001", "missing"}) rejects([&] { deserialize(std::string("BT_AUDIO_SPLITTER 2\n\"cable\"\n1\n\"id\" \"Headphones\" 70 1 ") + delay); });
    });
    test("Volume taper is monotonic, quiet at 10 percent, unity at 100 and silent at zero", [] {
        require(volumeGain(0) == 0 && volumeGain(100) == 1 && volumeGain(-1) == 0 && volumeGain(101) == 1);
        require(volumeGain(10) < .002f && volumeGain(50) < .04f);
        for (int i = 1; i <= 100; ++i) require(volumeGain(i) > volumeGain(i - 1));
    });
    test("Low-latency periods respect driver bounds, granularity and the 5-ms floor", [] {
        require(lowLatencyPeriod(48000, 48, 144, 480) == 240);
        require(lowLatencyPeriod(44100, 4, 48, 448) == 224);
        require(lowLatencyPeriod(48000, 48, 384, 480) == 384);
        for (unsigned rate : {44100u, 48000u, 96000u}) for (unsigned step : {1u, 4u, 48u, 128u}) {
            unsigned period = lowLatencyPeriod(rate, step, step, rate / 50);
            require(period % step == 0 && period >= step && period <= rate / 50 && period * 200 >= rate);
        }
        require(!lowLatencyPeriod(48000, 0, 144, 480));
        require(!lowLatencyPeriod(48000, 128, 512, 480));
        require(!lowLatencyPeriod(48000, 128, 128, 200));
    });
    test("Master mute persists without changing individual volume, pause or delay", [] {
        Settings settings{"cable", {{"a", "One", 35, true, 120}, {"b", "Two", 80, false, 0}}};
        settings.muted = true;
        auto restored = deserialize(serialize(settings));
        require(restored.muted && restored.outputs[0].volume == 35 && restored.outputs[0].delayMs == 120 && !restored.outputs[1].enabled);
        restored.muted = false;
        require(restored.outputs[0].volume == 35 && restored.outputs[1].volume == 80);
    });
    test("Device mute survives reload and master unmute without changing slider or other outputs", [] {
        Settings s{"cable", {{"a", "One", 37, true, 120, true}, {"b", "Two", 82, true}}};
        auto restored = deserialize(serialize(s));
        require(restored.outputs[0].muted && restored.outputs[0].volume == 37 && restored.outputs[0].delayMs == 120);
        require(effectiveVolume(restored, restored.outputs[0]) == 0 && effectiveVolume(restored, restored.outputs[1]) == 82);
        restored.muted = true;
        require(effectiveVolume(restored, restored.outputs[1]) == 0);
        restored.muted = false;
        require(effectiveVolume(restored, restored.outputs[0]) == 0);
        restored.outputs[0].muted = false;
        AudioPipe pipe(4, effectiveVolume(restored, restored.outputs[0]));
        float in[]{.5f, -.5f}, out[2]; pipe.push(in, 1); pipe.pop(out, 1);
        require(out[0] == .5f * volumeGain(37) && out[1] == -.5f * volumeGain(37));
        restored.outputs[0].muted = true; pipe.volume = effectiveVolume(restored, restored.outputs[0]);
        pipe.push(in, 1); pipe.pop(out, 1); require(out[0] == 0 && out[1] == 0);
    });
    test("Version 2 gains and master mute migrate; invalid device mute flags are rejected", [] {
        auto migrated = deserialize("BT_AUDIO_SPLITTER 2\n\"cable\"\n1\n\"a\" \"One\" 37 1 120\n1\n");
        require(migrated.muted && !migrated.outputs[0].muted && migrated.outputs[0].volume == 37 && migrated.outputs[0].delayMs == 120);
        for (auto flag : {"-1", "2", "missing"}) rejects([&] {
            deserialize(std::string("BT_AUDIO_SPLITTER 3\n\"cable\"\n1\n\"a\" \"One\" 37 1 120 ") + flag + "\n0\n");
        });
    });
    test("Extra delay preserves stereo frames exactly, including the silent tail", [] {
        AudioPipe pipe(8, 100); pipe.delayFrames(3);
        float pulse[]{.75f, -.25f}, out[2];
        pipe.push(pulse, 1);
        for (int i = 0; i < 3; ++i) { pipe.pop(out, 1); require(out[0] == 0 && out[1] == 0); }
        pipe.pop(out, 1); require(out[0] == .75f && out[1] == -.25f);
        pipe.pop(out, 1); require(out[0] == 0 && out[1] == 0);
    });
    test("Mute applies immediately after delay; close cannot replay delayed samples", [] {
        AudioPipe pipe(8, 100); pipe.delayFrames(2);
        float pulse[]{.75f, -.25f}, out[2]; pipe.push(pulse, 1); pipe.pop(out, 1);
        pipe.volume = 0; pipe.pop(out, 1); pipe.pop(out, 1); require(out[0] == 0 && out[1] == 0);
        pipe.volume = 100; pipe.push(pulse, 1); pipe.pop(out, 1); pipe.close();
        for (int i = 0; i < 4; ++i) { pipe.pop(out, 1); require(out[0] == 0 && out[1] == 0); }
    });
    test("Changing delay clears its old timeline and zero delay passes through", [] {
        AudioPipe pipe(8, 100); pipe.delayFrames(8);
        float pulse[]{.5f, -.5f}, out[2]; pipe.push(pulse, 1); pipe.pop(out, 1);
        pipe.delayFrames(0); pipe.pop(out, 1); require(out[0] == 0);
        pipe.push(pulse, 1); pipe.pop(out, 1); require(out[0] == .5f && out[1] == -.5f);
    });
    test("Spectrum detects a stereo tone without opposite-phase cancellation", [] {
        SpectrumAnalyzer analyzer(48000); std::vector<float> tone(4096);
        for (std::size_t i = 0; i < 2048; ++i) { tone[i * 2] = std::sin(2 * std::numbers::pi_v<float> * 1000 * i / 48000); tone[i * 2 + 1] = -tone[i * 2]; }
        require(analyzer.push(tone.data(), 2048)); auto input = analyzer.bands();
        auto peak = static_cast<std::size_t>(std::max_element(input.begin(), input.end()) - input.begin());
        auto expectedBand = static_cast<std::size_t>(std::log(1000.0f / 40) / std::log(500.0f) * spectrumBands);
        require(peak >= expectedBand - 1 && peak <= expectedBand + 1 && input[peak] > .95f);
        require(outputSpectrum(input, 100, true) == input);
        require(outputSpectrum(input, 0, true) == Spectrum{} && outputSpectrum(input, 100, false) == Spectrum{});
        auto quieter = outputSpectrum(input, 10, true);
        require(quieter[peak] < .3f);
        std::fill(tone.begin(), tone.end(), 0); analyzer.push(tone.data(), 2048); require(analyzer.bands() == Spectrum{});
    });
    std::cout << passed << " passed, " << failed << " failed\n";
    return failed ? 1 : 0;
}
