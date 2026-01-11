// Unit Tests for High Shelving Filter
// Tests initialization, parameter setting, and basic processing

#include "filters/highshelving.h"
#include <gtest/gtest.h>


using namespace daisysp;

class HighShelvingTest : public ::testing::Test {
protected:
  HighShelving filter;

  void SetUp() override { filter.Init(48000.0f); }
};

// Test initialization
TEST_F(HighShelvingTest, Initialization) {
  EXPECT_GT(filter.GetFrequency(), 0.0f);
  EXPECT_FLOAT_EQ(filter.GetGain(), 0.0f);
}

// Test parameter setting
TEST_F(HighShelvingTest, ParameterSetting) {
  filter.SetFrequency(4000.0f);
  EXPECT_FLOAT_EQ(filter.GetFrequency(), 4000.0f);

  filter.SetGain(6.0f);
  EXPECT_FLOAT_EQ(filter.GetGain(), 6.0f);
}

// Test zero input
TEST_F(HighShelvingTest, ZeroInput) {
  float in = 0.0f;
  float out = filter.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test unity gain
TEST_F(HighShelvingTest, UnityGain) {
  filter.SetGain(0.0f);
  float in = 0.5f;
  for (int i = 0; i < 100; i++) {
    filter.Process(in);
  }
  float out = filter.Process(in);
  EXPECT_NEAR(out, in, 0.05f);
}

// Test output range
TEST_F(HighShelvingTest, OutputRange) {
  filter.SetGain(12.0f);
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = filter.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test different sample rates
TEST_F(HighShelvingTest, DifferentSampleRates) {
  EXPECT_NO_THROW(filter.Init(44100.0f));
  EXPECT_NO_THROW(filter.Init(48000.0f));
  EXPECT_NO_THROW(filter.Init(96000.0f));
}
