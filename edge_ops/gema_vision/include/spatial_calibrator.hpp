#pragma once

#include "visual_primitive.hpp"

#include <opencv2/opencv.hpp>

#include <atomic>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Spatial calibrator — projects pixel bounding boxes to physical
 *        conveyor-belt coordinates (mm) via a calibrated homography.
 *
 * ## Anchor point
 *
 * For every VisualPrimitive, the **bottom-centre** of the bounding box
 * is used as the anchor point:
 *
 *     anchor = (bbox.x + bbox.width / 2,  bbox.y + bbox.height)
 *
 * This is the point where the object touches the conveyor belt (Z=0).
 * Projecting this point through the homography matrix H yields
 * correct (X, Y) millimetre coordinates **regardless of the object's
 * height**, because the belt plane is the calibration plane.
 *
 * ## Thread safety
 *
 * The calibration matrix H is protected by a std::shared_mutex:
 *
 *   - Inference thread (consumer loop)  → shared_lock  (N readers)
 *   - MQTT / config update              → unique_lock (1 writer)
 *
 * The homography matrix is set ONCE during installation and MAY be
 * updated at runtime (e.g. via MQTT calibration command) — writes
 * are so rare that the locking overhead is negligible.
 *
 * ## Uncalibrated state
 *
 * If no homography has been loaded, is_calibrated() returns false
 * and apply() is a no-op — primitives keep their default
 * physical_coords_mm = {-1, -1} and has_physical_coords = false.
 */
class SpatialCalibrator {
public:
    SpatialCalibrator() = default;

    // Non-copiable, non-movable (protects the mutex).
    SpatialCalibrator(const SpatialCalibrator&) = delete;
    SpatialCalibrator& operator=(const SpatialCalibrator&) = delete;
    SpatialCalibrator(SpatialCalibrator&&) = delete;
    SpatialCalibrator& operator=(SpatialCalibrator&&) = delete;

    // ------------------------------------------------------------------
    // Configuration
    // ------------------------------------------------------------------

    /**
     * @brief Load the homography matrix from a JSON config file.
     *
     * Expected JSON structure:
     * ```json
     * {
     *     "homography": [h00, h01, h02, h10, h11, h12, h20, h21, h22],
     *     "product_height_mm": 0.0,
     *     "camera_height_mm": 500.0
     * }
     * ```
     *
     * @param path  Path to config.json on the eMMC.
     * @return true  if the file was read and H is valid (determinant ≠ 0).
     * @return false if the file could not be read or H is degenerate.
     */
    bool load_from_file(const std::string& path);

    /**
     * @brief Load the homography from a 9-element vector.
     *
     * Useful for MQTT updates or programmatic injection in tests.
     *
     * @param h  Exactly 9 floats in row-major order [h00 … h22].
     * @return false if the vector is empty or the determinant is zero.
     */
    bool load_from_vector(const std::vector<float>& h);

    // ------------------------------------------------------------------
    // Calibration
    // ------------------------------------------------------------------

    /**
     * @brief Project a single primitive's bottom-centre anchor point
     *        to physical coordinates.
     *
     * If not calibrated, this is a no-op (primitive unchanged).
     *
     * @param primitive  The visual primitive to calibrate (modified
     *                   in-place).
     */
    void apply(VisualPrimitive& primitive) const;

    /**
     * @brief Batch-calibrate all primitives in a batch.
     *
     * @param batch  The batch to calibrate (modified in-place).
     */
    void apply_batch(PrimitiveBatch& batch) const;

    // ------------------------------------------------------------------
    // Status
    // ------------------------------------------------------------------

    /** @brief True if a valid homography matrix is loaded. */
    bool is_calibrated() const noexcept { return calibrated_.load(); }

    /** @brief Returns a copy of the current homography matrix. */
    cv::Mat homography() const;

private:
    /** @brief Internal: project a single (u, v) pixel to (x, y) mm. */
    cv::Point2f project(float u, float v) const;

    /// Thread-safe access to H_.
    mutable std::shared_mutex mtx_;

    /// 3×3 homography matrix (CV_64F).
    cv::Mat H_;

    /// Cached flag — updated under mutex, read atomically.
    std::atomic<bool> calibrated_{false};
};

}  // namespace vision
}  // namespace gema
