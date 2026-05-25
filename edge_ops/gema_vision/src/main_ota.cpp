/**
 * @file main_ota.cpp
 * @brief GEMA Vision OTA Update Agent — Daemon Entry Point
 *
 * Standalone daemon that listens for OTA commands via a named FIFO and
 * drives the OtaManager state machine.
 *
 * ## Architecture
 *
 * The gema-vision daemon (or any MQTT listener) receives the OTA command
 * on `novamex/vision/ota/update` and writes the JSON payload to:
 *
 *     /userdata/ota_tmp/ota_cmd.fifo
 *
 * This daemon reads from the FIFO, calls OtaManager::on_ota_command(),
 * and publishes status updates back via the MQTT callback.
 *
 * ## Signals
 *
 *   SIGTERM / SIGINT  → graceful shutdown
 *
 * ## Resource constraints
 *
 *   - OOMScoreAdjust = -900  (lower priority than gema-vision at -1000)
 *   - LimitNOFILE = 1024     (enough for FIFO + logging)
 *
 * @note In production, the MQTT callback should publish to the real MQTT
 *       broker.  The current implementation logs to stderr (journald).
 */

#include "ota_manager.hpp"

#include <sys/stat.h>     // mkfifo()
#include <sys/types.h>

#include <csignal>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>

// ---------------------------------------------------------------------------
// Global state for async-signal-safe shutdown
// ---------------------------------------------------------------------------

namespace {
    std::unique_ptr<gema::vision::OtaManager> g_ota_manager;
    std::atomic<bool>                         g_shutdown{false};
}  // anonymous namespace

extern "C" void signal_handler(int sig)
{
    const char msg[] = "[gema-ota] Signal received — shutting down\n";
    if (::write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {
        // ignore
    }
    g_shutdown.store(true, std::memory_order_release);
    std::_Exit(128 + sig);
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

int main(int, char**)
{
    constexpr const char* kFifoPath = "/userdata/ota_tmp/ota_cmd.fifo";

    // ---- Signal handling -------------------------------------------------
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT,  signal_handler);

    // ---- Ensure required directories exist --------------------------------
    // Use a [[maybe_unused]] variable to suppress GCC's warn_unused_result.
    [[maybe_unused]] int rc = ::system("mkdir -p /userdata/ota_tmp /backup /userdata/bin 2>/dev/null");

    // ---- Create the command FIFO -----------------------------------------
    // Remove any stale FIFO from a previous run, then create a new one.
    ::unlink(kFifoPath);
    if (::mkfifo(kFifoPath, 0666) != 0) {
        const char err[] = "[gema-ota] FATAL: Cannot create FIFO — continuing anyway\n";
        if (::write(STDERR_FILENO, err, sizeof(err) - 1) < 0) { /* ignore */ }
        // Non-fatal — OTA can still be triggered programmatically.
    }

    // ---- Create OTA manager ----------------------------------------------
    g_ota_manager = std::make_unique<gema::vision::OtaManager>(
        [](const std::string& topic, const std::string& payload) {
            // In production, replace this lambda with a real MQTT publish
            // via the MqttClient interface.
            std::string log = "[gema-ota] " + topic + ": " + payload + "\n";
            if (::write(STDOUT_FILENO, log.c_str(), log.size()) < 0) {
                /* ignore */
            }
        }
    );

    {
        std::string msg = "[gema-ota] Daemon started (PID="
                        + std::to_string(::getpid()) + ")\n";
        if (::write(STDOUT_FILENO, msg.c_str(), msg.size()) < 0) {
            /* ignore */
        }
    }

    // ---- Main loop: read commands from FIFO ------------------------------
    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::ifstream fifo(kFifoPath);
        if (fifo.is_open()) {
            std::string line;
            while (std::getline(fifo, line)) {
                if (!line.empty() && !g_shutdown.load()) {
                    g_ota_manager->on_ota_command(line);
                }
            }
        }

        // Small sleep to avoid busy-waiting if the FIFO is empty.
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    return 0;
}
