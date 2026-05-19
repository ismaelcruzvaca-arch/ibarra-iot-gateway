#pragma once
#include <atomic>
#include <thread>
#include <chrono>

namespace vision {
namespace watchdog {

class Watchdog {
public:
    Watchdog();
    ~Watchdog();

    void start();
    void stop();
    void heartbeat();

private:
    void monitorLoop();

    std::atomic<long long> last_heartbeat_{0};
    std::atomic<bool> running_{false};
    std::thread thread_;
    const long long TIMEOUT_MS = 5000;
};

} // namespace watchdog
} // namespace vision
