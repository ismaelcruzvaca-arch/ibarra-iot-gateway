#pragma once

#include "color_validator.hpp"
#include "ocr_engine.hpp"
#include "visual_primitive.hpp"

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Routes each VisualPrimitive through the appropriate
 *        post-processing filters based on its class_id.
 *
 * ## Dispatch rules (from config.json)
 *
 *     class_id=0 (DEFECT)     → no filters
 *     class_id=1 (OCR_ZONE)   → OcrFilter → ColorFilter
 *     class_id=2 (COLOR_ZONE) → ColorFilter
 *
 * ## Thread safety
 *
 * The dispatcher itself is not thread-safe (it is called from the
 * single consumer thread).  The underlying filters (OcrEngine,
 * ColorValidator) MUST be thread-safe.
 */
class PostProcessDispatcher {
public:
    /**
     * @param ocr      OCR engine (injected, may be nullptr).
     * @param color    Colour validator (injected, may be nullptr).
     */
    PostProcessDispatcher(OcrEngine* ocr, ColorValidator* color) noexcept
        : ocr_(ocr)
        , color_(color)
    {}

    /**
     * @brief Run all applicable filters on every primitive in the batch.
     *
     * @param batch       Primitives to process (modified in-place).
     * @param source_frame Original camera frame (for ROI extraction).
     */
    void process(PrimitiveBatch& batch, const cv::Mat& source_frame);

private:
    OcrEngine*      ocr_;    ///< May be nullptr.
    ColorValidator* color_;  ///< May be nullptr.
};

}  // namespace vision
}  // namespace gema
