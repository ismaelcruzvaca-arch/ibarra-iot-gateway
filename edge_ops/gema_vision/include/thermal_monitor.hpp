#pragma once

#include <chrono>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Thermal monitor for the RV1106 SoC.
 *
 * Reads the on-die temperature sensor once per interval and drives a
 * graceful-degradation ladder to prevent the thermal death spiral:
 *
 *   NPU runs YOLO → heat → if unchecked → 95 °C → TSHUT hardware reset
 *
 * ## Escalera de degradación
 *
 * | Temp      | Acción                                        |
 * |-----------|-----------------------------------------------|
 * | ≥ 65 °C   | Log / publish to MQTT                         |
 * | ≥ 70 °C   | Reduce NPU max frequency via sysfs             |
 * | ≥ 75 °C   | Switch YOLO input to 320×320 (4× less NPU work) |
 * | ≥ 80 °C   | Skip every other frame                         |
 * | ≥ 85 °C   | Stop inference (emergency)                     |
 * | ≥ 95 °C   | TSHUT — hardware reset (cannot prevent)       |
 *
 * ## Thread safety
 *
 * The monitor runs its own internal thread.  `current_state()` is
 * thread-safe via mutex.
 */
class ThermalMonitor {
public:
    /// Severity levels matching the degradation ladder.
    enum class Level : uint8_t {
        OK        = 0,
        WARM      = 1,   // ≥ 65 °C — log
        HOT       = 2,   // ≥ 70 °C — throttle NPU
        HOTTER    = 3,   // ≥ 75 °C — reduce resolution
        CRITICAL  = 4,   // ≥ 80 °C — skip frames
        EMERGENCY = 5    // ≥ 85 °C — stop
    };

    struct State {
        int   temp_celsius;
        double rate_celsius_per_sec;  // dT/dt
        Level level;
    };

    /// Callback invoked when the thermal level changes.
    using DegradeCallback = void (*)(const State& state, void* user_data);

    /**
     * @param zone_path   Path to the thermal zone sysfs file.
     *                    Default: "/sys/class/thermal/thermal_zone0/temp".
     * @param interval    Polling interval (default: 5 s).
     * @param callback    Optional callback for level transitions.
     * @param user_data   Opaque pointer passed to the callback.
     */
    explicit ThermalMonitor(
        std::string zone_path = "/sys/class/thermal/thermal_zone0/temp",
        std::chrono::seconds interval = std::chrono::seconds{5},
        DegradeCallback callback = nullptr,
        void* user_data = nullptr) noexcept;

    ~ThermalMonitor();

    // Non-copiable, non-movable.
    ThermalMonitor(const ThermalMonitor&) = delete;
    ThermalMonitor& operator=(const ThermalMonitor&) = delete;
    ThermalMonitor(ThermalMonitor&&) = delete;
    ThermalMonitor& operator=(ThermalMonitor&&) = delete;

    /** @brief Start the monitoring thread. */
    bool start();

    /** @brief Stop the monitoring thread. */
    void stop();

    /** @brief Most recently sampled state. */
    State current_state() const;

private:
    void monitor_loop();
    int  read_temp();
    Level evaluate_level(int temp_celsius, double rate) const;

    std::string zone_path_;
    std::chrono::seconds interval_;
    DegradeCallback callback_;
    void* user_data_;

    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> running_{false};

    mutable std::mutex state_mtx_;
    State state_{0, 0.0, Level::OK};
    int    prev_temp_{25};
    std::chrono::steady_clock::time_point prev_time_;
};

// ---------------------------------------------------------------------------
// Threshold constants (shared between ThermalMonitor and application code)
// ---------------------------------------------------------------------------

namespace thermal_thresholds {
    constexpr int MONITOR_LOG      = 65;
    constexpr int WARN_THROTTLE    = 70;
    constexpr int ACTION_REDUCE_RES = 75;
    constexpr int ACTION_SKIP      = 80;
    constexpr int EMERGENCY_STOP   = 85;
    constexpr int TSHUT_HARDWARE   = 95;

    // Rate of change indicative of thermal runaway
    constexpr double SPIRAL_RATE_CPS = 0.5;

    // Hysteresis — restore previous level only after cooling this much
    constexpr int HYSTERESIS = 5;
}  // namespace thermal_thresholds

}  // namespace vision
}  // namespace gema
