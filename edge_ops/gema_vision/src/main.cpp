/**
 * @file main.cpp
 * @brief GEMA Vision Inference Pipeline — Daemon Entry Point
 *
 * Production entry point for the RV1106.  Initialises:
 *   1. Hardware watchdog timer (WDT)
 *   2. MQTT client
 *   3. Inference engine (RknnContext / DataCollectorEngine)
 *   4. Flash trigger (GPIO)
 *   5. Inference orchestrator (consumer thread)
 *
 * Currently a minimal skeleton — each component will be wired as
 * the rest of the pipeline is implemented.
 *
 * ## Signals
 *
 *   SIGTERM / SIGINT  → graceful shutdown (disarms watchdog)
 *   SIGKILL           → hardware watchdog resets the SoC
 *
 * The signal handler is async-signal-safe: it ONLY writes to a
 * file descriptor (STDERR_FILENO) and sets an atomic flag.  No
 * std::cerr, no mutexes, no heap — those are NOT safe in signals.
 *
 * @note The hardware WDT runs independently in a dedicated thread.
 *       If the main processing loop hangs, the keepalive stops and
 *       the WDT resets the entire board after ~15 s.
 */

#include "watchdog.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>
#include <unistd.h>

// ---------------------------------------------------------------------------
// Global state for async-signal-safe shutdown
// ---------------------------------------------------------------------------

namespace {
    std::unique_ptr<gema::vision::Watchdog> g_watchdog;
    std::atomic<bool>                       g_shutdown{false};
}  // anonymous namespace

extern "C" void signal_handler(int sig)
{
    // write() is async-signal-safe — std::cerr is NOT.
    const char msg[] = "[gema-vision] Signal received — shutting down\n";
    if (::write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {
        // ignore — can't do anything in a signal handler
    }

    g_shutdown.store(true, std::memory_order_release);

    if (g_watchdog) {
        // stop() disarms the watchdog (magic close 'V') so the board
        // does NOT reset during a clean shutdown.
        g_watchdog->stop();
    }

    // _Exit() is async-signal-safe; exit() is not.
    std::_Exit(128 + sig);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int, char**)
{
    // ---- Configuration (from env vars) -----------------------------------
    // Capture getenv result ONCE to avoid TOCTOU.
    const char* wd_env = std::getenv("GEMA_WATCHDOG_DEVICE");
    std::string watchdog_dev = wd_env ? wd_env : "/dev/watchdog";

    int watchdog_interval = 5;  // seconds
    if (auto* env = std::getenv("GEMA_WATCHDOG_INTERVAL_SEC")) {
        char* end = nullptr;
        long val = std::strtol(env, &end, 10);
        if (end != env && val >= 1 && val <= 300) {
            watchdog_interval = static_cast<int>(val);
        }
    }

    // ---- Signal handling -------------------------------------------------
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // ---- Watchdog --------------------------------------------------------
    if (::write(STDOUT_FILENO, "Starting hardware watchdog...\n", 30) < 0) {
        /* ignore */
    }

    g_watchdog = std::make_unique<gema::vision::Watchdog>(
        watchdog_dev,
        std::chrono::seconds(watchdog_interval));

    if (!g_watchdog->start()) {
        const char warn[] = "WARNING: Could not open watchdog device — continuing\n";
        if (::write(STDERR_FILENO, warn, sizeof(warn) - 1) < 0) {
            /* ignore */
        }
        g_watchdog.reset();
    }

    // ---- Main loop (placeholder) -----------------------------------------
    {
        std::string msg = "GEMA Vision daemon started (PID="
                        + std::to_string(::getpid()) + ")\n";
        if (::write(STDOUT_FILENO, msg.c_str(), msg.size()) < 0) {
            /* ignore */
        }
    }

    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}
