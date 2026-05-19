#include "watchdog.hpp"
#include <iostream>
#include <cstdlib>

namespace vision {
namespace watchdog {

Watchdog::Watchdog() {}

Watchdog::~Watchdog() {
    stop();
}

void Watchdog::start() {
    running_ = true;
    heartbeat(); // Initialize heartbeat
    thread_ = std::thread(&Watchdog::monitorLoop, this);
}

void Watchdog::stop() {
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void Watchdog::heartbeat() {
    auto now = std::chrono::steady_clock::now();
    last_heartbeat_ = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
}

void Watchdog::monitorLoop() {
    while (running_) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
        
        auto now = std::chrono::steady_clock::now();
        long long current_time = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
        
        if (current_time - last_heartbeat_ > TIMEOUT_MS) {
            std::cerr << "[CRITICAL ERROR] Watchdog triggered: Main loop hung for > 5000ms. Aborting process." << std::endl;
            std::exit(1); // Force abort to allow Docker to restart the container
        }
    }
}

} // namespace watchdog
} // namespace vision
