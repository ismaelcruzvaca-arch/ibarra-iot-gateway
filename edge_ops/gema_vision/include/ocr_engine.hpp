#pragma once

#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

namespace gema {
namespace vision {

/**
 * @brief Abstract interface for optical character recognition.
 *
 * Each implementation reads text from a cropped ROI and returns the
 * decoded string.  The interface is designed for dependency injection:
 *
 *     LprnetOcrEngine  → NPU inference via RKNN (production)
 *     MockOcrEngine    → canned results (SIL tests)
 *
 * ## Thread safety
 *
 * All implementations MUST be thread-safe: infer() may be called from
 * the orchestrator's consumer thread while the NPU is shared between
 * YOLO and OCR contexts.
 */
class OcrEngine {
public:
    virtual ~OcrEngine() = default;

    /**
     * @brief Read text from a cropped image region.
     *
     * @param roi  Cropped ROI (typically 24×94 for LPRNet).
     * @return     Decoded UTF-8 text, or empty string on failure.
     */
    virtual std::string infer(const cv::Mat& roi) = 0;

    /**
     * @brief Load (or reload) the OCR model.
     *
     * @param path  Path to the RKNN model file.
     * @return true on success.
     */
    virtual bool load_model(const std::string& path) = 0;

    /** @brief Human-readable name for logging. */
    virtual std::string_view name() const = 0;
};

// ---------------------------------------------------------------------------
// SIL test mock
// ---------------------------------------------------------------------------

/**
 * @brief Mock OCR engine that returns canned text.
 *
 * Used by GTest to validate the post-processing pipeline without a
 * real NPU model.
 */
class MockOcrEngine final : public OcrEngine {
public:
    explicit MockOcrEngine(std::string canned = "LOTE-2405")
        : canned_(std::move(canned))
    {}

    std::string infer(const cv::Mat& /*roi*/) override
    {
        return canned_;
    }

    bool load_model(const std::string& /*path*/) override
    {
        return true;
    }

    std::string_view name() const override { return "mock_ocr"; }

    void set_canned(std::string text) { canned_ = std::move(text); }

private:
    std::string canned_;
};

// ---------------------------------------------------------------------------
// Production LPRNet engine (skeleton — requires actual .rknn model)
// ---------------------------------------------------------------------------

/**
 * @brief OCR engine using LPRNet on the RV1106 NPU.
 *
 * Expects an RKNN-quantized LPRNet model with a 20-class output
 * (19 active chars: 0-9, /, :, L, O, T, E, V, N, C; blank at index 19).
 *
 * The RKNN context is injected from outside (it is shared with the
 * YOLO engine via the dual-context pattern).  This class only
 * manages the LPRNet-specific input/output tensor setup.
 *
 * ## Character mapping
 *
 * 19 active characters + 1 blank = 20 total classes.
 * Index 19 is the CTC blank token (not a real character).
 */
class LprnetOcrEngine final : public OcrEngine {
public:
    /** 19 active characters + 1 blank = 20 total classes.
     *  Index 19 is the CTC blank token (not a real character). */
    static constexpr char kCharMap[20] = {
        '0','1','2','3','4','5','6','7','8','9',  // 0-9
        '/',':',                                     // 10-11
        'L','O','T','E','V','N','C',                // 12-18
        '\0'                                         // 19 = blank (unused)
    };
    static constexpr int kNumClasses = 20;
    static constexpr int kBlankIdx = 19;
    static constexpr int kInputWidth = 94;
    static constexpr int kInputHeight = 24;
    static constexpr int kTimesteps = 18;

    /**
     * @param rknn_context  Pre-initialized RKNN context (set up by
     *                      main.cpp at boot).
     */
    explicit LprnetOcrEngine(void* rknn_context = nullptr) noexcept
        : ctx_(rknn_context)
    {}

    std::string infer(const cv::Mat& roi) override;
    bool load_model(const std::string& path) override;
    std::string_view name() const override { return "lprnet"; }

private:
    /** @brief Greedy CTC decode.
     *
     *  Takes flat logits array [timesteps * num_classes] and decodes using
     *  argmax + blank-skip + collapse-repeats rules.
     *
     *  @param logits     Flat float array, row-major [T, C].
     *  @param timesteps  Number of timesteps (T).
     *  @param num_classes Number of classes (C).
     *  @return           Decoded UTF-8 string.
     */
    static std::string greedy_ctc_decode(const float* logits,
                                         int timesteps,
                                         int num_classes);

    void* ctx_;  ///< Opaque rknn_context handle.
};

}  // namespace vision
}  // namespace gema
