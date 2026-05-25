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
 * Expects an RKNN-quantized LPRNet model with a 38-class output
 * (0-9, A-Z sans I/O, /, -, :).
 *
 * The RKNN context is injected from outside (it is shared with the
 * YOLO engine via the dual-context pattern).  This class only
 * manages the LPRNet-specific input/output tensor setup.
 */
class LprnetOcrEngine final : public OcrEngine {
public:
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
    void* ctx_;  ///< Opaque rknn_context handle.
};

}  // namespace vision
}  // namespace gema
