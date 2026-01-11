// Unit Tests for Ring Modulator Effect
// Tests initialization, parameter setting, and basic processing

#include "modulation/ringmod.h"
#include <gtest/gtest.h>


using namespace daisysp;

class RingModTest : public ::testing::Test {
protected:
  RingMod ringmod;

  void SetUp() override { ringmod.Init(48000.0f); }
};

// Test initialization
TEST_F(RingModTest, Initialization) { EXPECT_GT(ringmod.GetFrequency(), 0.0f); }

// Test parameter setting
TEST_F(RingModTest, ParameterSetting) {
  ringmod.SetFrequency(440.0f);
  EXPECT_FLOAT_EQ(ringmod.GetFrequency(), 440.0f);

  ringmod.SetFrequency(1000.0f);
  EXPECT_FLOAT_EQ(ringmod.GetFrequency(), 1000.0f);
}

// Test zero input
TEST_F(RingModTest, ZeroInput) {
  float in = 0.0f;
  float out = ringmod.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test output is finite
TEST_F(RingModTest, OutputRange) {
  ringmod.SetFrequency(440.0f);
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = ringmod.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test modulation produces varying output
TEST_F(RingModTest, Modulation) {
  ringmod.SetFrequency(100.0f);
  float in = 1.0f;

  float min_out = 1.0f;
  float max_out = -1.0f;

  for (int i = 0; i < 500; i++) {
    float out = ringmod.Process(in);
    min_out = std::min(min_out, out);
    max_out = std::max(max_out, out);
  }

  // Ring mod should produce both positive and negative output
  EXPECT_LT(min_out, 0.0f);
  EXPECT_GT(max_out, 0.0f);
}

// Test different sample rates
TEST_F(RingModTest, DifferentSampleRates) {
  EXPECT_NO_THROW(ringmod.Init(44100.0f));
  EXPECT_NO_THROW(ringmod.Init(48000.0f));
  EXPECT_NO_THROW(ringmod.Init(96000.0f));
}

// Test frequency range
TEST_F(RingModTest, FrequencyRange) {
  EXPECT_NO_THROW(ringmod.SetFrequency(10.0f));
  EXPECT_NO_THROW(ringmod.SetFrequency(440.0f));
  EXPECT_NO_THROW(ringmod.SetFrequency(5000.0f));
}
