#pragma once

#include "flash_trigger.hpp"
#include "inference_engine.hpp"
#include "thread_safe_queue.hpp"

#include <opencv2/opencv.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace gema {
namespace vision {

/**
 * @brief Forward declaration of the MQTT client wrapper.
 *
 * The MQTT client lives in edge_ops/gateway_translator/ and follows
 * the same DI-friendly interface pattern.  A minimal abstract shape
 * is declared here to break the circular dependency;
 * the real include lives in the .cpp translation unit.
 */
class MqttClient {
public:
    virtual ~MqttClient() = default;
    virtual bool publish(const std::string& topic, const std::string& payload) = 0;
};

// ---------------------------------------------------------------------------
// Inference result forwarded to the cloud
// ---------------------------------------------------------------------------

/**
 * @brief Structured result produced after a full inference cycle.
 *
 * Serialised to JSON (or Protobuf) before being published via MqttClient.
 */
struct InferenceResult {
    /** Number of primitives detected in this frame */
    size_t primitive_count = 0;

    /** Count per class_id for quick dashboard aggregation */
    int defect_count = 0;
    int ocr_count = 0;
    int color_count = 0;

    /** ISO-8601 timestamp of the frame capture */
    std::string capture_timestamp;

    /** Full serialisable primitive list */
    PrimitiveBatch primitives;
};

// ---------------------------------------------------------------------------
// Orchestrator — consumer thread
// ---------------------------------------------------------------------------

/**
 * @brief High-level vision pipeline orchestrator.
 *
 * Owns the **consumer thread** in the Producer-Consumer pair:
 *
 *   1. Reads a frame from ThreadSafeQueue<cv::Mat> (pushed by the
 *      GPIO-driven producer).
 *   2. Passes the frame through InferenceEngine → PrimitiveBatch.
 *   3. Post-processes each primitive (defect counting, colour
 *      classification, OCR — delegated to strategy objects).
 *   4. Serialises the result and publishes it via MqttClient.
 *
 * ## Dependency injection
 *
 * All three dependencies are injected through the constructor so
 * the orchestrator can be instantiated in SIL tests with a MockEngine
 * and a null MqttClient.
 *
 * ## Thread safety
 *
 * The orchestrator itself is **not** thread-safe — it is designed to
 * be used from exactly one consumer thread.  The queue is the
 * synchronisation boundary between producer and consumer.
 */
class InferenceOrchestrator {
public:
    /**
     * @brief Construct the orchestrator.
     *
     * @param engine      Inference engine (RknnContext or MockEngine).
     * @param frame_queue Bounded/unbounded frame queue fed by the
     *                    GPIO-driven producer thread.
     * @param mqtt        MQTT client for cloud egress.
     * @param camera_index OpenCV camera index (default 0).
     */
    InferenceOrchestrator(
        InferenceEngine& engine,
        ThreadSafeQueue<cv::Mat>& frame_queue,
        MqttClient& mqtt,
        int camera_index = 0) noexcept;

    ~InferenceOrchestrator();

    // Non-copiable, non-movable.
    InferenceOrchestrator(const InferenceOrchestrator&) = delete;
    InferenceOrchestrator& operator=(const InferenceOrchestrator&) = delete;
    InferenceOrchestrator(InferenceOrchestrator&&) = delete;
    InferenceOrchestrator& operator=(InferenceOrchestrator&&) = delete;

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    /**
     * @brief Start the consumer processing loop.
     *
     * Launches the internal thread that reads frames from the queue
     * and runs inference.  This call returns immediately.
     */
    void start();

    /**
     * @brief Gracefully stop the consumer loop.
     *
     * Signals the internal thread to drain the queue and exit.
     * Blocks until the thread has joined.
     */
    void stop();

    // ------------------------------------------------------------------
    // Runtime status
    // ------------------------------------------------------------------

    /** @brief True if the consumer loop is running. */
    bool is_running() const noexcept { return running_.load(); }

    /** @brief Total frames processed since start(). */
    uint64_t frames_processed() const noexcept { return frames_processed_.load(); }

private:
    /** The main consumer loop executed by the internal thread. */
    void consumer_loop();

    /** @brief Get current UTC time as ISO-8601 string. */
    std::string current_timestamp_iso8601() const;

    /** @brief Serialise an InferenceResult to a minimal JSON string. */
    std::string serialize_to_json(const InferenceResult& result) const;

    // Dependencies (injected, not owned).
    InferenceEngine& engine_;
    ThreadSafeQueue<cv::Mat>& frame_queue_;
    MqttClient& mqtt_;

    // Camera index used by the producer.
    int camera_index_;

    // Thread and control.
    std::unique_ptr<std::thread> consumer_thread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> frames_processed_{0};
};

}  // namespace vision
}  // namespace gema
