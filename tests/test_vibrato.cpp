// Unit Tests for Vibrato Effect
// Tests initialization, parameter setting, and basic processing

#include "modulation/vibrato.h"
#include <gtest/gtest.h>


using namespace daisysp;

class VibratoTest : public ::testing::Test {
protected:
  Vibrato vibrato;

  void SetUp() override { vibrato.Init(48000.0f); }
};

// Test initialization
TEST_F(VibratoTest, Initialization) {
  EXPECT_GT(vibrato.GetFrequency(), 0.0f);
  EXPECT_GT(vibrato.GetWidth(), 0.0f);
}

// Test parameter setting
TEST_F(VibratoTest, ParameterSetting) {
  vibrato.SetFrequency(5.0f);
  EXPECT_FLOAT_EQ(vibrato.GetFrequency(), 5.0f);

  vibrato.SetWidth(0.01f);
  EXPECT_FLOAT_EQ(vibrato.GetWidth(), 0.01f);
}

// Test zero input
TEST_F(VibratoTest, ZeroInput) {
  float in = 0.0f;
  float out = vibrato.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test output is finite
TEST_F(VibratoTest, OutputRange) {
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = vibrato.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test that vibrato produces output different from input (modulation)
TEST_F(VibratoTest, ModulationEffect) {
  vibrato.SetFrequency(5.0f);
  vibrato.SetWidth(0.005f);

  float in = 0.5f;
  bool found_different = false;

  for (int i = 0; i < 1000; i++) {
    float out = vibrato.Process(in);
    if (std::abs(out - in) > 0.01f) {
      found_different = true;
      break;
    }
  }

  EXPECT_TRUE(found_different);
}

// Test different sample rates
TEST_F(VibratoTest, DifferentSampleRates) {
  EXPECT_NO_THROW(vibrato.Init(44100.0f));
  EXPECT_NO_THROW(vibrato.Init(48000.0f));
  EXPECT_NO_THROW(vibrato.Init(96000.0f));
}

// Test frequency range
TEST_F(VibratoTest, FrequencyRange) {
  EXPECT_NO_THROW(vibrato.SetFrequency(0.1f));
  EXPECT_NO_THROW(vibrato.SetFrequency(5.0f));
  EXPECT_NO_THROW(vibrato.SetFrequency(20.0f));
}
