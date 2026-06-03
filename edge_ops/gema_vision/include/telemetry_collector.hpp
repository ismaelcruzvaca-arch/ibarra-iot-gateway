#pragma once

#include "inference_orchestrator.hpp"
#include "thermal_monitor.hpp"
#include "thread_safe_queue.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Periodic telemetry collector for the RV1106 vision pipeline.
 *
 * Runs an independent thread that wakes every 30 s to sample system and
 * pipeline metrics and publish them as strict JSON to
 * `novamex/vision/telemetry`.
 *
 * ## Collected metrics
 *
 * | Field            | Source                                    |
 * |------------------|-------------------------------------------|
 * | uptime_sec       | std::chrono::steady_clock                  |
 * | fps              | frames_processed delta / 30 s              |
 * | frames_processed | InferenceOrchestrator::frames_processed()   |
 * | frames_dropped   | ThreadSafeQueue::dropped_count()            |
 * | soc_temp_c       | ThermalMonitor::current_state().temp_celsius |
 * | heap_free_kb     | /proc/self/status → VmRSS (resident, not free) |
 * | defects_total    | InferenceOrchestrator cumulative            |
 *
 * ## Thread safety
 *
 * The telemetry loop runs in its own thread.  All reads from the pipeline
 * are atomic or mutex-protected.  The condition_variable wait_for ensures
 * immediate shutdown on SIGTERM without waiting for the full 30 s interval.
 *
 * ## Graceful shutdown
 *
 * Call stop() from the main thread during shutdown.  It signals the
 * condition variable, joins the thread, and returns.
 */
class TelemetryCollector {
public:
    /**
     * @param mqtt        Shared MQTT client for publishing telemetry.
     * @param orchestrator Pipeline orchestrator (provides frames_processed).
     * @param frame_queue  Frame queue (provides dropped_count).
     * @param thermal      SoC thermal monitor.
     * @param device_id    Unique device identifier injected at construction.
     * @param interval     Publish interval (default: 30 seconds).
     */
    explicit TelemetryCollector(
        MqttClient& mqtt,
        const InferenceOrchestrator& orchestrator,
        const ThreadSafeQueue<std::shared_ptr<cv::Mat>>& frame_queue,
        const ThermalMonitor& thermal,
        const std::string& device_id,
        std::chrono::seconds interval = std::chrono::seconds{30}) noexcept;

    ~TelemetryCollector();

    // Non-copiable, non-movable.
    TelemetryCollector(const TelemetryCollector&) = delete;
    TelemetryCollector& operator=(const TelemetryCollector&) = delete;
    TelemetryCollector(TelemetryCollector&&) = delete;
    TelemetryCollector& operator=(TelemetryCollector&&) = delete;

    /** @brief Start the telemetry thread. */
    void start();

    /** @brief Gracefully stop the telemetry thread (joins). */
    void stop();

    /** @brief True if the telemetry thread is running. */
    bool is_running() const noexcept { return running_.load(); }

private:
    /** @brief Main telemetry loop. */
    void collect_loop();

    /** @brief Read VmRSS (kB) from /proc/self/status. */
    uint64_t read_vmrss_kb() const;

    /** @brief Build the telemetry JSON payload. */
    std::string build_payload(
        uint64_t frames_total,
        double fps,
        uint64_t dropped,
        int temp_c,
        uint64_t vmrss_kb,
        uint64_t defects_total,
        uint64_t last_defect_epoch) const;

    // Dependencies (injected, not owned).
    MqttClient& mqtt_;
    const InferenceOrchestrator& orchestrator_;
    const ThreadSafeQueue<std::shared_ptr<cv::Mat>>& frame_queue_;
    const ThermalMonitor& thermal_;
    std::string device_id_;
    std::chrono::seconds interval_;

    // Thread control.
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};
    std::mutex cv_mtx_;
    std::condition_variable cv_;

    // Timestamp of start() for uptime / FPS calculation.
    std::chrono::steady_clock::time_point start_time_;
    uint64_t last_frames_{0};
};

}  // namespace vision
}  // namespace gema
