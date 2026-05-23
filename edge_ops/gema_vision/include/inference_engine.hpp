#pragma once

#include "visual_primitive.hpp"

#include <opencv2/opencv.hpp>

#include <string>
#include <vector>

namespace gema {
namespace vision {

/**
 * @brief Abstract interface for a vision inference engine.
 *
 * Separates the "what" (inference) from the "how" (NPU, mock, or
 * software fallback).  Every implementation MUST be:
 *
 *   - RAII-safe   (resources acquired in ctor, released in dtor)
 *   - Thread-safe (infer() may be called from the consumer thread)
 *
 * @note This is the Injection Target for dependency inversion.
 *       Production code receives an RknnContext; SIL tests receive
 *       a MockEngine that returns canned primitives.
 */
class InferenceEngine {
public:
    virtual ~InferenceEngine() = default;

    /**
     * @brief Run inference on a single camera frame.
     *
     * @param frame  Input BGR image (typically from the camera pipeline).
     * @return PrimitiveBatch  Zero or more detected visual primitives.
     *
     * Implementations MUST not throw on an empty / all-black frame;
     * they MUST return an empty vector instead.
     */
    virtual PrimitiveBatch infer(const cv::Mat& frame) = 0;

    /**
     * @brief Load (or reload) a model from disk.
     *
     * @param path  Filesystem path to the model artefact
     *              (e.g. ".rknn" for Rockchip NPU).
     * @return true  if the model was loaded successfully.
     * @return false on failure (file not found, incompatible format, etc.).
     *
     * @post A subsequent call to infer() uses the newly loaded model.
     */
    virtual bool load_model(const std::string& path) = 0;
};

}  // namespace vision
}  // namespace gema
