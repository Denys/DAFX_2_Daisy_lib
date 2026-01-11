// Unit Tests for Tone Stack Effect
// Tests initialization, parameter setting, and basic processing

#include "effects/tonestack.h"
#include <gtest/gtest.h>


using namespace daisysp;

class ToneStackTest : public ::testing::Test {
protected:
  ToneStack tonestack;

  void SetUp() override { tonestack.Init(48000.0f); }
};

// Test initialization
TEST_F(ToneStackTest, Initialization) {
  // Default values should be set
  EXPECT_GE(tonestack.GetBass(), 0.0f);
  EXPECT_LE(tonestack.GetBass(), 1.0f);
  EXPECT_GE(tonestack.GetMid(), 0.0f);
  EXPECT_LE(tonestack.GetMid(), 1.0f);
  EXPECT_GE(tonestack.GetTreble(), 0.0f);
  EXPECT_LE(tonestack.GetTreble(), 1.0f);
}

// Test parameter setting
TEST_F(ToneStackTest, ParameterSetting) {
  tonestack.SetBass(0.3f);
  EXPECT_FLOAT_EQ(tonestack.GetBass(), 0.3f);

  tonestack.SetMid(0.5f);
  EXPECT_FLOAT_EQ(tonestack.GetMid(), 0.5f);

  tonestack.SetTreble(0.7f);
  EXPECT_FLOAT_EQ(tonestack.GetTreble(), 0.7f);
}

// Test zero input
TEST_F(ToneStackTest, ZeroInput) {
  float in = 0.0f;
  float out = tonestack.Process(in);
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test flat response
TEST_F(ToneStackTest, FlatResponse) {
  // Set all controls to middle position
  tonestack.SetBass(0.5f);
  tonestack.SetMid(0.5f);
  tonestack.SetTreble(0.5f);

  float in = 0.5f;
  // Process several samples to settle
  for (int i = 0; i < 100; i++) {
    tonestack.Process(in);
  }
  float out = tonestack.Process(in);

  // Output should be in reasonable range
  EXPECT_GT(std::abs(out), 0.01f);
  EXPECT_LT(std::abs(out), 2.0f);
}

// Test output is finite
TEST_F(ToneStackTest, OutputRange) {
  for (int i = -10; i <= 10; i++) {
    float in = static_cast<float>(i) * 0.1f;
    float out = tonestack.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test different sample rates
TEST_F(ToneStackTest, DifferentSampleRates) {
  EXPECT_NO_THROW(tonestack.Init(44100.0f));
  EXPECT_NO_THROW(tonestack.Init(48000.0f));
  EXPECT_NO_THROW(tonestack.Init(96000.0f));
}

// Test parameter ranges
TEST_F(ToneStackTest, ParameterRanges) {
  EXPECT_NO_THROW(tonestack.SetBass(0.0f));
  EXPECT_NO_THROW(tonestack.SetBass(1.0f));
  EXPECT_NO_THROW(tonestack.SetMid(0.0f));
  EXPECT_NO_THROW(tonestack.SetMid(1.0f));
  EXPECT_NO_THROW(tonestack.SetTreble(0.0f));
  EXPECT_NO_THROW(tonestack.SetTreble(1.0f));
}

// Test that different EQ settings produce different outputs
TEST_F(ToneStackTest, EQVariation) {
  float in = 0.5f;

  // Bass boosted
  tonestack.SetBass(1.0f);
  tonestack.SetMid(0.5f);
  tonestack.SetTreble(0.5f);
  for (int i = 0; i < 100; i++)
    tonestack.Process(in);
  float bass_out = tonestack.Process(in);

  // Re-init and treble boosted
  tonestack.Init(48000.0f);
  tonestack.SetBass(0.5f);
  tonestack.SetMid(0.5f);
  tonestack.SetTreble(1.0f);
  for (int i = 0; i < 100; i++)
    tonestack.Process(in);
  float treble_out = tonestack.Process(in);

  // Different EQ should produce different output
  EXPECT_NE(bass_out, treble_out);
}
