#pragma once

#include <opencv2/opencv.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Visual primitive — the atomic unit of spatial reasoning.
 *
 * Maps directly to the "Thinking with Visual Primitives" paradigm:
 * a bounding box anchored to physical coordinates, with an optional
 * pre-extracted ROI and classification metadata.
 *
 * The `roi` field is a lightweight cv::Mat header (O(1), zero-copy)
 * that shares the underlying pixel buffer of the source frame.
 */
struct VisualPrimitive {
    /** Bounding box in source-frame coordinates */
    cv::Rect bbox;

    /** Model confidence score [0.0, 1.0] */
    float confidence = 0.0f;

    /**
     * Semantic class identifier.
     *
     * Suggested convention:
     *   0 = DEFECT      — surface anomaly
     *   1 = OCR_ZONE    — text / lot number region
     *   2 = COLOR_ZONE  — colour-classification region
     *   3+ = reserved for application-specific classes
     */
    int class_id = 0;

    /**
     * Pre-extracted region of interest.
     *
     * Populated by the inference engine after model evaluation.
     * Zero-copy header into the original frame — do NOT clone unless
     * the primitive outlives the source frame.
     */
    cv::Mat roi;
};

/**
 * @brief Alias for a batch of primitives produced by a single inference pass.
 */
using PrimitiveBatch = std::vector<VisualPrimitive>;

}  // namespace vision
}  // namespace gema
