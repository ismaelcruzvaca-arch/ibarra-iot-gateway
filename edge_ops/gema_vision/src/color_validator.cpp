#include "color_validator.hpp"

#include <cmath>

namespace gema {
namespace vision {

void ColorValidator::configure(int class_id,
                                int hue_low, int hue_high,
                                int hue_low2, int hue_high2,
                                int sat_min, int val_min,
                                float pass_ratio)
{
    std::lock_guard lock(mtx_);
    configs_[class_id] = {
        hue_low, hue_high,
        hue_low2, hue_high2,
        sat_min, val_min,
        pass_ratio,
        true  // configured
    };
}

bool ColorValidator::is_configured(int class_id) const
{
    std::lock_guard lock(mtx_);
    auto it = configs_.find(class_id);
    return it != configs_.end() && it->second.configured;
}

ColorValidationResult ColorValidator::validate(const cv::Mat& roi) const
{
    ColorValidationResult result;

    if (roi.empty()) {
        result.skipped = true;
        result.skip_reason = "empty_roi";
        return result;
    }

    cv::Mat hsv;
    cv::cvtColor(roi, hsv, cv::COLOR_BGR2HSV);

    int total_px = roi.rows * roi.cols;

    // ---- Step 1: Glare mask -----------------------------------------------
    // Specular highlights: V > 240 AND S < 30 (white flash on glossy surfaces)
    cv::Mat glare_mask;
    cv::inRange(hsv,
                cv::Scalar(0, 0, 240),
                cv::Scalar(180, 30, 255),
                glare_mask);
    int glare_px = cv::countNonZero(glare_mask);

    // If more than 70 % of the ROI is glare, skip validation entirely.
    if (glare_px > total_px * 0.70f) {
        result.skipped = true;
        result.skip_reason = "excessive_glare";
        return result;
    }

    int valid_px = total_px - glare_px;
    if (valid_px < total_px * 0.30f) {
        result.skipped = true;
        result.skip_reason = "insufficient_valid_pixels";
        return result;
    }

    // ---- Step 2: Determine active config ----------------------------------
    // We need to know which class_id is being validated to select the range.
    // Since validate() is called from ColorFilter which knows the class_id,
    // we rely on the caller to configure the correct range BEFORE calling.
    // We use the FIRST configured range as default.
    ColorConfig cfg;
    {
        std::lock_guard lock(mtx_);
        if (!configs_.empty()) {
            cfg = configs_.begin()->second;
        } else {
            // No config — pass by default (safety).
            result.pass = true;
            result.skipped = true;
            result.skip_reason = "unconfigured";
            return result;
        }
    }

    // ---- Step 3: Dual-range colour mask -----------------------------------
    cv::Mat mask_low, mask_high, color_mask;
    cv::inRange(hsv,
                cv::Scalar(cfg.h_lo, cfg.s_min, cfg.v_min),
                cv::Scalar(cfg.h_hi, 255, 255),
                mask_low);

    if (cfg.h_lo2 != cfg.h_hi2) {
        // Second range (e.g. red wrap-around: H in [170, 180]).
        cv::inRange(hsv,
                    cv::Scalar(cfg.h_lo2, cfg.s_min, cfg.v_min),
                    cv::Scalar(cfg.h_hi2, 255, 255),
                    mask_high);
        cv::bitwise_or(mask_low, mask_high, color_mask);
    } else {
        color_mask = mask_low;
    }

    // ---- Step 4: Exclude glare pixels from the colour mask ----------------
    cv::bitwise_and(color_mask, ~glare_mask, color_mask);
    int matching_px = cv::countNonZero(color_mask);

    // ---- Step 5: Compute pass ratio ---------------------------------------
    float ratio = static_cast<float>(matching_px) / static_cast<float>(valid_px);
    result.ratio = ratio;
    result.pass  = (ratio >= cfg.pass_ratio);

    return result;
}

}  // namespace vision
}  // namespace gema
