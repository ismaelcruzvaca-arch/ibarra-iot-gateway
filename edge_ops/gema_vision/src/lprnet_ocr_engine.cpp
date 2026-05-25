#include "ocr_engine.hpp"

#include <cmath>

namespace gema {
namespace vision {

// ===========================================================================
// Greedy CTC Decode
// ===========================================================================

std::string LprnetOcrEngine::greedy_ctc_decode(const float* logits,
                                                int timesteps,
                                                int num_classes)
{
    if (!logits || timesteps <= 0 || num_classes <= 0) {
        return {};
    }

    std::string result;
    int prev_char = -1;
    const int blank = kBlankIdx;

    for (int t = 0; t < timesteps; ++t) {
        // --- NaN/Inf guard ---
        // If the entire timestep is corrupt, skip it entirely rather than
        // decoding garbage that might look like a valid character.
        bool corrupt = false;
        for (int c = 0; c < num_classes; ++c) {
            float v = logits[t * num_classes + c];
            if (!std::isfinite(v)) {
                corrupt = true;
                break;
            }
        }
        if (corrupt) {
            // Reset prev_char so the next valid timestep doesn't get
            // collapsed — better to lose a timestep than to silently
            // propagate a garbage decode.
            prev_char = -1;
            continue;
        }

        // Argmax over classes at this timestep
        int max_idx = 0;
        float max_val = logits[t * num_classes];
        for (int c = 1; c < num_classes; ++c) {
            if (logits[t * num_classes + c] > max_val) {
                max_val = logits[t * num_classes + c];
                max_idx = c;
            }
        }

        // CTC decode rules:
        // 1. Skip blank tokens (index == kBlankIdx)
        // 2. Collapse consecutive identical characters
        if (max_idx != blank && max_idx != prev_char) {
            result += kCharMap[max_idx];
        }
        prev_char = max_idx;
    }

    return result;
}

// ===========================================================================
// Model Loading
// ===========================================================================

bool LprnetOcrEngine::load_model(const std::string& path)
{
    // TODO: implement when ocr.rknn is trained.
    //
    // Production implementation:
    //   1. If ctx_ is already valid, call rknn_destroy(ctx_) first.
    //   2. Call rknn_init(&ctx_, path.c_str(), 0, 0, NULL).
    //   3. Call rknn_query(ctx_, RKNN_QUERY_IN_OUT_NUM, &io_num, ...)
    //      to verify input/output tensor specs.
    //   4. Store input/output indices for infer().
    //
    // Expected:
    //   Input:  [1, 3, 24, 94]  NCHW BGR uint8
    //   Output: [1, 18, 20]     logits (18 timesteps × 20 classes)
    //
    (void)path;
    return false;  // TODO: implement when ocr.rknn is trained
}

// ===========================================================================
// NPU Inference
// ===========================================================================

std::string LprnetOcrEngine::infer(const cv::Mat& roi)
{
    // --- Empty ROI guard ---
    if (roi.empty()) {
        return {};
    }

    // --- Model loaded guard ---
    if (!ctx_) {
        return {};
    }

    // ---- 1. Preprocess: resize to 24×94 BGR -------------------------------
    cv::Mat input;
    cv::resize(roi, input,
               cv::Size(kInputWidth, kInputHeight),
               0, 0,
               cv::INTER_LINEAR);

    // Ensure 3-channel BGR (model expects BGR per Rockchip spec;
    // OpenCV's default imread/camera input is already BGR).
    if (input.channels() != 3) {
        cv::cvtColor(input, input, cv::COLOR_GRAY2BGR);
    }
    if (input.type() != CV_8UC3) {
        input.convertTo(input, CV_8UC3);
    }

    // ---- 2-4. Run NPU inference + decode ----------------------------------
    // TODO: implement when hardware is available and .rknn is loaded.
    //
    // Production implementation:
    //   rknn_input inputs[1];
    //   inputs[0].index = 0;
    //   inputs[0].type = RKNN_TENSOR_UINT8;
    //   inputs[0].size = 3 * 24 * 94;
    //   inputs[0].fmt = RKNN_TENSOR_NCHW;
    //   inputs[0].buf = input.data;
    //   rknn_inputs_set(ctx_, 1, inputs);
    //
    //   rknn_run(ctx_, nullptr);
    //
    //   rknn_output outputs[1];
    //   outputs[0].want_float = 1;
    //   rknn_outputs_get(ctx_, 1, outputs, nullptr);
    //
    //   const float* logits = static_cast<const float*>(outputs[0].buf);
    //   const int output_size = kTimesteps * kNumClasses;  // 18 * 20 = 360
    //   std::string text = greedy_ctc_decode(logits, kTimesteps, kNumClasses);
    //
    //   rknn_outputs_release(ctx_, 1, outputs);
    //   return text;
    //
    return {};  // TODO: implement when hardware is available
}

}  // namespace vision
}  // namespace gema
