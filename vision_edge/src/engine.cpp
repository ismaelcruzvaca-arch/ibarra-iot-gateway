#include "engine.hpp"

namespace vision {
namespace engine {

float calculateSubPixelPeak(float left, float center, float right) {
#ifdef __aarch64__
    // Inline ARM64 Assembly for Devernay Parabola:
    // delta = (left - right) / (2.0f * (left - 2.0f * center + right))
    float result = 0.0f;
    asm volatile(
        "fsub s0, %w1, %w3 \n\t"        // s0 = left - right
        "fmov s1, #2.0 \n\t"            // s1 = 2.0
        "fmul s2, s1, %w2 \n\t"         // s2 = 2.0 * center
        "fadd s3, %w1, %w3 \n\t"        // s3 = left + right
        "fsub s3, s3, s2 \n\t"          // s3 = (left + right) - 2.0 * center
        "fmul s3, s1, s3 \n\t"          // s3 = 2.0 * (left - 2.0 * center + right)
        "fdiv %w0, s0, s3 \n\t"         // result = s0 / s3
        : "=w" (result)
        : "w" (left), "w" (center), "w" (right)
        : "s0", "s1", "s2", "s3"
    );
    return result;
#else
    // Fallback for x86/CI environments ensuring portability
    float denominator = 2.0f * (left - 2.0f * center + right);
    if (denominator == 0.0f) return 0.0f;
    return (left - right) / denominator;
#endif
}

float processDefectBuffer(const std::vector<float>& buffer, std::size_t peak_index) {
    if (peak_index == 0 || peak_index >= buffer.size() - 1) {
        return 0.0f; // Prevent out of bounds
    }
    return calculateSubPixelPeak(buffer[peak_index - 1], buffer[peak_index], buffer[peak_index + 1]);
}

} // namespace engine
} // namespace vision
