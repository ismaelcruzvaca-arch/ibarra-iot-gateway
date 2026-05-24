#include "watchdog.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cstring>
#include <system_error>

namespace gema {
namespace vision {

// ===========================================================================
// Lifetime
// ===========================================================================

Watchdog::Watchdog(
    std::string device_path,
    std::chrono::milliseconds keepalive_interval) noexcept
    : device_path_(std::move(device_path))
    , interval_(keepalive_interval)
{}

Watchdog::~Watchdog()
{
    stop();
}

// ===========================================================================
// Lifecycle
// ===========================================================================

bool Watchdog::start()
{
    if (running_.exchange(true)) {
        return true;  // already running — idempotent
    }

    // Open the watchdog device.
    // O_CLOEXEC ensures the fd is not leaked to child processes.
    fd_ = ::open(device_path_.c_str(), O_WRONLY | O_CLOEXEC);
    if (fd_ < 0) {
        running_.store(false);
        // Not a fatal error — log and continue.
        // On production RV1106 this MUST succeed, but during development
        // or in CI the device may not exist.
        return false;
    }

    ping_count_.store(0);

    // Launch the keepalive thread.
    thread_ = std::make_unique<std::thread>(
        &Watchdog::keepalive_loop, this);

    return true;
}

void Watchdog::stop()
{
    if (!running_.exchange(false)) {
        return;  // already stopped — idempotent
    }

    // Wait for the keepalive thread to exit.
    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
    thread_.reset();

    // Magic close: write 'V' before closing so the watchdog is
    // disarmed during normal shutdown.  This prevents an unwanted
    // reset when the process exits cleanly.
    if (fd_ >= 0) {
        const char magic = 'V';
        if (::write(fd_, &magic, 1) < 0) {
            // Magic close failed — the watchdog will remain armed and
            // the board will reset after the timeout.  This is acceptable
            // during abnormal termination but should not happen during
            // a normal shutdown.
        }
        ::close(fd_);
        fd_ = -1;
    }
}

// ===========================================================================
// Keepalive
// ===========================================================================

bool Watchdog::ping()
{
    if (fd_ < 0) {
        return false;
    }

    // Write a single byte (any value) to reset the WDT timer.
    // The kernel driver interprets any write as a keepalive.
    const char data = 0x0A;
    ssize_t ret = ::write(fd_, &data, 1);
    if (ret != 1) {
        return false;
    }

    ping_count_.fetch_add(1);
    return true;
}

// ===========================================================================
// Private: keepalive thread
// ===========================================================================

void Watchdog::keepalive_loop()
{
    // Sleep in 100 ms chunks so that stop() is responsive even with
    // long keepalive intervals.
    auto total_ms = interval_.count();
    auto chunk_ms = std::min(total_ms, static_cast<decltype(total_ms)>(100));
    int iterations = static_cast<int>(total_ms / chunk_ms);

    while (running_.load()) {
        ping();

        for (int i = 0; i < iterations; ++i) {
            if (!running_.load()) {
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(chunk_ms));
        }
    }
}

}  // namespace vision
}  // namespace gema
