#pragma once
#include "platform.hpp"
#include <condition_variable>
#include <functional>
#include <map>
#include <mutex>
#include <thread>

namespace splitter {
struct Endpoint {
    std::string id, name;
    bool available = false, cable = false, handsFree = false, present = true;
};
struct RouteView { Output preference; RouteState state = RouteState::Paused; std::string error; };
struct Snapshot {
    Settings settings;
    std::vector<Endpoint> devices;
    std::vector<RouteView> routes;
    std::string sourceName, error;
    bool sourceReady = false, suspended = false, devicesChecked = false, driverInstalled = false;
};
std::vector<Endpoint> enumerateOutputs();
void renameCable(const std::string& id);
void audioSelfTest(const std::filesystem::path& report);

// Commands are copied across threads. COM interfaces and audio queues remain on the MTA worker.
// GUI snapshots contain only values; the UI never waits for a Bluetooth audio driver.
class AudioWorker {
    std::mutex mutex_;
    std::condition_variable wake_;
    std::vector<std::function<void(Settings&, bool&)>> commands_;
    Snapshot snapshot_;
    bool stop_ = false;
    std::thread thread_;
    void run() noexcept;
public:
    AudioWorker();
    ~AudioWorker();
    AudioWorker(const AudioWorker&) = delete; AudioWorker& operator=(const AudioWorker&) = delete;
    Snapshot snapshot();
    void command(std::function<void(Settings&, bool&)> operation);
};
} // namespace splitter
