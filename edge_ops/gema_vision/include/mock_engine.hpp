#pragma once

#include "inference_engine.hpp"
#include "visual_primitive.hpp"

#include <chrono>
#include <random>
#include <thread>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Software-in-the-Loop (SIL) mock inference engine.
 *
 * Simulates an RKNN NPU with a configurable latency and returns
 * canned VisualPrimitives.  Used by GTest to validate the
 * orchestrator threading model WITHOUT real hardware.
 *
 * ## Timing model
 *
 *   - infer() sleeps for `latency_ms` (default 20 ms) to mimic
 *     NPU inference wall-clock time.
 *   - load_model() is a no-op that always returns true.
 *
 * ## Canned output
 *
 * Every call to infer() returns exactly two primitives:
 *
 *   | idx | class_id | confidence | bbox              | Meaning  |
 *   |-----|----------|------------|-------------------|----------|
 *   | 0   | 0        | 0.95       | (10,20,100,200)   | DEFECT   |
 *   | 1   | 1        | 0.88       | (300,50,150,80)   | OCR_ZONE |
 */
class MockEngine final : public InferenceEngine {
public:
    explicit MockEngine(int latency_ms = 20)
        : latency_ms_(latency_ms)
        , rng_(std::random_device{}())
    {}

    bool load_model(const std::string& /*path*/) override
    {
        return true;
    }

    PrimitiveBatch infer(const cv::Mat& /*frame*/) override
    {
        // Simulate NPU inference latency.
        if (latency_ms_ > 0) {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(latency_ms_));
        }

        PrimitiveBatch batch;
        batch.reserve(2);

        // Primitive 0 — Surface defect
        VisualPrimitive defect;
        defect.bbox       = cv::Rect(10, 20, 100, 200);
        defect.confidence = 0.95f;
        defect.class_id   = 0;  // DEFECT
        defect.roi        = cv::Mat();  // empty — not needed for SIL
        batch.push_back(std::move(defect));

        // Primitive 1 — OCR zone
        VisualPrimitive ocr;
        ocr.bbox       = cv::Rect(300, 50, 150, 80);
        ocr.confidence = 0.88f;
        ocr.class_id   = 1;  // OCR_ZONE
        ocr.roi        = cv::Mat();
        batch.push_back(std::move(ocr));

        return batch;
    }

private:
    int latency_ms_;
    std::mt19937 rng_;
};

}  // namespace vision
}  // namespace gema
