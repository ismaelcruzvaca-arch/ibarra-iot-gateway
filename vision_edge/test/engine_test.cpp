#include <gtest/gtest.h>
#include "../src/engine.hpp"

TEST(VisionEngineTest, DevernayParabolaPeak) {
    // A synthetic defect peak where center is highest
    std::vector<float> defect = {0.1f, 0.5f, 0.9f, 0.8f, 0.2f};
    
    // We expect the peak to shift slightly towards the right (index 3) since 0.8 > 0.5
    float subpixel = vision::engine::processDefectBuffer(defect, 2);
    
    // Mathematically:
    // left = 0.5, center = 0.9, right = 0.8
    // denominator = 2 * (0.5 - 1.8 + 0.8) = 2 * (-0.5) = -1.0
    // numerator = 0.5 - 0.8 = -0.3
    // result = -0.3 / -1.0 = 0.3
    
    EXPECT_FLOAT_EQ(subpixel, 0.3f);
}

TEST(VisionEngineTest, OutOfBoundsPeak) {
    std::vector<float> defect = {0.1f, 0.5f, 0.9f};
    // Peak index 0 is invalid since it has no left neighbor
    EXPECT_FLOAT_EQ(vision::engine::processDefectBuffer(defect, 0), 0.0f);
    // Peak index 2 is invalid since it has no right neighbor
    EXPECT_FLOAT_EQ(vision::engine::processDefectBuffer(defect, 2), 0.0f);
}
