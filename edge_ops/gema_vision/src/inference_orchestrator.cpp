#include "inference_orchestrator.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>

namespace gema {
namespace vision {

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

InferenceOrchestrator::InferenceOrchestrator(
    InferenceEngine& engine,
    ThreadSafeQueue<cv::Mat>& frame_queue,
    MqttClient& mqtt,
    SpatialCalibrator& calibrator,
    int camera_index) noexcept
    : engine_(engine)
    , frame_queue_(frame_queue)
    , mqtt_(mqtt)
    , calibrator_(calibrator)
    , camera_index_(camera_index)
{}

InferenceOrchestrator::~InferenceOrchestrator()
{
    stop();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void InferenceOrchestrator::start()
{
    if (running_.exchange(true)) {
        return;  // already running
    }
    frames_processed_.store(0);
    consumer_thread_ = std::make_unique<std::thread>(
        &InferenceOrchestrator::consumer_loop, this);
}

void InferenceOrchestrator::stop()
{
    if (!running_.exchange(false)) {
        return;  // already stopped
    }

    // Signal the consumer to wake and drain.
    frame_queue_.shutdown();

    if (consumer_thread_ && consumer_thread_->joinable()) {
        consumer_thread_->join();
    }
    consumer_thread_.reset();
}

// ---------------------------------------------------------------------------
// Consumer loop  (drain pattern — exit ONLY on empty frame after shutdown)
// ---------------------------------------------------------------------------

void InferenceOrchestrator::consumer_loop()
{
    while (true) {
        // Block until a frame arrives OR shutdown is signalled.
        cv::Mat frame = frame_queue_.pop();

        // If the queue has been shut down AND drained, frame is empty.
        if (frame.empty()) {
            break;  // ◀── EXIT CONDITION: nothing left to process
        }

        // --- 1. Run inference ---------------------------------------------
        PrimitiveBatch primitives = engine_.infer(frame);

        // --- 1b. Spatial calibration (pixel → mm via homography) ----------
        calibrator_.apply_batch(primitives);

        // --- 2. Build structured result -----------------------------------
        InferenceResult result;
        result.primitive_count = primitives.size();
        result.capture_timestamp = current_timestamp_iso8601();

        for (const auto& p : primitives) {
            switch (p.class_id) {
                case 0: ++result.defect_count; break;
                case 1: ++result.ocr_count;    break;
                case 2: ++result.color_count;  break;
                default: break;
            }
        }
        result.primitives = std::move(primitives);

        // --- 3. Serialise & publish ---------------------------------------
        std::string json = serialize_to_json(result);
        mqtt_.publish("novamex/vision/inference", json);

        // --- 4. Bookkeeping -----------------------------------------------
        frames_processed_.fetch_add(1);
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string InferenceOrchestrator::current_timestamp_iso8601() const
{
    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::gmtime(&t_c), "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

std::string InferenceOrchestrator::serialize_to_json(
    const InferenceResult& result) const
{
    // Minimal JSON serialisation — no external dependency.
    // For production, swap to a proper library (e.g. nlohmann/json).
    std::ostringstream json;
    json << "{\n";
    json << "  \"primitive_count\": " << result.primitive_count << ",\n";
    json << "  \"defect_count\": "    << result.defect_count    << ",\n";
    json << "  \"ocr_count\": "       << result.ocr_count       << ",\n";
    json << "  \"color_count\": "     << result.color_count     << ",\n";
    json << "  \"capture_timestamp\": \""
         << result.capture_timestamp << "\",\n";
    json << "  \"primitives\": [\n";

    for (size_t i = 0; i < result.primitives.size(); ++i) {
        const auto& p = result.primitives[i];
        json << "    {\n";
        json << "      \"class_id\": "    << p.class_id    << ",\n";
        json << "      \"confidence\": "  << p.confidence  << ",\n";
        json << "      \"bbox\": ["
             << p.bbox.x << "," << p.bbox.y << ","
             << p.bbox.width << "," << p.bbox.height << "],\n";
        json << "      \"physical_coords_mm\": ["
             << p.physical_coords_mm.x << ", "
             << p.physical_coords_mm.y << "],\n";
        json << "      \"has_physical_coords\": "
             << (p.has_physical_coords ? "true" : "false") << "\n";
        json << "    }";
        if (i + 1 < result.primitives.size()) {
            json << ",";
        }
        json << "\n";
    }

    json << "  ]\n";
    json << "}\n";
    return json.str();
}

}  // namespace vision
}  // namespace gema
