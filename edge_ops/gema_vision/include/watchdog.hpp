#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Hardware watchdog timer (WDT) manager for the RV1106 SoC.
 *
 * Encapsulates the Linux `/dev/watchdog` API with a dedicated keepalive
 * thread.  The hardware watchdog will reset the entire SoC if the
 * keepalive thread stops pinging — this is the **last line of defence**
 * against software hangs, kernel panics, or OOM deadlocks.
 *
 * ## Architecture
 *
 * ```
 * Application main
 *       │
 *       ├── InferenceOrchestrator  (consumer thread)
 *       ├── FlashTrigger           (producer thread)
 *       └── Watchdog               (keepalive thread)  ←  NEW
 * ```
 *
 * The Watchdog thread runs independently at a fixed interval
 * (default: 5 s).  If the application crashes or hangs, the keepalive
 * stops and the hardware resets the board after the WDT timeout
 * (typically 15 s).
 *
 * ## Production usage (RV1106)
 *
 * ```cpp
 * Watchdog wd("/dev/watchdog");
 * wd.start();
 * // ... run application ...
 * wd.stop();   // sends magic close 'V' — disarms watchdog
 * ```
 *
 * ## Testability
 *
 * The device path is injected via the constructor, so SIL tests can
 * point to a regular file or a named pipe:
 *
 * ```cpp
 * // SIL test: verify keepalive pings at the right frequency
 * Watchdog wd("/tmp/fake_watchdog", std::chrono::seconds(1));
 * wd.start();
 * std::this_thread::sleep_for(2500ms);
 * wd.stop();
 * EXPECT_GE(wd.ping_count(), 2);
 * ```
 *
 * @note The magic close character ('V') is written to the device
 *       before closing the fd so that normal shutdown does NOT
 *       trigger a hardware reset.  However, if the process is
 *       killed with SIGKILL (e.g. OOM killer), the fd is never
 *       closed and the watchdog remains armed — which is the
 *       CORRECT behaviour for an industrial safety net.
 */
class Watchdog {
public:
    /**
     * @brief Construct a watchdog manager.
     *
     * @param device_path         Path to the watchdog device
     *                            (default: "/dev/watchdog").
     * @param keepalive_interval  How often to ping the watchdog
     *                            (default: 5 s).
     *
     * @note The device is NOT opened until start() is called.
     *       This allows construction before daemonisation /
     *       privilege drop.
     */
    explicit Watchdog(
        std::string device_path = "/dev/watchdog",
        std::chrono::milliseconds keepalive_interval = std::chrono::seconds{5}) noexcept;

    /// RAII — calls stop() to disarm the watchdog.
    ~Watchdog();

    // Non-copiable, non-movable.
    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;
    Watchdog(Watchdog&&) = delete;
    Watchdog& operator=(Watchdog&&) = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /**
     * @brief Open the watchdog device and start the keepalive thread.
     *
     * @return true  if the device was opened and the thread started.
     * @return false if the device could not be opened (logged).
     */
    bool start();

    /**
     * @brief Stop the keepalive thread and disarm the watchdog.
     *
     * Sends the magic close character ('V') to prevent an unwanted
     * reset during normal shutdown, then closes the device fd.
     *
     * Safe to call multiple times (idempotent).
     */
    void stop();

    // ------------------------------------------------------------------
    // Runtime status
    // ------------------------------------------------------------------

    /** @brief True if the keepalive thread is running. */
    bool is_running() const noexcept { return running_.load(); }

    /** @brief Total number of keepalive pings sent since start(). */
    uint64_t ping_count() const noexcept { return ping_count_.load(); }

    // ------------------------------------------------------------------
    // Manual control (useful for integration tests)
    // ------------------------------------------------------------------

    /**
     * @brief Send a single keepalive ping to the watchdog device.
     *
     * Writes a byte (typically 0x0A) to the device fd.
     *
     * @return true if the write succeeded, false otherwise.
     */
    bool ping();

private:
    /** @brief Thread function: loop that calls ping() at the configured interval. */
    void keepalive_loop();

    // Configuration.
    std::string                device_path_;
    std::chrono::milliseconds  interval_;

    // Device fd (owned by this instance).
    int fd_ = -1;

    // Thread and control.
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool>            running_{false};
    std::atomic<uint64_t>        ping_count_{0};
};

}  // namespace vision
}  // namespace gema
