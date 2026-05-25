#pragma once

#include "inference_orchestrator.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

// ===========================================================================
// ConfigState — immutable snapshot of configuration
// ===========================================================================

/**
 * @brief Immutable configuration snapshot shared via RCU pointer.
 *
 * Readers (InferenceOrchestrator consumer loop, TelemetryCollector)
 * hold a std::shared_ptr<const ConfigState> obtained from
 * ConfigManager::current().  The ConfigManager atomically swaps the
 * pointer when a new config is applied — readers never see a
 * half-updated state.
 */
struct ConfigState {
    /** @brief MQTT broker connection (secrets excluded — stored in .env). */
    struct MqttConfig {
        std::string host = "localhost";
        int         port = 1883;
    };

    /** @brief HSV colour threshold for a single class_id. */
    struct ColorThreshold {
        int   class_id = 0;
        int   hue_low  = 0;
        int   hue_high = 10;
        int   sat_min  = 70;
        int   val_min  = 50;
        float pass_ratio = 0.80f;
    };

    MqttConfig                  mqtt;
    int                         camera_index         = 0;
    int                         inference_interval_ms = 666;  // ~1.5 Hz
    int                         watchdog_interval_sec = 5;
    std::string                 mode                 = "inspection";  // inspection | calibration | idle
    std::vector<ColorThreshold> color_thresholds;
    std::string                 inference_topic  = "novamex/vision/inference";
    std::string                 telemetry_topic  = "novamex/vision/telemetry";
    std::string                 config_topic     = "novamex/vision/config/update";
};

// ===========================================================================
// ConfigManager — RCU-based hot-reload
// ===========================================================================

/**
 * @brief Thread-safe configuration manager with hot-reload via MQTT.
 *
 * ## RCU (Read-Copy-Update) pattern
 *
 * The active config lives in an atomically-swapped
 * `std::shared_ptr<const ConfigState>`.  Readers (the inference loop,
 * telemetry, etc.) grab the pointer once per frame — no mutex in the
 * hot path.
 *
 * ## Write path (MQTT hot-reload)
 *
 *   apply_json(json)
 *     → parse new ConfigState
 *     → atomic_store(&config_, new_state)       ← RAM first, 0 alloc in hot path
 *     → schedule_async_flush()                  ← disk write after 2s debounce
 *
 * ## Startup
 *
 *   start()
 *     → load /userdata/vision/config.json
 *     → atomic_store(&config_, parsed)
 *     → (no disk write needed — already on disk)
 */
class ConfigManager {
public:
    ConfigManager() noexcept;
    ~ConfigManager();

    // Non-copiable, non-movable.
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;
    ConfigManager(ConfigManager&&) = delete;
    ConfigManager& operator=(ConfigManager&&) = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /** @brief Load config from disk and start the async flush thread. */
    void start();

    /** @brief Stop the async flush thread. */
    void stop();

    // ------------------------------------------------------------------
    // Read path (RCU)
    // ------------------------------------------------------------------

    /**
     * @brief Atomically get the current config snapshot.
     *
     * Safe to call from any thread.  The returned shared_ptr pins the
     * config version until it goes out of scope.  No mutex involved
     * in the read path.
     */
    std::shared_ptr<const ConfigState> current() const noexcept
    {
        return std::atomic_load(&config_);
    }

    // ------------------------------------------------------------------
    // Write path (hot-reload)
    // ------------------------------------------------------------------

    /**
     * @brief Apply a configuration update from a JSON string.
     *
     * Parses the JSON, creates a new ConfigState, swaps the RCU
     * pointer (RAM first), and schedules an async disk flush with
     * 2-second debounce.
     *
     * @param json_str  Strict JSON payload.
     * @return true if parsing and applying succeeded.
     */
    bool apply_json(const std::string& json_str);

    /**
     * @brief Load configuration from a JSON file.
     *
     * @param path  Filesystem path (default: /userdata/vision/config.json).
     * @return true if file was read and parsed successfully.
     */
    bool load_file(const std::string& path = "/userdata/vision/config.json");

private:
    /** @brief Serialise current config to JSON string. */
    std::string serialize() const;

    /** @brief Flush current config to disk (called from flush thread). */
    void flush_to_disk();

    /** @brief Debounced flush loop. */
    void flush_loop();

    /** @brief Parse a JSON string into a new ConfigState. */
    std::shared_ptr<ConfigState> parse_json(const std::string& json) const;

    // RCU pointer — the ONE source of truth for all readers.
    std::shared_ptr<ConfigState> config_;

    // Serialisation for the write path (only one writer at a time).
    mutable std::mutex write_mtx_;

    // Async flush thread with debounce.
    std::unique_ptr<std::thread> flush_thread_;
    std::atomic<bool>            flush_running_{false};
    std::mutex                   flush_mtx_;
    std::condition_variable      flush_cv_;
    bool                         flush_pending_{false};

    // Config file path.
    std::string config_path_;
};

}  // namespace vision
}  // namespace gema
