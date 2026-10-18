#include "audio.hpp"
#include "pipe.hpp"
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ks.h>
#include <ksmedia.h>
#include <avrt.h>
#include <cstring>
#include <chrono>

namespace splitter {
namespace {
using Clock = std::chrono::steady_clock;
Com<IMMDeviceEnumerator> enumerator() {
    Com<IMMDeviceEnumerator> e;
    check(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(e.put())), "Audiogeräte öffnen");
    return e;
}
Com<IMMDevice> device(const std::string& id) {
    auto e = enumerator(); Com<IMMDevice> d;
    check(e->GetDevice(wide(id).c_str(), d.put()), "Audiogerät finden"); return d;
}
std::string property(IPropertyStore* store, const PROPERTYKEY& key) {
    PROPVARIANT value{};
    HRESULT hr = store->GetValue(key, &value);
    std::string result;
    if (SUCCEEDED(hr) && value.vt == VT_LPWSTR && value.pwszVal) result = utf8(value.pwszVal);
    PropVariantClear(&value); return result;
}
bool contains(std::string text, const std::string& needle) {
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text.find(needle) != std::string::npos;
}
const Endpoint* find(const std::vector<Endpoint>& list, const std::string& id) {
    auto it = std::find_if(list.begin(), list.end(), [&](const auto& d) { return d.id == id; });
    return it == list.end() ? nullptr : &*it;
}
Com<IAudioClient> client(IMMDevice* d) {
    Com<IAudioClient> c;
    check(d->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(c.put())), "Audioverbindung öffnen"); return c;
}
WAVEFORMATEX floatFormat(DWORD rate) {
    return {WAVE_FORMAT_IEEE_FLOAT, 2, rate, rate * 8, 8, 32, 0};
}

class Source {
    Com<IAudioClient> client_;
    Com<IAudioCaptureClient> capture_;
    std::unique_ptr<WAVEFORMATEX, CoMemory> format_;
    bool floating_ = false;
    std::vector<float> scratch_;
public:
    DWORD rate() const { return format_->nSamplesPerSec; }
    explicit Source(const std::string& id) {
        auto d = device(id); client_ = client(d.get());
        WAVEFORMATEX* raw = nullptr;
        check(client_->GetMixFormat(&raw), "Eingangsformat lesen"); format_.reset(raw);
        auto tag = raw->wFormatTag;
        if (tag == WAVE_FORMAT_EXTENSIBLE && raw->cbSize >= 22) {
            auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(raw);
            if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT)) tag = WAVE_FORMAT_IEEE_FLOAT;
            else if (IsEqualGUID(ext->SubFormat, KSDATAFORMAT_SUBTYPE_PCM)) tag = WAVE_FORMAT_PCM;
        }
        floating_ = tag == WAVE_FORMAT_IEEE_FLOAT;
        if (raw->nChannels != 2) throw std::runtime_error("Audioeingang in Windows bitte auf Stereo (2 Kanäle) stellen.");
        if ((!floating_ && tag != WAVE_FORMAT_PCM) || (floating_ && raw->wBitsPerSample != 32) ||
            (!floating_ && raw->wBitsPerSample != 16 && raw->wBitsPerSample != 24 && raw->wBitsPerSample != 32) ||
            raw->nBlockAlign != 2 * (raw->wBitsPerSample / 8) || rate() < 8000 || rate() > 192000)
            throw std::runtime_error("Eingangsformat nicht unterstützt. Bitte Stereo, 48 kHz wählen.");
        check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_NOPERSIST,
            800000, 0, raw, nullptr), "Audioeingang starten");
        check(client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(capture_.put())), "Loopback öffnen");
        UINT32 capacity = 0; check(client_->GetBufferSize(&capacity), "Eingangspuffer lesen"); scratch_.resize(capacity * 2);
        check(client_->Start(), "Audioeingang aktivieren");
    }
    ~Source() { if (client_) client_->Stop(); }
    template<class Callback> void pump(Callback&& receive) {
        UINT32 frames = 0;
        // Limit work per iteration, so unusually busy input cannot starve output or UI commands.
        for (int packet = 0; packet < 32; ++packet) {
            check(capture_->GetNextPacketSize(&frames), "Audioeingang wurde getrennt");
            if (!frames) break;
            BYTE* data = nullptr; DWORD flags = 0;
            check(capture_->GetBuffer(&data, &frames, &flags, nullptr, nullptr), "Audioeingang lesen");
            struct Release { IAudioCaptureClient* c; UINT32 n; ~Release() { c->ReleaseBuffer(n); } } release{capture_.get(), frames};
            if (frames * 2 > scratch_.size()) throw std::runtime_error("Unerwartete Audiopaketgröße.");
            if (flags & AUDCLNT_BUFFERFLAGS_SILENT) std::fill_n(scratch_.data(), frames * 2, 0.0f);
            else if (floating_) std::memcpy(scratch_.data(), data, frames * 8);
            else {
                int bytes = format_->wBitsPerSample / 8;
                for (std::size_t i = 0; i < frames * 2; ++i) {
                    const BYTE* p = data + i * bytes;
                    if (bytes == 2) { std::int16_t v; std::memcpy(&v, p, 2); scratch_[i] = v / 32768.0f; }
                    else if (bytes == 3) { std::int32_t v = (p[0] << 8) | (p[1] << 16) | (static_cast<std::uint32_t>(p[2]) << 24); scratch_[i] = v / 2147483648.0f; }
                    else { std::int32_t v; std::memcpy(&v, p, 4); scratch_[i] = v / 2147483648.0f; }
                }
            }
            receive(scratch_.data(), frames);
        }
    }
};

