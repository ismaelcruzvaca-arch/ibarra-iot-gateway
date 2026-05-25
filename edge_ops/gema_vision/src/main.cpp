/**
 * @file main.cpp
 * @brief GEMA Vision Inference Pipeline — Daemon Entry Point
 *
 * Production entry point for the RV1106.  Initialises:
 *   1. Hardware watchdog timer (WDT)
 *   2. Frame pool and thread-safe queue
 *   3. Video producer (mock or RGA, selected at compile time)
 *   4. Inference engine (DataCollectorEngine)
 *   5. Inference orchestrator (consumer thread)
 *
 * ## Signals
 *
 *   SIGTERM / SIGINT  → sets g_shutdown flag (async-signal-safe)
 *   SIGKILL           → hardware watchdog resets the SoC
 *
 * The signal handler is MINIMAL and async-signal-safe: it ONLY
 * writes to a file descriptor (STDERR_FILENO) and sets an atomic
 * flag.  No _Exit(), no method calls, no heap — those are NOT safe
 * in signals.  The main loop observes g_shutdown and performs a
 * clean shutdown in the correct order.
 *
 * ## Shutdown order
 *
 *   orchestrator → producer → (queue already shut down by orchestrator)
 *   → watchdog
 *
 * ## Producer selection
 *
 *   #define USE_MOCK_PRODUCER  → MockVideoProducer (SIL tests, x86)
 *   no define                  → RgaVideoProducer (RV1106 production)
 *
 * @note The hardware WDT runs independently in a dedicated thread.
 *       If the main processing loop hangs, the keepalive stops and
 *       the WDT resets the entire board after ~15 s.
 */

#include "frame_pool.hpp"
#include "inference_orchestrator.hpp"
#include "data_collector_engine.hpp"
#include "postproc_dispatcher.hpp"
#include "spatial_calibrator.hpp"
#include "telemetry_collector.hpp"
#include "thermal_monitor.hpp"
#include "thread_safe_queue.hpp"
#include "watchdog.hpp"

#ifdef USE_MOCK_PRODUCER
#include "mock_video_producer.hpp"
#else
#include "rga_video_producer.hpp"
#endif

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
    std::atomic<bool> g_shutdown{false};
}  // anonymous namespace

extern "C" void signal_handler(int /*sig*/)
{
    // write() is async-signal-safe — std::cerr is NOT.
    const char msg[] = "[gema-vision] Signal received — shutting down\n";
    if (::write(STDERR_FILENO, msg, sizeof(msg) - 1) < 0) {
        // ignore — can't do anything in a signal handler
    }

    g_shutdown.store(true, std::memory_order_release);

    // NOTE: Do NOT call _Exit() or watchdog.stop() here.
    // _Exit() bypasses destructors, leaving the queue in an
    // inconsistent state.  watchdog.stop() is NOT async-signal-safe.
    // The main loop will see g_shutdown and perform a clean
    // reverse-order shutdown.
}

// ---------------------------------------------------------------------------
// Null MQTT client — no-op implementation for the initial skeleton
// ---------------------------------------------------------------------------

