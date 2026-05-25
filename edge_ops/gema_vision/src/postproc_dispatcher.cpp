#include "postproc_dispatcher.hpp"

#include <cmath>

namespace gema {
namespace vision {

void PostProcessDispatcher::process(
    PrimitiveBatch& batch,
    const cv::Mat& source_frame)
{
    for (auto& prim : batch) {
        switch (prim.class_id) {

        case 1:  // OCR_ZONE — run OCR, then colour validation
            if (ocr_ != nullptr && prim.roi.empty()) {
                // Extract ROI if not already set by the inference engine.
                prim.roi = source_frame(prim.bbox).clone();
            }
            if (ocr_ != nullptr) {
                prim.ocr_text = ocr_->infer(prim.roi);
            }
            if (color_ != nullptr) {
                auto cv_result = color_->validate(prim.roi);
                prim.color_pass = cv_result.pass;
            }
            break;

        case 2:  // COLOR_ZONE — colour validation only
            if (color_ != nullptr) {
                if (prim.roi.empty()) {
                    prim.roi = source_frame(prim.bbox).clone();
                }
                auto cv_result = color_->validate(prim.roi);
                prim.color_pass = cv_result.pass;
            }
            break;

        case 0:  // DEFECT — no post-processing
        default:
            break;
        }
    }
}

}  // namespace vision
}  // namespace gema