class Sink {
    Com<IAudioClient> client_;
    Com<IAudioRenderClient> render_;
    UINT32 bufferFrames_ = 0;
    std::shared_ptr<AudioPipe> pipe_;
public:
    Sink(const std::string& id, DWORD rate, int volume, std::shared_ptr<AudioPipe> pipe = {}) : pipe_(pipe ? std::move(pipe) : std::make_shared<AudioPipe>(rate * 180 / 1000, volume)) {
        auto d = device(id); client_ = client(d.get());
        auto format = floatFormat(rate);
        // Windows negotiates each device independently; its high-quality shared-mode resampler
        // handles differing sample rates. No microphone/communications stream is ever opened.
        check(client_->Initialize(AUDCLNT_SHAREMODE_SHARED,
            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM | AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY | AUDCLNT_STREAMFLAGS_NOPERSIST,
            800000, 0, &format, nullptr), "Stereo-Ausgabe öffnen");
        check(client_->GetBufferSize(&bufferFrames_), "Ausgangspuffer lesen");
        check(client_->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void**>(render_.put())), "Audioausgabe öffnen");
        BYTE* buffer = nullptr;
        check(render_->GetBuffer(bufferFrames_, &buffer), "Audioausgabe vorbereiten");
        check(render_->ReleaseBuffer(bufferFrames_, AUDCLNT_BUFFERFLAGS_SILENT), "Audioausgabe vorbereiten");
        check(client_->Start(), "Audioausgabe aktivieren");
    }
    ~Sink() { if (client_) client_->Stop(); }
    void volume(int volume) { pipe_->volume = volume; }
    void write(const float* samples, UINT32 frames) { pipe_->push(samples, frames); }
    void pump() {
        UINT32 padding = 0;
        check(client_->GetCurrentPadding(&padding), "Ausgabe wurde getrennt");
        if (padding >= bufferFrames_) return;
        UINT32 frames = bufferFrames_ - padding;
        BYTE* buffer = nullptr;
        check(render_->GetBuffer(frames, &buffer), "Audioausgabe schreiben");
        pipe_->pop(reinterpret_cast<float*>(buffer), frames);
        check(render_->ReleaseBuffer(frames, 0), "Audioausgabe abschließen");
    }
};

