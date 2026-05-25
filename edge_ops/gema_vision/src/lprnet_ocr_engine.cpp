#include "ocr_engine.hpp"

namespace gema {
namespace vision {

bool LprnetOcrEngine::load_model(const std::string& path)
{
    // TODO: implement when the RKNN model is available.
    // rknn_init(ctx_, path.c_str(), 0, 0, NULL);
    (void)path;
    return true;
}

std::string LprnetOcrEngine::infer(const cv::Mat& roi)
{
    // TODO: implement when the RKNN model is available.
    // 1. Preprocess: resize to 24x94, RGB2BGR
    // 2. rknn_inputs_set(ctx_, ...)
    // 3. rknn_run(ctx_, NULL)
    // 4. rknn_outputs_get → CTC decode → string
    (void)roi;
    return {};
}

}  // namespace vision
}  // namespace gema
