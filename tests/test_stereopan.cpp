// Unit Tests for Stereo Panning
// Tests initialization, parameter setting, and basic processing

#include "spatial/stereopan.h"
#include <gtest/gtest.h>

using namespace daisysp;

class StereoPanTest : public ::testing::Test {
protected:
  StereoPan panner;

  void SetUp() override { panner.Init(); }
};

// Test initialization
TEST_F(StereoPanTest, Initialization) {
  // Default pan should be center (0.0 in -1 to +1 range)
  EXPECT_FLOAT_EQ(panner.GetPan(), 0.0f);
}

// Test parameter setting
TEST_F(StereoPanTest, ParameterSetting) {
  panner.SetPan(-1.0f);
  EXPECT_FLOAT_EQ(panner.GetPan(), -1.0f);

  panner.SetPan(0.0f);
  EXPECT_FLOAT_EQ(panner.GetPan(), 0.0f);

  panner.SetPan(1.0f);
  EXPECT_FLOAT_EQ(panner.GetPan(), 1.0f);
}

// Test zero input
TEST_F(StereoPanTest, ZeroInput) {
  float in = 0.0f;
  float left, right;
  panner.Process(in, &left, &right);

  EXPECT_NEAR(left, 0.0f, 1e-6f);
  EXPECT_NEAR(right, 0.0f, 1e-6f);
}

// Test center pan (equal power)
TEST_F(StereoPanTest, CenterPan) {
  panner.SetPan(0.0f); // Center in -1 to +1 range
  float in = 1.0f;
  float left, right;
  panner.Process(in, &left, &right);

  // Both channels should be roughly equal at center
  EXPECT_NEAR(left, right, 0.01f);
}

// Test full left pan
TEST_F(StereoPanTest, FullLeftPan) {
  panner.SetPan(-1.0f); // Full left in -1 to +1 range
  float in = 1.0f;
  float left, right;
  panner.Process(in, &left, &right);

  // Left should be dominant, right should be small
  // Note: tangent law can produce negative gains
  EXPECT_GT(std::fabs(left), std::fabs(right));
  EXPECT_GT(std::fabs(left), 0.5f); // Left should be significant
}

// Test full right pan
TEST_F(StereoPanTest, FullRightPan) {
  panner.SetPan(1.0f);
  float in = 1.0f;
  float left, right;
  panner.Process(in, &left, &right);

  // Right should be dominant, left should be small
  // Note: tangent law can produce negative gains
  EXPECT_GT(std::fabs(right), std::fabs(left));
  EXPECT_GT(std::fabs(right), 0.5f); // Right should be significant
}

// Test output is finite
TEST_F(StereoPanTest, OutputRange) {
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float left, right;
    panner.Process(in, &left, &right);

    EXPECT_TRUE(std::isfinite(left));
    EXPECT_TRUE(std::isfinite(right));
  }
}

// Test pan range
TEST_F(StereoPanTest, PanRange) {
  EXPECT_NO_THROW(panner.SetPan(-1.0f));
  EXPECT_NO_THROW(panner.SetPan(-0.5f));
  EXPECT_NO_THROW(panner.SetPan(0.0f));
  EXPECT_NO_THROW(panner.SetPan(0.5f));
  EXPECT_NO_THROW(panner.SetPan(1.0f));
}
