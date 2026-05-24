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
 * ## Usage
 *
 *   gema-vision                     # run as daemon
 *   gema-vision --help              # show options
 *
 * ## Signals
 *
 *   SIGTERM / SIGINT  → graceful shutdown (disarms watchdog)
 *   SIGKILL           → hardware watchdog resets the SoC
 *
 * @note The hardware WDT runs independently in a dedicated thread.
 *       If the main processing loop hangs, the keepalive stops and
 *       the WDT resets the entire board after ~15 s.
 */

#include "watchdog.hpp"

#include <chrono>
#include <cstdlib>
#include <csignal>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Global watchdog pointer for signal handler
// ---------------------------------------------------------------------------

namespace {
std::unique_ptr<gema::vision::Watchdog> g_watchdog;
}  // anonymous namespace

extern "C" void signal_handler(int sig)
{
    std::cerr << "Signal " << sig << " received — shutting down" << std::endl;
    if (g_watchdog) {
        g_watchdog->stop();
    }
    std::_Exit(128 + sig);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int, char**)
{
    // ---- Configuration (from env vars) -----------------------------------
    std::string watchdog_dev =
        std::getenv("GEMA_WATCHDOG_DEVICE")
            ? std::getenv("GEMA_WATCHDOG_DEVICE")
            : "/dev/watchdog";

    int watchdog_interval = 5;  // seconds
    if (auto* env = std::getenv("GEMA_WATCHDOG_INTERVAL_SEC")) {
        watchdog_interval = std::atoi(env);
        if (watchdog_interval < 1) watchdog_interval = 5;
    }

    // ---- Signal handling -------------------------------------------------
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // ---- Watchdog --------------------------------------------------------
    std::cout << "Starting hardware watchdog (" << watchdog_dev
              << ", interval=" << watchdog_interval << " s)..." << std::endl;

    g_watchdog = std::make_unique<gema::vision::Watchdog>(
        watchdog_dev,
        std::chrono::seconds(watchdog_interval));  // still takes seconds, converts to ms

    if (!g_watchdog->start()) {
        std::cerr << "WARNING: Could not open " << watchdog_dev
                  << " — continuing without hardware watchdog" << std::endl;
        g_watchdog.reset();
    }

    // ---- Main loop (placeholder) -----------------------------------------
    std::cout << "GEMA Vision daemon started (PID=" << getpid() << ")" << std::endl;

    // Keep the process alive until signalled.
    // In production this would be the orchestrator consumer loop.
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    // Unreachable (signals handle shutdown).
    return 0;
}
