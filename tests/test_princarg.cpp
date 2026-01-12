// Unit Tests for Phase Unwrap (princarg) Utility
// Tests phase wrapping matches MATLAB implementation

#include "utility/princarg.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class PrincargTest : public ::testing::Test {
protected:
  const float tolerance = 1e-5f;
};

// Test basic wrapping within range
TEST_F(PrincargTest, WithinRange) {
  // Values already in (-π, π] should be unchanged
  EXPECT_NEAR(Princarg(0.0f), 0.0f, tolerance);
  EXPECT_NEAR(Princarg(1.0f), 1.0f, tolerance);
  EXPECT_NEAR(Princarg(-1.0f), -1.0f, tolerance);
  // π is at the boundary - may be exactly π or wrap to -π due to float
  // precision
  float pi_result = Princarg(static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(pi_result) >= static_cast<float>(M_PI) - tolerance);
}

// Test wrapping positive overflow
TEST_F(PrincargTest, PositiveOverflow) {
  // 2π should wrap to ~0
  EXPECT_NEAR(Princarg(TWOPI), 0.0f, tolerance);

  // π + 0.5 should become -(π - 0.5)
  float input = static_cast<float>(M_PI) + 0.5f;
  float expected = input - TWOPI; // Wrap back
  EXPECT_NEAR(Princarg(input), expected, tolerance);

  // 3π should wrap to -π or π (boundary)
  float result_3pi = Princarg(3.0f * static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(result_3pi) >= static_cast<float>(M_PI) - tolerance);
}

// Test wrapping negative overflow
TEST_F(PrincargTest, NegativeOverflow) {
  // -2π should wrap to ~0
  EXPECT_NEAR(Princarg(-TWOPI), 0.0f, tolerance);

  // -π - 0.5 should become π - 0.5
  float input = -static_cast<float>(M_PI) - 0.5f;
  float expected = input + TWOPI;
  EXPECT_NEAR(Princarg(input), expected, tolerance);
}

// Test multiple wraps
TEST_F(PrincargTest, MultipleWraps) {
  // 4π should wrap to ~0
  EXPECT_NEAR(Princarg(4.0f * static_cast<float>(M_PI)), 0.0f, tolerance);

  // -4π should wrap to ~0
  EXPECT_NEAR(Princarg(-4.0f * static_cast<float>(M_PI)), 0.0f, tolerance);

  // 5π should wrap to -π or π (boundary)
  float result_5pi = Princarg(5.0f * static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(result_5pi) >= static_cast<float>(M_PI) - tolerance);
}

// Test MATLAB compatibility
// MATLAB: princarg(phase_in) = mod(phase_in+pi,-2*pi) + pi
TEST_F(PrincargTest, MATLABCompatibility) {
  // Test specific values from MATLAB
  // In MATLAB: princarg(0) = 0
  EXPECT_NEAR(Princarg(0.0f), 0.0f, tolerance);

  // In MATLAB: princarg(pi) = pi (but may be -pi due to float precision)
  float pi_result = Princarg(static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(pi_result) >= static_cast<float>(M_PI) - tolerance);

  // In MATLAB: princarg(-pi) is ambiguous (edge case)
  float neg_pi_result = Princarg(-static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(neg_pi_result) >= static_cast<float>(M_PI) - tolerance);
}

// Test array processing
TEST_F(PrincargTest, ArrayProcessing) {
  float phases[4] = {0.0f, TWOPI, -TWOPI, 3.0f * static_cast<float>(M_PI)};
  PrincargArray(phases, 4);

  EXPECT_NEAR(phases[0], 0.0f, tolerance);
  EXPECT_NEAR(phases[1], 0.0f, tolerance);
  EXPECT_NEAR(phases[2], 0.0f, tolerance);
  // 3π wraps to boundary (±π)
  EXPECT_TRUE(std::fabs(phases[3]) >= static_cast<float>(M_PI) - tolerance);
}

// Test phase difference
TEST_F(PrincargTest, PhaseDifference) {
  // Simple difference
  EXPECT_NEAR(PhaseDiff(1.0f, 0.5f), 0.5f, tolerance);

  // Difference that wraps
  float phase1 = static_cast<float>(M_PI) - 0.1f;
  float phase2 = -static_cast<float>(M_PI) + 0.1f;
  float diff = PhaseDiff(phase1, phase2);
  // The difference should be close to -0.2 (wrapped)
  EXPECT_NEAR(diff, -0.2f, tolerance);
}

// Test edge cases
TEST_F(PrincargTest, EdgeCases) {
  // Very large values should be finite
  EXPECT_TRUE(std::isfinite(Princarg(1000.0f * static_cast<float>(M_PI))));

  // Very small values should be unchanged
  EXPECT_NEAR(Princarg(1e-10f), 1e-10f, tolerance);

  // Boundary at π - result should be ±π
  float boundary_result = Princarg(static_cast<float>(M_PI));
  EXPECT_TRUE(std::fabs(boundary_result) >=
              static_cast<float>(M_PI) - tolerance);
}
