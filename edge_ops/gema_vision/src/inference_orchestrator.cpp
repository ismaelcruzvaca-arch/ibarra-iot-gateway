#include "inference_orchestrator.hpp"

#include <chrono>
#include <cmath>
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
    ThreadSafeQueue<std::shared_ptr<cv::Mat>>& frame_queue,
    MqttClient& mqtt,
    SpatialCalibrator& calibrator,
    PostProcessDispatcher& dispatcher,
    int camera_index) noexcept
    : engine_(engine)
    , frame_queue_(frame_queue)
    , mqtt_(mqtt)
    , calibrator_(calibrator)
    , dispatcher_(dispatcher)
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
// Consumer loop
// ---------------------------------------------------------------------------

void InferenceOrchestrator::consumer_loop()
{
    while (true) {
        // Block until a frame arrives OR shutdown is signalled.
        auto frame_ptr = frame_queue_.pop();

        // When shutdown is signalled and the queue is drained, pop()
        // returns a default-constructed shared_ptr (nullptr).
        if (!frame_ptr && frame_queue_.is_shutdown()) {
            break;  // ◀── EXIT CONDITION: queue drained after shutdown
        }

        // Skip a null frame that arrived before shutdown (should not
        // happen in practice, but defensive).
        if (!frame_ptr) {
            continue;
        }

        // --- 1. Run inference ---------------------------------------------
        PrimitiveBatch primitives = engine_.infer(*frame_ptr);

        // --- 1b. Spatial calibration (pixel → mm via homography) ----------
        calibrator_.apply_batch(primitives);

        // --- 1c. Post-processing (OCR + colour validation) ----------------
        dispatcher_.process(primitives, *frame_ptr);

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
        if (!mqtt_.publish("novamex/vision/inference", json)) {
            // Publish failed — MQTT broker unreachable.
            // The watchdog + systemd Restart= will keep the process alive
            // until the connection is restored.
        }

        // --- 4. Bookkeeping -----------------------------------------------
        frames_processed_.fetch_add(1);

        // Track cumulative defect count + last defect timestamp.
        if (result.defect_count > 0) {
            defects_total_.fetch_add(result.defect_count);
            last_defect_epoch_.store(
                static_cast<uint64_t>(std::time(nullptr)));
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string InferenceOrchestrator::current_timestamp_iso8601() const
{
    auto now = std::chrono::system_clock::now();
    auto t_c = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf;
    std::ostringstream oss;
    oss << std::put_time(gmtime_r(&t_c, &tm_buf), "%Y-%m-%dT%H:%M:%SZ");
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

        // Guard against NaN in physical coordinates (corrupt JSON).
        float px = std::isfinite(p.physical_coords_mm.x)
                   ? p.physical_coords_mm.x : -1.0f;
        float py = std::isfinite(p.physical_coords_mm.y)
                   ? p.physical_coords_mm.y : -1.0f;

        json << "    {\n";
        json << "      \"class_id\": "    << p.class_id    << ",\n";
        json << "      \"confidence\": "  << p.confidence  << ",\n";
        json << "      \"bbox\": ["
             << p.bbox.x << "," << p.bbox.y << ","
             << p.bbox.width << "," << p.bbox.height << "],\n";
        json << "      \"physical_coords_mm\": ["
             << px << ", " << py << "],\n";
        json << "      \"has_physical_coords\": "
             << (p.has_physical_coords ? "true" : "false") << ",\n";
        json << "      \"ocr_text\": \""
             << p.ocr_text << "\",\n";
        json << "      \"color_pass\": "
             << (p.color_pass ? "true" : "false") << "\n";
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
