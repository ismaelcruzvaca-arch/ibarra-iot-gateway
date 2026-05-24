#include "data_collector_engine.hpp"
#include "inference_orchestrator.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <thread>

namespace gema {
namespace vision {

// ===========================================================================
// Lifetime
// ===========================================================================

DataCollectorEngine::DataCollectorEngine(
    MqttClient* mqtt,
    std::string output_dir) noexcept
    : mqtt_(mqtt)
    , output_dir_(std::move(output_dir))
{
    // Ensure the output path has a trailing separator.
    if (!output_dir_.empty() && output_dir_.back() != '/') {
        output_dir_.push_back('/');
    }
}

// ===========================================================================
// InferenceEngine interface
// ===========================================================================

bool DataCollectorEngine::load_model(const std::string& /*path*/)
{
    // Data collection needs no model — always succeeds.
    return true;
}

PrimitiveBatch DataCollectorEngine::infer(const cv::Mat& frame)
{
    if (mode_.load() == Mode::CALIBRATION) {
        save_frame(frame);
    }
    // No inference — return empty batch regardless of mode.
    return PrimitiveBatch{};
}

// ===========================================================================
// Mode control
// ===========================================================================

void DataCollectorEngine::set_mode(Mode new_mode)
{
    Mode old_mode = mode_.exchange(new_mode);

    if (new_mode == Mode::CALIBRATION) {
        // Reset capture tracking for this calibration run.
        capture_count_.store(0);
        batch_id_.fetch_add(1);

        // Ensure the output directory exists.
        std::filesystem::create_directories(output_dir_);

        publish_status("calibration_started");
    } else {
        // IDLE — publish completion only if we were actually capturing.
        if (old_mode == Mode::CALIBRATION) {
            int captured = capture_count_.load();
            std::ostringstream msg;
            msg << "calibration_complete:" << captured;
            publish_status(msg.str());
        }
    }
}

// ===========================================================================
// Frame capture
// ===========================================================================

void DataCollectorEngine::save_frame(const cv::Mat& frame)
{
    int current = capture_count_.load();
    if (current >= kCaptureLimit) {
        // Hard limit reached — auto-switch to IDLE.
        set_mode(Mode::IDLE);
        return;
    }

    // Build the output file path.
    std::string filename = build_filename();

    // Write JPEG with quality 95.
    std::vector<int> params;
    params.push_back(cv::IMWRITE_JPEG_QUALITY);
    params.push_back(kJpegQuality);

    if (!cv::imwrite(filename, frame, params)) {
        // Disk full or path invalid — abort calibration.
        publish_status("error:write_failed");
        set_mode(Mode::IDLE);
        return;
    }

    // fsync() to ensure data survives a power loss.
    int fd = ::open(filename.c_str(), O_RDONLY);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    // Also fsync the directory to ensure the directory entry is persisted.
    int dir_fd = ::open(output_dir_.c_str(), O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        ::fsync(dir_fd);
        ::close(dir_fd);
    }

    // Increment counter.
    int new_count = capture_count_.fetch_add(1) + 1;

    // Publish progress every 10 frames (avoid flooding MQTT).
    if (new_count % 10 == 0 || new_count == kCaptureLimit) {
        std::ostringstream msg;
        msg << "capturing:" << new_count << "/" << kCaptureLimit;
        publish_status(msg.str());
    }

    // Auto-stop when limit is reached.
    if (new_count >= kCaptureLimit) {
        set_mode(Mode::IDLE);
    }
}

// ===========================================================================
// Helpers
// ===========================================================================

std::string DataCollectorEngine::build_filename()
{
    // Timestamp: YYYYMMDD_HHMMSS  (thread-safe gmtime_r)
    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    std::ostringstream ts;
    ts << std::put_time(gmtime_r(&t_c, &tm_buf), "%Y%m%d_%H%M%S");

    // frame_YYYYMMDD_HHMMSS_batch{id}_cap{capture}.jpg
    // capture count guarantees uniqueness even when multiple frames
    // are written within the same clock second.
    int capture = capture_count_.load();
    int batch   = batch_id_.load();
    std::ostringstream path;
    path << output_dir_
         << "frame_" << ts.str()
         << "_batch" << batch
         << "_cap" << capture
         << ".jpg";
    return path.str();
}

void DataCollectorEngine::publish_status(const std::string& message)
{
    if (mqtt_ != nullptr) {
        if (!mqtt_->publish(kStatusTopic, message)) {
            // Publish failed — log is the best we can do.
            // The watchdog + systemd Restart=will recover the connection.
        }
    }
}

}  // namespace vision
}  // namespace gema
