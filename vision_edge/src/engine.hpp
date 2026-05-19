#pragma once
#include <vector>
#include <cstddef>

namespace vision {
namespace engine {

/**
 * Calculates the sub-pixel peak (Devernay Parabola) given 3 points: left, center, right.
 * If compiled on ARM64, uses inline NEON assembly. Otherwise, falls back to standard C++.
 */
float calculateSubPixelPeak(float left, float center, float right);

/**
 * Processes a synthetic defect buffer, returning the computed sub-pixel peak.
 */
float processDefectBuffer(const std::vector<float>& buffer, std::size_t peak_index);

} // namespace engine
} // namespace vision
