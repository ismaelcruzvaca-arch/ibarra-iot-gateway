/**
 * @file telemetry_collector.cpp
 * @brief Periodic telemetry collector implementation.
 *
 * Collects system and pipeline metrics every 30 s and publishes them
 * as strict JSON via MQTT to `novamex/vision/telemetry`.
 */

#include "telemetry_collector.hpp"

#include <fstream>
#include <sstream>
#include <string>

namespace gema {
namespace vision {

// ===========================================================================
// Construction / destruction
// ===========================================================================

TelemetryCollector::TelemetryCollector(
    MqttClient& mqtt,
    const InferenceOrchestrator& orchestrator,
    const ThreadSafeQueue<std::shared_ptr<cv::Mat>>& frame_queue,
    const ThermalMonitor& thermal,
    const std::string& device_id,
    std::chrono::seconds interval) noexcept
    : mqtt_(mqtt)
    , orchestrator_(orchestrator)
    , frame_queue_(frame_queue)
    , thermal_(thermal)
    , device_id_(device_id)
    , interval_(interval)
{
}

TelemetryCollector::~TelemetryCollector()
{
    stop();
}

// ===========================================================================
// Lifecycle
// ===========================================================================

void TelemetryCollector::start()
{
    if (running_.exchange(true)) {
        return;  // already running — idempotent
    }

    start_time_ = std::chrono::steady_clock::now();
    last_frames_ = orchestrator_.frames_processed();

    thread_ = std::make_unique<std::thread>(&TelemetryCollector::collect_loop, this);
}

void TelemetryCollector::stop()
{
    if (!running_.exchange(false)) {
        return;  // already stopped — idempotent
    }

    // Wake the thread immediately from cv::wait_for.
    {
        std::lock_guard<std::mutex> lock(cv_mtx_);
        cv_.notify_one();
    }

    if (thread_ && thread_->joinable()) {
        thread_->join();
    }
}

// ===========================================================================
// Metrics collection
// ===========================================================================

uint64_t TelemetryCollector::read_vmrss_kb() const
{
    std::ifstream proc_status("/proc/self/status");
    if (!proc_status.is_open()) {
        return 0;
    }

    std::string line;
    while (std::getline(proc_status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            // Line format: "VmRSS:   12345 kB"
            std::istringstream iss(line.substr(6));
            uint64_t value = 0;
            std::string unit;
            iss >> value >> unit;
            return value;
        }
    }

    return 0;
}

// ===========================================================================
// JSON payload formatting
// ===========================================================================

std::string TelemetryCollector::build_payload(
    uint64_t frames_total,
    double fps,
    uint64_t dropped,
    int temp_c,
    uint64_t vmrss_kb,
    uint64_t defects_total,
    uint64_t /* last_defect_epoch */) const
{
    auto now = std::chrono::steady_clock::now();
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
        now - start_time_).count();

    // Build ISO-8601 timestamp from system clock.
    std::time_t t = std::time(nullptr);
    std::tm tm_buf;
    char ts_buf[32];
    gmtime_r(&t, &tm_buf);
    std::strftime(ts_buf, sizeof(ts_buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
    std::string timestamp(ts_buf);

    // Determine node_health per spec: soc_temp_c > 75 → WARNING
    const char* node_health = (temp_c > 75) ? "WARNING" : "ONLINE";

    // Strict JSON — no trailing commas, all values typed.
    // Unified schema: device_id, device_type, timestamp, node_health, metrics[]
    std::ostringstream json;
    json << "{"
         << "\"device_id\":\"" << device_id_ << "\","
         << "\"device_type\":\"camera\","
         << "\"timestamp\":\"" << timestamp << "\","
         << "\"node_health\":\"" << node_health << "\","
         << "\"metrics\":["
         << "{\"name\":\"uptime_sec\",\"value\":" << uptime << ",\"unit\":\"s\"},"
         << "{\"name\":\"fps\",\"value\":" << fps << "},"
         << "{\"name\":\"frames_processed\",\"value\":" << frames_total << "},"
         << "{\"name\":\"frames_dropped\",\"value\":" << dropped << "},"
         << "{\"name\":\"soc_temp_c\",\"value\":" << temp_c << ",\"unit\":\"\\u00b0C\"},"
         << "{\"name\":\"heap_resident_kb\",\"value\":" << vmrss_kb << ",\"unit\":\"kB\"},"
         << "{\"name\":\"defects_total\",\"value\":" << defects_total << "}"
         << "]}";

    return json.str();
}

// ===========================================================================
// Main telemetry loop
// ===========================================================================

void TelemetryCollector::collect_loop()
{
    while (running_.load(std::memory_order_acquire)) {
        // ---- Sleep with interruptible wait --------------------------------
        {
            std::unique_lock<std::mutex> lock(cv_mtx_);
            cv_.wait_for(lock, interval_, [this]() {
                return !running_.load(std::memory_order_relaxed);
            });
        }

        if (!running_.load(std::memory_order_acquire)) {
            break;  // shutdown requested during sleep
        }

        // ---- Collect metrics ---------------------------------------------
        uint64_t frames_now = orchestrator_.frames_processed();
        uint64_t delta = frames_now - last_frames_;
        last_frames_ = frames_now;

        double fps = 0.0;
        if (interval_.count() > 0) {
            fps = static_cast<double>(delta) / interval_.count();
        }

        uint64_t dropped = frame_queue_.dropped_count();
        int temp_c = thermal_.current_state().temp_celsius;
        uint64_t vmrss_kb = read_vmrss_kb();
        uint64_t defects = orchestrator_.defects_total();
        uint64_t last_defect_epoch = orchestrator_.last_defect_epoch();

        // ---- Build and publish -------------------------------------------
        std::string payload = build_payload(
            frames_now, fps, dropped, temp_c, vmrss_kb,
            defects, last_defect_epoch);

        mqtt_.publish("novamex/vision/telemetry", payload);
    }
}

}  // namespace vision
}  // namespace gema
