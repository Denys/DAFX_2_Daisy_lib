// Unit Tests for Universal Comb Filter
// Tests comb filter configurations and delay behavior

#include "effects/universal_comb.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class UniversalCombTest : public ::testing::Test {
protected:
  static constexpr size_t kMaxDelay = 2048;
  static constexpr float kSampleRate = 48000.0f;
  UniversalComb<kMaxDelay> comb;
  const float tolerance = 1e-5f;

  void SetUp() override { comb.Init(kSampleRate); }
};

// Test initialization
TEST_F(UniversalCombTest, Initialization) {
  EXPECT_FLOAT_EQ(comb.GetSampleRate(), kSampleRate);
  EXPECT_EQ(comb.GetDelay(), 10); // Default
}

// Test parameter setters
TEST_F(UniversalCombTest, ParameterSetters) {
  comb.SetDelay(100);
  EXPECT_EQ(comb.GetDelay(), 100);

  comb.SetDelayMs(10.0f);
  EXPECT_EQ(comb.GetDelay(), 480); // 10ms at 48kHz

  comb.SetFeedback(0.7f);
  EXPECT_FLOAT_EQ(comb.GetFeedback(), 0.7f);

  comb.SetFeedforward(0.5f);
  EXPECT_FLOAT_EQ(comb.GetFeedforward(), 0.5f);

  comb.SetBlend(0.3f);
  EXPECT_FLOAT_EQ(comb.GetBlend(), 0.3f);
}

// Test feedback clamping
TEST_F(UniversalCombTest, FeedbackClamping) {
  comb.SetFeedback(1.5f);
  EXPECT_LE(comb.GetFeedback(), 0.999f);

  comb.SetFeedback(-1.5f);
  EXPECT_GE(comb.GetFeedback(), -0.999f);
}

// Test impulse response (FIR comb)
TEST_F(UniversalCombTest, FIRCombImpulseResponse) {
  comb.SetFIRComb();
  comb.SetDelay(10);

  // Send impulse
  float out = comb.Process(1.0f);

  // Immediate output should be blend * 1 + feedforward * 0
  // For FIR: blend=1, so out = 1
  EXPECT_FLOAT_EQ(out, 1.0f);

  // Process zeros until we hit the delay
  for (int i = 1; i < 10; i++) {
    out = comb.Process(0.0f);
    EXPECT_NEAR(out, 0.0f, tolerance);
  }

  // At delay = 10, we should see the delayed impulse
  out = comb.Process(0.0f);
  EXPECT_NEAR(out, 1.0f, tolerance); // feedforward * 1
}

// Test IIR comb - should repeat
TEST_F(UniversalCombTest, IIRCombRepeats) {
  comb.SetIIRComb(0.5f);
  comb.SetDelay(10);

  // Send impulse
  comb.Process(1.0f);

  // Process to first echo
  for (int i = 1; i < 10; i++) {
    comb.Process(0.0f);
  }
  float first_echo = comb.Process(0.0f);

  // Process to second echo
  for (int i = 0; i < 9; i++) {
    comb.Process(0.0f);
  }
  float second_echo = comb.Process(0.0f);

  // Second echo should be reduced by feedback
  EXPECT_LT(std::fabs(second_echo), std::fabs(first_echo));
}

// Test allpass configuration
TEST_F(UniversalCombTest, AllpassConfiguration) {
  comb.SetAllpass(0.5f);

  EXPECT_FLOAT_EQ(comb.GetFeedback(), 0.5f);
  EXPECT_FLOAT_EQ(comb.GetFeedforward(), -0.5f);
  EXPECT_FLOAT_EQ(comb.GetBlend(), 1.0f);
}

// Test delay clamping
TEST_F(UniversalCombTest, DelayClamping) {
  comb.SetDelay(10000); // Exceeds MaxDelay
  EXPECT_LE(comb.GetDelay(), kMaxDelay - 1);
}

// Test clear function
TEST_F(UniversalCombTest, ClearFunction) {
  // Fill with signal
  for (int i = 0; i < 100; i++) {
    comb.Process(1.0f);
  }

  comb.Clear();

  // After clear, output should be based only on new input
  comb.SetFIRComb();
  comb.SetDelay(10);
  comb.Process(0.0f);

  for (int i = 0; i < 20; i++) {
    float out = comb.Process(0.0f);
    EXPECT_NEAR(out, 0.0f, tolerance);
  }
}
