#pragma once

#include "inference_engine.hpp"
#include "visual_primitive.hpp"

#include <opencv2/opencv.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Forward declaration — MqttClient lives outside the vision module.
 *
 * Same pattern used in inference_orchestrator.hpp.
 */
class MqttClient;

/**
 * @brief Data-collection engine that captures real camera frames to
 *        persistent storage for use as an RKNN calibration dataset.
 *
 * Implements the **same** InferenceEngine interface so it can be
 * injected into the orchestrator as a drop-in replacement:
 *
 *     MockEngine           (SIL, canned output)
 *     DataCollectorEngine  (captures real frames → JPEG)
 *     RknnContext          (NPU inference, future)
 *
 * ## State machine
 *
 *     IDLE ──set_mode(CALIBRATION)──▶ CALIBRATION
 *      ▲                                 │
 *      │                                 │  (200 frames captured)
 *      └──────── auto-reset ─────────────┘
 *
 * ## Storage
 *
 * Frames are saved as JPEG (quality 95) to a configurable output
 * directory.  On the production RV1106 this MUST be the eMMC
 * persistent partition (e.g. /userdata/vision/calibration/).
 * The /tmp/ path is deliberately avoided because it is typically a
 * tmpfs (RAM-backed) on Rockchip BSPs — writing there would defeat
 * the purpose and risk OOM.
 *
 * ## Thread safety
 *
 * set_mode(), mode(), and capture_count() are thread-safe (atomic /
 * mutex).  infer() is designed to be called from the orchestrator's
 * consumer thread and is NOT internally synchronised beyond the
 * capture count increment.
 */
class DataCollectorEngine final : public InferenceEngine {
public:
    /** Engine operational mode. */
    enum class Mode : uint8_t {
        IDLE,         ///< No-op — frames are discarded.
        CALIBRATION   ///< Save every incoming frame as JPEG.
    };

    /**
     * @brief Construct the data-collection engine.
     *
     * @param mqtt        Optional MqttClient pointer for status
     *                    publishing.  May be nullptr (tests).
     * @param output_dir  Directory where JPEG files are written.
     *                    Default: "/userdata/vision/calibration/".
     */
    explicit DataCollectorEngine(
        MqttClient* mqtt = nullptr,
        std::string  output_dir = "/userdata/vision/calibration/") noexcept;

    ~DataCollectorEngine() override = default;

    // ------------------------------------------------------------------
    // InferenceEngine interface
    // ------------------------------------------------------------------

    /** @brief No-op — always returns true. */
    bool load_model(const std::string& path) override;

    /**
     * @brief Process a single camera frame.
     *
     * In IDLE mode:         discards the frame, returns empty batch.
     * In CALIBRATION mode:  saves the frame as JPEG, increments
     *                       counter, returns empty batch.
     *
     * @return Always an empty PrimitiveBatch (this engine does not
     *         perform inference).
     */
    PrimitiveBatch infer(const cv::Mat& frame) override;

    // ------------------------------------------------------------------
    // Mode control (thread-safe)
    // ------------------------------------------------------------------

    /**
     * @brief Switch the engine to a new operational mode.
     *
     * When entering CALIBRATION mode:
     *   - Resets the frame counter.
     *   - Increments the batch ID.
     *   - Creates the output directory if it does not exist.
     *
     * @param new_mode  IDLE or CALIBRATION.
     */
    void set_mode(Mode new_mode);

    /** @brief Current operational mode. */
    Mode mode() const noexcept { return mode_.load(); }

    // ------------------------------------------------------------------
    // Capture status
    // ------------------------------------------------------------------

    /** @brief Number of frames captured in the current calibration run. */
    int capture_count() const noexcept { return capture_count_.load(); }

    /** @brief Maximum frames before auto-stop (hard-coded to 200). */
    static constexpr int capture_limit() noexcept { return kCaptureLimit; }

private:
    /** @brief Save an OpenCV Mat to a timestamped JPEG file. */
    void save_frame(const cv::Mat& frame);

    /** @brief Build the filesystem path for the next frame. */
    std::string build_filename();

    /** @brief Publish a status update via MqttClient, if available. */
    void publish_status(const std::string& message);

    // Dependencies.
    MqttClient* mqtt_;            ///< Optional status publisher.
    std::string output_dir_;      ///< eMMC persistent path.

    // State (atomic for thread-safe reads from outside).
    std::atomic<Mode>   mode_{Mode::IDLE};
    std::atomic<int>    capture_count_{0};

    // Synchronised by the consumer thread (only infer() touches these).
    int batch_id_ = 0;

    // Constants.
    static constexpr int    kCaptureLimit  = 200;
    static constexpr int    kJpegQuality   = 95;
    static constexpr char   kStatusTopic[] = "novamex/vision/status";
};

}  // namespace vision
}  // namespace gema