// Each driver owns a separate MTA thread. Activation, device loss or a stalled driver on
// one output cannot prevent another output from servicing its WASAPI buffer.
class Session {
    std::mutex errorMutex_, waitMutex_;
    std::string error_;
    std::condition_variable wake_;
protected:
    std::atomic<bool> stop_{false};
    std::thread thread_;
    template<class F> void start(F work) {
        thread_ = std::thread([this, work = std::move(work)] {
            try {
                ComApartment apartment;
                DWORD index = 0; HANDLE task = AvSetMmThreadCharacteristicsW(L"Audio", &index);
                struct Priority { HANDLE task; ~Priority() { if (task) AvRevertMmThreadCharacteristics(task); } } priority{task};
                work();
            } catch (const std::exception& e) { std::lock_guard lock(errorMutex_); error_ = e.what(); }
        });
    }
    void wait() { std::unique_lock lock(waitMutex_); wake_.wait_for(lock, std::chrono::milliseconds(5), [&] { return stop_.load(); }); }
    void stop() { stop_ = true; wake_.notify_one(); if (thread_.joinable()) thread_.join(); }
public:
    virtual ~Session() { stop(); }
    void pump() { std::lock_guard lock(errorMutex_); if (!error_.empty()) throw std::runtime_error(error_); }
};
class CaptureSession : public Session {
    std::atomic<DWORD> rate_{0};
public:
    CaptureSession(const std::string& id, std::shared_ptr<AudioBus> bus) {
        start([this, id, bus = std::move(bus)] {
            Source source(id); rate_ = source.rate();
            while (!stop_) { source.pump([&](const float* data, UINT32 frames) { bus->push(data, frames); }); wait(); }
        });
    }
    ~CaptureSession() override { stop(); }
    DWORD rate() const { return rate_.load(); }
};
class OutputSession : public Session {
    std::shared_ptr<AudioPipe> pipe_;
    std::atomic<bool> ready_{false};
public:
    OutputSession(const std::string& id, DWORD rate, int volume) : pipe_(std::make_shared<AudioPipe>(rate * 180 / 1000, volume)) {
        start([this, id, rate, volume] {
            Sink sink(id, rate, volume, pipe_); ready_ = true;
            while (!stop_) { sink.pump(); wait(); }
        });
    }
    ~OutputSession() override { pipe_->close(); stop(); }
    auto pipe() const { return pipe_; }
    bool ready() const { return ready_; }
    void volume(int value) { pipe_->volume = value; }
};

class Engine {
    Settings settings_;
    std::vector<Endpoint> devices_;
    std::shared_ptr<AudioBus> bus_ = std::make_shared<AudioBus>();
    std::unique_ptr<CaptureSession> source_;
    std::map<std::string, std::unique_ptr<OutputSession>> sinks_;
    std::map<std::string, std::string> routeErrors_;
    std::string sourceId_, sourceError_, warning_, saveError_, lastSaved_;
    bool suspended_ = false;
public:
    Engine() { settings_ = loadSettings(warning_); lastSaved_ = serialize(settings_); }
    void commands(std::vector<std::function<void(Settings&, bool&)>>& list) {
        for (auto& operation : list) {
            try { operation(settings_, suspended_); }
            catch (const std::exception& e) { warning_ = e.what(); logError(e.what()); }
        }
    }
    void enumerate() {
        devices_ = enumerateOutputs();
        if (settings_.source.empty()) {
            auto it = std::find_if(devices_.begin(), devices_.end(), [](const auto& d) { return d.cable && d.available && !contains(d.name, "16"); });
            if (it != devices_.end()) settings_.source = it->id;
        }
        for (auto& o : settings_.outputs) if (const auto* d = find(devices_, o.id)) o.name = d->name;
    }
    void reconcile(bool allowRetry) {
        const auto* input = find(devices_, settings_.source);
        const bool inputOK = input && input->available && input->cable && !suspended_;
        bool wanted = false;
        for (const auto& o : settings_.outputs) {
            const auto* d = find(devices_, o.id);
            if (decide(o.enabled, d && !d->cable && !d->handsFree, d && d->available, inputOK) == RouteState::Playing) wanted = true;
        }
        if (!inputOK || !wanted || sourceId_ != settings_.source) {
            sinks_.clear(); source_.reset(); sourceId_ = settings_.source;
        }
        if (inputOK && wanted && !source_ && allowRetry) {
            try { source_ = std::make_unique<CaptureSession>(settings_.source, bus_); sourceError_.clear(); }
            catch (const std::exception& e) { sourceError_ = e.what(); logError(e.what()); }
        }
        for (auto it = sinks_.begin(); it != sinks_.end();) {
            auto o = std::find_if(settings_.outputs.begin(), settings_.outputs.end(), [&](const auto& p) { return p.id == it->first; });
            const auto* d = find(devices_, it->first);
            if (!source_ || o == settings_.outputs.end() || decide(o->enabled, d && !d->cable && !d->handsFree, d && d->available, inputOK) != RouteState::Playing) it = sinks_.erase(it);
            else { it->second->volume(o->volume); ++it; }
        }
        for (const auto& o : settings_.outputs) {
            const auto* d = find(devices_, o.id);
            if (!source_ || !source_->rate() || decide(o.enabled, d && !d->cable && !d->handsFree, d && d->available, inputOK) != RouteState::Playing || !allowRetry || sinks_.contains(o.id)) continue;
            try { sinks_[o.id] = std::make_unique<OutputSession>(o.id, source_->rate(), o.volume); routeErrors_.erase(o.id); }
            catch (const std::exception& e) { routeErrors_[o.id] = e.what(); logError(e.what()); }
        }
        std::vector<std::shared_ptr<AudioPipe>> pipes;
        for (auto& [id, sink] : sinks_) pipes.push_back(sink->pipe());
        bus_->set(std::move(pipes));
    }
    void pump() {
        if (source_) {
            try { source_->pump(); }
            catch (const std::exception& e) { sourceError_ = e.what(); logError(e.what()); sinks_.clear(); source_.reset(); }
        }
        for (auto it = sinks_.begin(); it != sinks_.end();) {
            try { it->second->pump(); ++it; }
            catch (const std::exception& e) { routeErrors_[it->first] = e.what(); logError(e.what()); it = sinks_.erase(it); }
        }
    }
    void save() {
        auto data = serialize(settings_);
        if (data == lastSaved_) return;
        try { atomicWrite(dataDirectory() / L"settings.txt", data); lastSaved_ = data; saveError_.clear(); }
        catch (const std::exception& e) { saveError_ = e.what(); }
    }
    Snapshot snapshot() {
        Snapshot s; s.settings = settings_; s.devices = devices_; s.suspended = suspended_;
        const auto* input = find(devices_, settings_.source);
        s.sourceReady = input && input->available && input->cable;
        s.sourceName = input ? input->name : "VB-CABLE noch nicht verfügbar";
        s.error = !saveError_.empty() ? saveError_ : !sourceError_.empty() && input && input->available ? sourceError_ : warning_;
        for (const auto& o : settings_.outputs) {
            const auto* d = find(devices_, o.id);
            RouteView r{o, decide(o.enabled, o.id != settings_.source && (!d || (!d->cable && !d->handsFree)), d && d->available, s.sourceReady && !suspended_), {}};
            if (r.state == RouteState::Playing && (!sinks_.contains(o.id) || !sinks_.at(o.id)->ready())) {
                r.state = RouteState::WaitingSource;
                if (routeErrors_.contains(o.id)) r.error = routeErrors_[o.id];
            }
            s.routes.push_back(std::move(r));
        }
        return s;
    }
};
} // namespace