class NullMqttClient final : public gema::vision::MqttClient {
public:
    bool publish(const std::string& /*topic*/,
                 const std::string& /*payload*/) override
    {
        return true;
    }
};

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

    // ---- Watchdog (start first) ------------------------------------------
    if (::write(STDOUT_FILENO, "Starting hardware watchdog...\n", 30) < 0) {
        /* ignore */
    }

    auto watchdog = std::make_unique<gema::vision::Watchdog>(
        watchdog_dev,
        std::chrono::seconds(watchdog_interval));

    if (!watchdog->start()) {
        const char warn[] = "WARNING: Could not open watchdog device — continuing\n";
        if (::write(STDERR_FILENO, warn, sizeof(warn) - 1) < 0) {
            /* ignore */
        }
        watchdog.reset();
    }

    // ---- Pipeline components ---------------------------------------------

    // 1. Frame pool (640 × 480, BGR, 4 pre-allocated buffers).
    auto pool = std::make_unique<gema::vision::FramePool>(640, 480, CV_8UC3);

    // 2. Thread-safe frame queue (bounded to 8 frames).
    auto queue = std::make_unique<
        gema::vision::ThreadSafeQueue<std::shared_ptr<cv::Mat>>>(8);

    // 3. MQTT client (null implementation — real MQTT wired later).
    auto mqtt = std::make_unique<NullMqttClient>();

    // 4. Inference engine (data-collector mode — captures calibration
    //    dataset; no NPU inference yet).
    auto engine = std::make_unique<gema::vision::DataCollectorEngine>(
        mqtt.get());

    // 5. Spatial calibrator (uncalibrated — no-op until homography loaded).
    auto calibrator = std::make_unique<gema::vision::SpatialCalibrator>();

    // 6. Post-processing dispatcher (no OCR/color engines configured yet).
    auto dispatcher = std::make_unique<gema::vision::PostProcessDispatcher>(
        nullptr, nullptr);

    // ---- Video producer --------------------------------------------------

#ifdef USE_MOCK_PRODUCER
    if (::write(STDOUT_FILENO, "Using MockVideoProducer (1.5 Hz)\n", 33) < 0) {
        /* ignore */
    }
    auto producer = std::make_unique<gema::vision::MockVideoProducer>(
        *pool, *queue, 1.5);
#else
    if (::write(STDOUT_FILENO, "Using RgaVideoProducer\n", 23) < 0) {
        /* ignore */
    }
    auto producer = std::make_unique<gema::vision::RgaVideoProducer>(
        *pool, *queue);
#endif

    // ---- Inference orchestrator (consumer thread) ------------------------
    auto orchestrator = std::make_unique<gema::vision::InferenceOrchestrator>(
        *engine, *queue, *mqtt, *calibrator, *dispatcher, 0);

    // ---- Thermal monitor -------------------------------------------------
    auto thermal = std::make_unique<gema::vision::ThermalMonitor>();

    // ---- Telemetry collector (publishes every 30 s) ----------------------
    auto telemetry = std::make_unique<gema::vision::TelemetryCollector>(
        *mqtt, *orchestrator, *queue, *thermal);

    // ---- Start sequence --------------------------------------------------
    // Order: watchdog → producer → orchestrator → telemetry.
    // (watchdog already started above.)

    producer->start();
    orchestrator->start();
    telemetry->start();

    // ---- Log startup -----------------------------------------------------
    {
        std::string msg = "GEMA Vision daemon started (PID="
                        + std::to_string(::getpid()) + ")\n";
        if (::write(STDOUT_FILENO, msg.c_str(), msg.size()) < 0) {
            /* ignore */
        }
    }

    // ---- Main loop -------------------------------------------------------
    // Poll g_shutdown every 100 ms.  Signal handler sets the flag;
    // the loop exit triggers clean reverse-order shutdown below.
    while (!g_shutdown.load(std::memory_order_acquire)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // ---- Shutdown sequence (REVERSE order) --------------------------------
    // producer → orchestrator → telemetry → watchdog.
    //
    // 1. producer.stop()    — stop filling the queue (close the valve)
    // 2. orchestrator.stop() — drain remaining frames + queue.shutdown()
    // 3. telemetry.stop()    — publish final state after pipeline is idle
    // 4. watchdog.stop()     — disarm WDT (last, resets if we hang)

    if (producer) {
        producer->stop();
    }

    if (orchestrator) {
        orchestrator->stop();
    }

    if (telemetry) {
        telemetry->stop();
    }

    // queue.shutdown() was already called inside orchestrator.stop().
    // Calling it again is harmless (idempotent), but unnecessary.

    if (watchdog) {
        // Disarm WDT so the board does NOT reset during shutdown.
        watchdog->stop();
    }

    return 0;
}
