// Unit Tests for LP-IIR Comb Filter
// Tests damped comb filter behavior for reverb applications

#include "effects/lp_iir_comb.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class LPIIRCombTest : public ::testing::Test {
protected:
  static constexpr size_t kMaxDelay = 4096;
  static constexpr float kSampleRate = 48000.0f;
  LPIIRComb<kMaxDelay> comb;
  const float tolerance = 1e-5f;

  void SetUp() override { comb.Init(kSampleRate); }
};

// Test initialization
TEST_F(LPIIRCombTest, Initialization) {
  EXPECT_FLOAT_EQ(comb.GetSampleRate(), kSampleRate);
  EXPECT_EQ(comb.GetDelay(), 100); // Default
  EXPECT_FLOAT_EQ(comb.GetFeedback(), 0.7f);
  EXPECT_FLOAT_EQ(comb.GetDamping(), 0.3f);
}

// Test parameter setters
TEST_F(LPIIRCombTest, ParameterSetters) {
  comb.SetDelay(200);
  EXPECT_EQ(comb.GetDelay(), 200);

  comb.SetDelayMs(50.0f);
  EXPECT_EQ(comb.GetDelay(), 2400); // 50ms at 48kHz

  comb.SetFeedback(0.9f);
  EXPECT_FLOAT_EQ(comb.GetFeedback(), 0.9f);

  comb.SetDamping(0.5f);
  EXPECT_FLOAT_EQ(comb.GetDamping(), 0.5f);
}

// Test feedback clamping
TEST_F(LPIIRCombTest, FeedbackClamping) {
  comb.SetFeedback(1.5f);
  EXPECT_LE(comb.GetFeedback(), 0.999f);

  comb.SetFeedback(-1.5f);
  EXPECT_GE(comb.GetFeedback(), -0.999f);
}

// Test damping clamping
TEST_F(LPIIRCombTest, DampingClamping) {
  comb.SetDamping(1.5f);
  EXPECT_LE(comb.GetDamping(), 0.999f);

  comb.SetDamping(-0.5f);
  EXPECT_GE(comb.GetDamping(), 0.0f);
}

// Test impulse response
TEST_F(LPIIRCombTest, ImpulseResponse) {
  comb.SetDelay(10);
  comb.SetFeedback(0.5f);
  comb.SetDamping(0.0f); // No LP filtering

  // Send impulse
  float out = comb.Process(1.0f);
  EXPECT_FLOAT_EQ(out, 1.0f); // Direct path

  // Process until first echo
  for (int i = 1; i < 10; i++) {
    out = comb.Process(0.0f);
  }

  // First echo
  out = comb.Process(0.0f);
  EXPECT_GT(std::fabs(out), 0.0f);
}

// Test damping reduces high frequencies
TEST_F(LPIIRCombTest, DampingReducesEnergy) {
  comb.SetDelay(10);
  comb.SetFeedback(0.8f);

  // Test with no damping
  comb.SetDamping(0.0f);
  comb.Clear();
  comb.Process(1.0f);

  float energy_no_damp = 0.0f;
  for (int i = 0; i < 200; i++) {
    float out = comb.Process(0.0f);
    energy_no_damp += out * out;
  }

  // Test with damping
  comb.SetDamping(0.5f);
  comb.Clear();
  comb.Process(1.0f);

  float energy_with_damp = 0.0f;
  for (int i = 0; i < 200; i++) {
    float out = comb.Process(0.0f);
    energy_with_damp += out * out;
  }

  // Damped version should have less energy (faster decay)
  EXPECT_LT(energy_with_damp, energy_no_damp);
}

// Test decay over time
TEST_F(LPIIRCombTest, DecayOverTime) {
  comb.SetDelay(20);
  comb.SetFeedback(0.7f);
  comb.SetDamping(0.2f);

  // Send impulse
  comb.Process(1.0f);

  // Record peak amplitude over several echoes
  float peaks[5] = {0};
  int echo_count = 0;

  for (int i = 0; i < 300 && echo_count < 5; i++) {
    float out = std::fabs(comb.Process(0.0f));
    if (i % 20 == 0 && i > 0) {
      peaks[echo_count++] = out;
    }
  }

  // Each subsequent echo should be smaller
  for (int i = 1; i < echo_count; i++) {
    EXPECT_LE(peaks[i], peaks[i - 1] * 1.1f); // Allow small tolerance
  }
}

// Test clear function
TEST_F(LPIIRCombTest, ClearFunction) {
  // Fill with signal
  for (int i = 0; i < 100; i++) {
    comb.Process(1.0f);
  }

  comb.Clear();

  // After clearing, should have minimal output
  comb.SetFeedback(0.0f);
  for (int i = 0; i < 50; i++) {
    float out = comb.Process(0.0f);
    EXPECT_NEAR(out, 0.0f, tolerance);
  }
}

// Test reverb time configuration
TEST_F(LPIIRCombTest, ReverbTimeConfiguration) {
  comb.SetReverbTime(2.0f, 50.0f); // 2 second RT60, 50ms delay

  EXPECT_EQ(comb.GetDelay(), 2400); // 50ms at 48kHz
  // Feedback should be calculated for RT60
  float fb = comb.GetFeedback();
  EXPECT_GT(fb, 0.0f);
  EXPECT_LT(fb, 1.0f);
}
