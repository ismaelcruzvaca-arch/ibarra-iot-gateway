#include "spatial_calibrator.hpp"

#include <fstream>
#include <sstream>

namespace gema {
namespace vision {

// ===========================================================================
// Configuration
// ===========================================================================

bool SpatialCalibrator::load_from_file(const std::string& path)
{
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    // Minimal JSON parser (no external dependency).
    // Expected: {"homography": [h00, h01, h02, h10, h11, h12, h20, h21, h22], ...}
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();

    // Find the homography array.
    auto pos = content.find('[');
    auto end = content.find(']');
    if (pos == std::string::npos || end == std::string::npos || pos >= end) {
        return false;
    }

    // Parse 9 comma/whitespace-separated floats.
    std::string segment = content.substr(pos + 1, end - pos - 1);
    std::vector<float> values;
    std::stringstream seg_stream(segment);
    std::string item;

    while (std::getline(seg_stream, item, ',')) {
        // Trim whitespace.
        size_t start = item.find_first_not_of(" \t\n\r");
        size_t stop  = item.find_last_not_of(" \t\n\r");
        if (start != std::string::npos && stop != std::string::npos) {
            values.push_back(std::stof(item.substr(start, stop - start + 1)));
        }
    }

    return load_from_vector(values);
}

bool SpatialCalibrator::load_from_vector(const std::vector<float>& h)
{
    if (h.size() != 9) {
        return false;
    }

    cv::Mat H(3, 3, CV_64F);
    for (int r = 0; r < 3; ++r) {
        for (int c = 0; c < 3; ++c) {
            H.at<double>(r, c) = static_cast<double>(h[r * 3 + c]);
        }
    }

    // Validate: determinant must not be zero (degenerate transform).
    if (std::abs(cv::determinant(H)) < 1e-9) {
        return false;
    }

    {
        std::unique_lock lock(mtx_);
        H_ = H.clone();
    }
    calibrated_.store(true);
    return true;
}

// ===========================================================================
// Calibration
// ===========================================================================

void SpatialCalibrator::apply(VisualPrimitive& primitive) const
{
    if (!calibrated_.load()) {
        return;  // no-op — defaults stay {-1, -1} / false
    }

    // Bottom-centre anchor point (touches the belt at Z=0).
    float u = primitive.bbox.x + primitive.bbox.width / 2.0f;
    float v = primitive.bbox.y + primitive.bbox.height;

    cv::Point2f physical = project(u, v);

    primitive.physical_coords_mm = physical;
    primitive.has_physical_coords = true;
}

void SpatialCalibrator::apply_batch(PrimitiveBatch& batch) const
{
    if (!calibrated_.load()) {
        return;
    }

    for (auto& p : batch) {
        apply(p);
    }
}

// ===========================================================================
// Status
// ===========================================================================

cv::Mat SpatialCalibrator::homography() const
{
    std::shared_lock lock(mtx_);
    return H_.clone();
}

// ===========================================================================
// Private
// ===========================================================================

cv::Point2f SpatialCalibrator::project(float u, float v) const
{
    std::shared_lock lock(mtx_);

    // perspectiveTransform for a single point:  s * [x' y' 1]^T = H * [u v 1]^T
    double w = H_.at<double>(2, 0) * u + H_.at<double>(2, 1) * v + H_.at<double>(2, 2);
    if (std::abs(w) < 1e-9) {
        return {-1.0f, -1.0f};  // degenerate — at infinity
    }

    double x = (H_.at<double>(0, 0) * u + H_.at<double>(0, 1) * v + H_.at<double>(0, 2)) / w;
    double y = (H_.at<double>(1, 0) * u + H_.at<double>(1, 1) * v + H_.at<double>(1, 2)) / w;

    return {static_cast<float>(x), static_cast<float>(y)};
}

}  // namespace vision
}  // namespace gema