std::vector<Endpoint> enumerateOutputs() {
    auto e = enumerator(); Com<IMMDeviceCollection> collection;
    check(e->EnumAudioEndpoints(eRender, DEVICE_STATEMASK_ALL, collection.put()), "Audiogeräte auflisten");
    UINT count = 0; check(collection->GetCount(&count), "Audiogeräte zählen");
    std::vector<Endpoint> result;
    for (UINT i = 0; i < count; ++i) {
        try {
            Com<IMMDevice> d; check(collection->Item(i, d.put()), "Audiogerät lesen");
            LPWSTR raw = nullptr; check(d->GetId(&raw), "Gerätekennung lesen");
            std::unique_ptr<wchar_t, CoMemory> id(raw);
            DWORD state = 0; check(d->GetState(&state), "Gerätestatus lesen");
            Com<IPropertyStore> props; check(d->OpenPropertyStore(STGM_READ, props.put()), "Gerätenamen lesen");
            auto name = property(props.get(), PKEY_Device_FriendlyName);
            auto hardware = property(props.get(), PKEY_DeviceInterface_FriendlyName);
            bool cable = contains(hardware, "vb-audio virtual cable");
            bool handsFree = contains(name, "hands-free") || contains(name, "handsfree") || contains(name, "freisprech") || contains(name, "ag audio");
            result.push_back({utf8(raw), name, state == DEVICE_STATE_ACTIVE, cable, handsFree});
        } catch (const std::exception&) { /* Removal may race enumeration; retry on the next tick. */ }
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.available != b.available) return a.available > b.available;
        return a.name != b.name ? a.name < b.name : a.id < b.id;
    });
    return result;
}
void renameCable(const std::string& id) {
    const auto endpoints = enumerateOutputs();
    const auto* endpoint = find(endpoints, id);
    if (!endpoint || !endpoint->cable) throw std::runtime_error("Es kann nur der VB-CABLE-Eingang umbenannt werden.");
    auto d = device(id); Com<IPropertyStore> store;
    check(d->OpenPropertyStore(STGM_READWRITE, store.put()), "Windows-Namen bearbeiten");
    PROPVARIANT value{}; value.vt = VT_LPWSTR; value.pwszVal = const_cast<LPWSTR>(L"Audio Splitter");
    check(store->SetValue(PKEY_Device_FriendlyName, value), "Windows-Namen setzen");
    check(store->Commit(), "Windows-Namen speichern");
}
AudioWorker::AudioWorker() : thread_([this] { run(); }) {}
AudioWorker::~AudioWorker() {
    { std::lock_guard lock(mutex_); stop_ = true; } wake_.notify_one();
    if (thread_.joinable()) thread_.join();
}
Snapshot AudioWorker::snapshot() { std::lock_guard lock(mutex_); return snapshot_; }
void AudioWorker::command(std::function<void(Settings&, bool&)> operation) {
    { std::lock_guard lock(mutex_); commands_.push_back(std::move(operation)); } wake_.notify_one();
}
void AudioWorker::run() noexcept {
    try {
        ComApartment apartment;
        Engine engine;
        auto enumerationAt = Clock::now(), publishAt = Clock::now(), saveAt = Clock::now();
        while (true) {
            std::vector<std::function<void(Settings&, bool&)>> commands;
            bool stopped;
            { std::lock_guard lock(mutex_); commands.swap(commands_); stopped = stop_; }
            engine.commands(commands);
            if (stopped) { engine.save(); break; }
            auto now = Clock::now(); bool retry = now >= enumerationAt;
            std::string error;
            if (retry) {
                try { engine.enumerate(); }
                catch (const std::exception& e) { error = e.what(); logError(e.what()); }
                enumerationAt = now + std::chrono::seconds(1);
            }
            engine.reconcile(retry); engine.pump();
            if (now >= saveAt) { engine.save(); saveAt = now + std::chrono::milliseconds(250); }
            if (!commands.empty() || now >= publishAt) {
                auto s = engine.snapshot(); if (!error.empty()) s.error = error;
                { std::lock_guard lock(mutex_); snapshot_ = std::move(s); }
                publishAt = now + std::chrono::milliseconds(200);
            }
            std::unique_lock lock(mutex_);
            wake_.wait_for(lock, std::chrono::milliseconds(5), [&] { return stop_ || !commands_.empty(); });
        }
    } catch (const std::exception& e) {
        logError(e.what()); std::lock_guard lock(mutex_); snapshot_.error = e.what();
    }
}
void audioSelfTest(const std::filesystem::path& report) {
    ComApartment apartment;
    auto devices = enumerateOutputs();
    auto it = std::find_if(devices.begin(), devices.end(), [](const auto& d) { return d.available && !d.handsFree && !d.cable; });
    if (it == devices.end()) throw std::runtime_error("Kein aktives Audiogerät für den Selbsttest.");
    Source input(it->id);
    Sink first(it->id, input.rate(), 0), second(it->id, input.rate() == 48000 ? 44100 : 48000, 0);
    std::vector<float> silence(19200, 0); std::size_t captured = 0;
    auto until = Clock::now() + std::chrono::seconds(2);
    while (Clock::now() < until) {
        input.pump([&](const float*, UINT32 frames) { captured += frames; });
        // This explicit self-test only submits silence; captured user audio is never replayed or stored.
        first.write(silence.data(), 240); second.write(silence.data(), 240);
        first.pump(); second.pump(); Sleep(5);
    }
    if (!captured) throw std::runtime_error("Loopback-Selbsttest lieferte keine Frames.");
    auto bus = std::make_shared<AudioBus>();
    CaptureSession capture(it->id, bus);
    std::vector<std::unique_ptr<OutputSession>> outputs;
    for (int i = 0; i < 5; ++i) outputs.push_back(std::make_unique<OutputSession>(it->id, 48000, 0));
    OutputSession invalid("intentionally-invalid-endpoint", 48000, 0);
    bool failureIsolated = false; int replacements = 0;
    auto start = Clock::now(); until = start + std::chrono::seconds(4);
    while (Clock::now() < until) {
        capture.pump();
        for (auto& output : outputs) { output->pump(); output->pipe()->push(silence.data(), 240); }
        try { invalid.pump(); } catch (const std::exception&) { failureIsolated = true; }
        if (Clock::now() - start > std::chrono::seconds(replacements + 1) && replacements < 2) {
            outputs[2].reset(); outputs[2] = std::make_unique<OutputSession>(it->id, 48000, 0); ++replacements;
        }
        Sleep(5);
    }
    if (!failureIsolated || !capture.rate() || std::any_of(outputs.begin(), outputs.end(), [](const auto& o) { return !o->ready(); }))
        throw std::runtime_error("Thread-/Ausfalltest fehlgeschlagen.");
    atomicWrite(report, "PASS: Native WASAPI loopback, sample-rate conversion, five independent output threads, failed endpoint isolation and output recreation.\nCaptured frames (not stored): " + std::to_string(captured) + "\n");
}
} // namespace splitter
