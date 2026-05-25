/**
 * @file config_manager.cpp
 * @brief RCU-based hot-reload configuration manager.
 */

#include "config_manager.hpp"

#include <cstdio>   // std::rename
#include <fstream>
#include <sstream>
#include <unistd.h> // ::sync()

namespace gema {
namespace vision {

// ===========================================================================
// Construction / destruction
// ===========================================================================

ConfigManager::ConfigManager() noexcept
    : config_(std::make_shared<ConfigState>())
{
}

ConfigManager::~ConfigManager()
{
    stop();
}

// ===========================================================================
// Lifecycle
// ===========================================================================

void ConfigManager::start()
{
    // Load config from disk.
    load_file();

    // Start the async flush thread.
    if (flush_running_.exchange(true)) {
        return;  // already running
    }
    flush_thread_ = std::make_unique<std::thread>(
        &ConfigManager::flush_loop, this);
}

void ConfigManager::stop()
{
    // Flush any pending write before stopping.
    {
        std::lock_guard<std::mutex> lock(flush_mtx_);
        flush_pending_ = false;
    }

    if (!flush_running_.exchange(false)) {
        return;
    }

    flush_cv_.notify_one();
    if (flush_thread_ && flush_thread_->joinable()) {
        flush_thread_->join();
    }
    flush_thread_.reset();

    // Final flush to disk.
    flush_to_disk();
}

// ===========================================================================
// Write path — hot-reload
// ===========================================================================

bool ConfigManager::apply_json(const std::string& json_str)
{
    auto new_state = parse_json(json_str);
    if (!new_state) {
        return false;
    }

    // Inherit any fields not present in the JSON from the current config.
    auto cur = std::atomic_load(&config_);
    if (new_state->mqtt.host.empty())  new_state->mqtt.host  = cur->mqtt.host;
    if (new_state->mqtt.port == 0)     new_state->mqtt.port  = cur->mqtt.port;
    if (new_state->inference_topic.empty()) new_state->inference_topic = cur->inference_topic;
    if (new_state->telemetry_topic.empty()) new_state->telemetry_topic = cur->telemetry_topic;

    // RCU swap — RAM first, readers see new config immediately.
    std::atomic_store(&config_, new_state);

    // Schedule async flush with 2s debounce.
    {
        std::lock_guard<std::mutex> lock(flush_mtx_);
        flush_pending_ = true;
    }
    flush_cv_.notify_one();

    return true;
}

bool ConfigManager::load_file(const std::string& path)
{
    config_path_ = path;

    std::ifstream f(path);
    if (!f.is_open()) {
        // File doesn't exist yet — use defaults silently.
        return false;
    }

    std::stringstream buf;
    buf << f.rdbuf();
    return apply_json(buf.str());
}

// ===========================================================================
// Async flush with debounce
// ===========================================================================

void ConfigManager::flush_loop()
{
    while (flush_running_.load(std::memory_order_acquire)) {
        std::unique_lock<std::mutex> lock(flush_mtx_);
        flush_cv_.wait_for(lock, std::chrono::seconds(2), [this]() {
            return flush_pending_ || !flush_running_.load();
        });

        if (!flush_running_.load()) {
            break;
        }

        if (flush_pending_) {
            flush_pending_ = false;
            lock.unlock();
            flush_to_disk();
            lock.lock();
        }
    }
}

void ConfigManager::flush_to_disk()
{
    if (config_path_.empty()) return;

    // Ensure directory exists.
    auto pos = config_path_.rfind('/');
    if (pos != std::string::npos) {
        std::string dir = config_path_.substr(0, pos);
        std::string mkdir_cmd = "mkdir -p " + dir;
        [[maybe_unused]] int rc = std::system(mkdir_cmd.c_str());
    }

    // Write to a temp file, then atomic rename.
    std::string tmp_path = config_path_ + ".tmp";
    {
        std::ofstream f(tmp_path);
        if (!f.is_open()) return;
        f << serialize();
        f.flush();
    }

    std::rename(tmp_path.c_str(), config_path_.c_str());
    ::sync();
}

// ===========================================================================
// JSON serialisation (minimal — no external dependency)
// ===========================================================================

std::string ConfigManager::serialize() const
{
    auto cfg = std::atomic_load(&config_);

    std::ostringstream json;
    json << "{\n";
    json << "  \"mqtt_host\": \""     << cfg->mqtt.host << "\",\n";
    json << "  \"mqtt_port\": "       << cfg->mqtt.port << ",\n";
    json << "  \"camera_index\": "    << cfg->camera_index << ",\n";
    json << "  \"inference_interval_ms\": " << cfg->inference_interval_ms << ",\n";
    json << "  \"watchdog_interval_sec\": " << cfg->watchdog_interval_sec << ",\n";
    json << "  \"mode\": \""          << cfg->mode << "\",\n";
    json << "  \"inference_topic\": \"" << cfg->inference_topic << "\",\n";
    json << "  \"telemetry_topic\": \"" << cfg->telemetry_topic << "\",\n";
    json << "  \"color_thresholds\": [\n";
    for (size_t i = 0; i < cfg->color_thresholds.size(); ++i) {
        const auto& ct = cfg->color_thresholds[i];
        json << "    {\n";
        json << "      \"class_id\": "  << ct.class_id << ",\n";
        json << "      \"hue_low\": "   << ct.hue_low << ",\n";
        json << "      \"hue_high\": "  << ct.hue_high << ",\n";
        json << "      \"sat_min\": "   << ct.sat_min << ",\n";
        json << "      \"val_min\": "   << ct.val_min << ",\n";
        json << "      \"pass_ratio\": " << ct.pass_ratio << "\n";
        json << "    }";
        if (i + 1 < cfg->color_thresholds.size()) json << ",";
        json << "\n";
    }
    json << "  ]\n";
    json << "}\n";
    return json.str();
}

std::shared_ptr<ConfigState> ConfigManager::parse_json(
    const std::string& json) const
{
    auto cfg = std::make_shared<ConfigState>();

    // Minimal hand-written parser for flat JSON with known keys.
    // Expects:  "key": value  or  "key": "string"
    auto extract_str = [&](const std::string& key) -> std::string {
        auto search = "\"" + key + "\"";
        auto pos = json.find(search);
        if (pos == std::string::npos) return {};
        pos = json.find(':', pos + search.size());
        if (pos == std::string::npos) return {};
        pos = json.find_first_not_of(" \t\r\n", pos + 1);
        if (pos == std::string::npos) return {};
        if (json[pos] == '"') {
            auto start = pos + 1;
            auto end   = json.find('"', start);
            if (end == std::string::npos) return {};
            return json.substr(start, end - start);
        }
        // Number — read until comma or newline
        auto end = json.find_first_of(",\n\r}", pos);
        if (end == std::string::npos) end = json.size();
        return json.substr(pos, end - pos);
    };

    auto extract_int = [&](const std::string& key, int default_val) -> int {
        auto s = extract_str(key);
        if (s.empty()) return default_val;
        try { return std::stoi(s); } catch (...) { return default_val; }
    };

    // Parse top-level fields.
    std::string mqtt_host = extract_str("mqtt_host");
    if (!mqtt_host.empty()) cfg->mqtt.host = mqtt_host;
    cfg->mqtt.port              = extract_int("mqtt_port", 1883);
    cfg->camera_index           = extract_int("camera_index", 0);
    cfg->inference_interval_ms  = extract_int("inference_interval_ms", 666);
    cfg->watchdog_interval_sec  = extract_int("watchdog_interval_sec", 5);

    std::string mode = extract_str("mode");
    if (!mode.empty()) cfg->mode = mode;

    std::string inf_topic = extract_str("inference_topic");
    if (!inf_topic.empty()) cfg->inference_topic = inf_topic;

    std::string tel_topic = extract_str("telemetry_topic");
    if (!tel_topic.empty()) cfg->telemetry_topic = tel_topic;

    // TODO: parse color_thresholds array when the JSON format is finalised.

    return cfg;
}

}  // namespace vision
}  // namespace gema
