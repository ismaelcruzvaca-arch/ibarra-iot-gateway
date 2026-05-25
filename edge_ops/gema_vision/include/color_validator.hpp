#pragma once

#include <opencv2/opencv.hpp>

namespace gema {
namespace vision {

/**
 * @brief Result of a single colour validation check.
 */
struct ColorValidationResult {
    /** True if the colour matched the reference. */
    bool pass = false;

    /**
     * Ratio of matching pixels to valid (non-glare) pixels [0.0, 1.0].
     * Only meaningful if pass == true.
     */
    float ratio = 0.0f;

    /**
     * True if validation was skipped (too much glare, underexposed, etc.).
     * When true, `pass` means "no decision — treat as pass for safety".
     */
    bool skipped = false;

    /** Reason for skipping (for logging / MQTT). */
    std::string skip_reason;
};

/**
 * @brief Industrial colour validator using HSV with glare masking.
 *
 * Designed for the RV1106: pure CPU, no NPU, ~5-15 ms per ROI.
 *
 * ## Glare masking
 *
 * Bottles and glossy labels under directional LED flash produce
 * specular highlights where S ≈ 0 and V ≈ 255.  These pixels are
 * excluded from the colour statistics so they do not cause false
 * rejections.
 *
 * ## Red wrap-around
 *
 * OpenCV HSV stores Hue in range [0, 180] for 8-bit images.
 * Red is centred around 0° AND 180° — both ranges MUST be checked.
 *
 * ## Dynamic adaptation
 *
 * The first N good products are used to learn the nominal HSV
 * distribution for each class_id.  Thresholds are set at
 * mean ± 2σ.
 */
class ColorValidator {
public:
    /**
     * @brief Configure the colour ranges for a given class_id.
     *
     * @param class_id   Primitive class (1 = OCR_ZONE, 2 = COLOR_ZONE).
     * @param hue_low    Lower Hue bound (e.g., 0 for red).
     * @param hue_high   Upper Hue bound (e.g., 10 for red).
     * @param hue_low2   Second Hue bound for wrap-around (e.g., 170 for red).
     * @param hue_high2  Upper second Hue bound (e.g., 180 for red).
     * @param sat_min    Minimum Saturation.
     * @param val_min    Minimum Value.
     * @param pass_ratio Minimum ratio of matching pixels to pass.
     */
    void configure(int class_id,
                   int hue_low, int hue_high,
                   int hue_low2, int hue_high2,
                   int sat_min, int val_min,
                   float pass_ratio = 0.80f);

    /**
     * @brief Validate the colour of an ROI.
     *
     * @param roi  The region of interest (BGR, 8-bit).
     * @return     Validation result with pass/fail/skip.
     */
    ColorValidationResult validate(const cv::Mat& roi) const;

    /** @brief True if this class_id has been configured. */
    bool is_configured(int class_id) const;

private:
    struct ColorConfig {
        int h_lo = 0, h_hi = 180;
        int h_lo2 = 0, h_hi2 = 0;   // second range (for red wrap)
        int s_min = 50, v_min = 50;
        float pass_ratio = 0.80f;
        bool configured = false;
    };

    mutable std::mutex mtx_;
    std::unordered_map<int, ColorConfig> configs_;
};

}  // namespace vision
}  // namespace gema
