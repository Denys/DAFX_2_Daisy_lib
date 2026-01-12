// Unit Tests for Peak Filter
// Tests initialization, parameter setting, and basic processing

#include "filters/peakfilter.h"
#include <gtest/gtest.h>

using namespace daisysp;

class PeakFilterTest : public ::testing::Test {
protected:
  PeakFilter filter;

  void SetUp() override { filter.Init(48000.0f); }
};

// Test initialization
TEST_F(PeakFilterTest, Initialization) {
  EXPECT_GT(filter.GetFrequency(), 0.0f);
  EXPECT_GT(filter.GetBandwidth(), 0.0f);
  EXPECT_FLOAT_EQ(filter.GetGain(), 0.0f);
}

// Test parameter setting
TEST_F(PeakFilterTest, ParameterSetting) {
  filter.SetFrequency(1000.0f);
  EXPECT_FLOAT_EQ(filter.GetFrequency(), 1000.0f);

  filter.SetBandwidth(200.0f);
  EXPECT_FLOAT_EQ(filter.GetBandwidth(), 200.0f);

  filter.SetGain(6.0f);
  EXPECT_FLOAT_EQ(filter.GetGain(), 6.0f);
}

// Test zero input
TEST_F(PeakFilterTest, ZeroInput) {
  float in = 0.0f;
  float out = filter.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test unity gain
TEST_F(PeakFilterTest, UnityGain) {
  filter.SetGain(0.0f);
  float in = 0.5f;
  for (int i = 0; i < 100; i++) {
    filter.Process(in);
  }
  float out = filter.Process(in);
  EXPECT_NEAR(out, in, 0.1f);
}

// Test output is finite
TEST_F(PeakFilterTest, OutputRange) {
  filter.SetFrequency(1000.0f);
  filter.SetBandwidth(100.0f);
  filter.SetGain(12.0f);

  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = filter.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test different sample rates
TEST_F(PeakFilterTest, DifferentSampleRates) {
  EXPECT_NO_THROW(filter.Init(44100.0f));
  EXPECT_NO_THROW(filter.Init(48000.0f));
  EXPECT_NO_THROW(filter.Init(96000.0f));
}

// Test Bandwidth range
TEST_F(PeakFilterTest, BandwidthRange) {
  EXPECT_NO_THROW(filter.SetBandwidth(50.0f));
  EXPECT_NO_THROW(filter.SetBandwidth(100.0f));
  EXPECT_NO_THROW(filter.SetBandwidth(1000.0f));
}
