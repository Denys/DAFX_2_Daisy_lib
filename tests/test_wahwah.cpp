// Unit Tests for Wah Wah Effect
// Tests initialization, parameter setting, and basic processing

#include "effects/wahwah.h"
#include <gtest/gtest.h>

using namespace daisysp;

class WahWahTest : public ::testing::Test {
protected:
  WahWah wah;

  void SetUp() override { wah.Init(48000.0f); }
};

// Test initialization
TEST_F(WahWahTest, Initialization) {
  EXPECT_GT(wah.GetFrequency(), 0.0f);
  EXPECT_GT(wah.GetDepth(), 0.0f);
  EXPECT_GT(wah.GetQ(), 0.0f);
}

// Test parameter setting
TEST_F(WahWahTest, ParameterSetting) {
  wah.SetFrequency(2.0f);
  EXPECT_FLOAT_EQ(wah.GetFrequency(), 2.0f);

  wah.SetDepth(0.5f);
  EXPECT_FLOAT_EQ(wah.GetDepth(), 0.5f);

  wah.SetQ(5.0f);
  EXPECT_FLOAT_EQ(wah.GetQ(), 5.0f);
}

// Test zero input
TEST_F(WahWahTest, ZeroInput) {
  float in = 0.0f;
  float out = wah.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test output is finite
TEST_F(WahWahTest, OutputRange) {
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = wah.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test that wah produces filtering effect
TEST_F(WahWahTest, FilteringEffect) {
  wah.SetFrequency(5.0f);
  wah.SetDepth(1.0f);
  wah.SetQ(10.0f);

  float in = 0.5f;
  float max_out = 0.0f;
  float min_out = 1.0f;

  // Process enough samples to see filter sweep
  for (int i = 0; i < 10000; i++) {
    float out = wah.Process(in);
    max_out = std::max(max_out, std::abs(out));
    min_out = std::min(min_out, std::abs(out));
  }

  // Output should vary due to resonant sweep
  EXPECT_GT(max_out - min_out, 0.01f);
}

// Test different sample rates
TEST_F(WahWahTest, DifferentSampleRates) {
  EXPECT_NO_THROW(wah.Init(44100.0f));
  EXPECT_NO_THROW(wah.Init(48000.0f));
  EXPECT_NO_THROW(wah.Init(96000.0f));
}

// Test parameter ranges
TEST_F(WahWahTest, ParameterRanges) {
  EXPECT_NO_THROW(wah.SetFrequency(0.5f));
  EXPECT_NO_THROW(wah.SetFrequency(10.0f));
  EXPECT_NO_THROW(wah.SetDepth(0.0f));
  EXPECT_NO_THROW(wah.SetDepth(1.0f));
  EXPECT_NO_THROW(wah.SetQ(1.0f));
  EXPECT_NO_THROW(wah.SetQ(20.0f));
}
