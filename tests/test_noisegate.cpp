// Unit Tests for Noise Gate
// Tests initialization, parameter setting, and basic processing

#include "dynamics/noisegate.h"
#include <gtest/gtest.h>


using namespace daisysp;

class NoiseGateTest : public ::testing::Test {
protected:
  NoiseGate gate;

  void SetUp() override { gate.Init(48000.0f); }
};

// Test initialization
TEST_F(NoiseGateTest, Initialization) {
  EXPECT_LT(gate.GetThreshold(), 0.0f); // Threshold should be negative dB
  EXPECT_GT(gate.GetHoldTime(), 0.0f);
  EXPECT_GT(gate.GetAttackTime(), 0.0f);
  EXPECT_GT(gate.GetReleaseTime(), 0.0f);
}

// Test parameter setting
TEST_F(NoiseGateTest, ParameterSetting) {
  gate.SetThreshold(-30.0f);
  EXPECT_FLOAT_EQ(gate.GetThreshold(), -30.0f);

  gate.SetHoldTime(0.2f);
  EXPECT_FLOAT_EQ(gate.GetHoldTime(), 0.2f);

  gate.SetAttackTime(0.01f);
  EXPECT_FLOAT_EQ(gate.GetAttackTime(), 0.01f);

  gate.SetReleaseTime(0.5f);
  EXPECT_FLOAT_EQ(gate.GetReleaseTime(), 0.5f);

  gate.SetAlpha(0.95f);
  EXPECT_FLOAT_EQ(gate.GetAlpha(), 0.95f);
}

// Test zero input
TEST_F(NoiseGateTest, ZeroInput) {
  float in = 0.0f;
  float out = gate.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test that loud signal passes through
TEST_F(NoiseGateTest, LoudSignalPassthrough) {
  gate.SetThreshold(-40.0f);
  float in = 0.5f; // Well above -40 dB

  // Process several samples to let gate open
  for (int i = 0; i < 1000; i++) {
    gate.Process(in);
  }

  float out = gate.Process(in);
  EXPECT_GT(std::abs(out), 0.1f); // Should pass through
}

// Test that quiet signal is gated
TEST_F(NoiseGateTest, QuietSignalGated) {
  gate.SetThreshold(-20.0f); // -20 dB threshold
  float in = 0.01f;          // Well below threshold

  // Process many samples to let gate close
  for (int i = 0; i < 10000; i++) {
    gate.Process(in);
  }

  float out = gate.Process(in);
  EXPECT_NEAR(out, 0.0f, 0.05f); // Should be gated
}

// Test output is finite
TEST_F(NoiseGateTest, OutputRange) {
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = gate.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test different sample rates
TEST_F(NoiseGateTest, DifferentSampleRates) {
  EXPECT_NO_THROW(gate.Init(44100.0f));
  EXPECT_NO_THROW(gate.Init(48000.0f));
  EXPECT_NO_THROW(gate.Init(96000.0f));
}

// Test parameter ranges
TEST_F(NoiseGateTest, ParameterRanges) {
  EXPECT_NO_THROW(gate.SetThreshold(-60.0f));
  EXPECT_NO_THROW(gate.SetThreshold(-20.0f));
  EXPECT_NO_THROW(gate.SetHoldTime(0.01f));
  EXPECT_NO_THROW(gate.SetHoldTime(1.0f));
  EXPECT_NO_THROW(gate.SetAttackTime(0.001f));
  EXPECT_NO_THROW(gate.SetReleaseTime(0.5f));
}
