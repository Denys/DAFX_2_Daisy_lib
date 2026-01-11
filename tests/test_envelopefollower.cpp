// Unit Tests for Envelope Follower Utility
// Tests initialization, attack/release, and peak/RMS modes

#include "utility/envelopefollower.h"
#include <cmath>
#include <gtest/gtest.h>


using namespace daisysp;

class EnvelopeFollowerTest : public ::testing::Test {
protected:
  EnvelopeFollower env;

  void SetUp() override { env.Init(48000.0f); }
};

// Test initialization
TEST_F(EnvelopeFollowerTest, Initialization) {
  EXPECT_NEAR(env.GetAttackTime(), 0.01f, 0.001f);
  EXPECT_NEAR(env.GetReleaseTime(), 0.1f, 0.001f);
  EXPECT_FLOAT_EQ(env.GetEnvelope(), 0.0f);
}

// Test parameter setting
TEST_F(EnvelopeFollowerTest, ParameterSetting) {
  env.SetAttackTime(0.005f);
  EXPECT_FLOAT_EQ(env.GetAttackTime(), 0.005f);

  env.SetReleaseTime(0.5f);
  EXPECT_FLOAT_EQ(env.GetReleaseTime(), 0.5f);

  env.SetMode(EnvelopeMode::RMS);
  EXPECT_EQ(env.GetMode(), EnvelopeMode::RMS);
}

// Test zero input
TEST_F(EnvelopeFollowerTest, ZeroInput) {
  float out = env.Process(0.0f);
  EXPECT_FLOAT_EQ(out, 0.0f);
}

// Test envelope rises with attack
TEST_F(EnvelopeFollowerTest, AttackPhase) {
  env.SetAttackTime(0.001f); // Fast attack

  float envelope = 0.0f;
  // Process constant input
  for (int i = 0; i < 100; i++) {
    envelope = env.Process(1.0f);
  }

  // Envelope should have risen significantly
  EXPECT_GT(envelope, 0.5f);
}

// Test envelope falls with release
TEST_F(EnvelopeFollowerTest, ReleasePhase) {
  env.SetAttackTime(0.001f); // Fast attack
  env.SetReleaseTime(0.1f);  // Slower release

  // First, build up envelope
  for (int i = 0; i < 500; i++) {
    env.Process(1.0f);
  }
  float peak = env.GetEnvelope();

  // Now let it decay
  for (int i = 0; i < 1000; i++) {
    env.Process(0.0f);
  }
  float decayed = env.GetEnvelope();

  EXPECT_LT(decayed, peak * 0.5f);
}

// Test peak mode with sine wave
TEST_F(EnvelopeFollowerTest, PeakMode) {
  env.SetMode(EnvelopeMode::Peak);
  env.SetAttackTime(0.001f);
  env.SetReleaseTime(0.01f);

  float max_envelope = 0.0f;

  // Process sine wave
  for (int i = 0; i < 1000; i++) {
    float sample = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f) * 0.5f;
    float envelope = env.Process(sample);
    max_envelope = std::max(max_envelope, envelope);
  }

  // Peak envelope should approach the sine amplitude
  EXPECT_NEAR(max_envelope, 0.5f, 0.1f);
}

// Test RMS mode with sine wave
TEST_F(EnvelopeFollowerTest, RMSMode) {
  env.SetMode(EnvelopeMode::RMS);
  env.SetAttackTime(0.01f);
  env.SetReleaseTime(0.05f);

  float envelope = 0.0f;

  // Process sine wave for a while
  for (int i = 0; i < 5000; i++) {
    float sample = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f) * 0.5f;
    envelope = env.Process(sample);
  }

  // RMS of sine wave is amplitude / sqrt(2)
  float expected_rms = 0.5f / std::sqrt(2.0f);
  EXPECT_NEAR(envelope, expected_rms, 0.05f);
}

// Test dB output
TEST_F(EnvelopeFollowerTest, DBOutput) {
  env.SetAttackTime(0.001f);

  // Process unity amplitude
  for (int i = 0; i < 500; i++) {
    env.Process(1.0f);
  }

  float db = env.ProcessDB(1.0f);
  EXPECT_NEAR(db, 0.0f, 1.0f); // Should be near 0 dB
}

// Test dB output with zero input
TEST_F(EnvelopeFollowerTest, DBOutputZero) {
  float db = env.ProcessDB(0.0f);
  EXPECT_LT(db, -60.0f); // Should be very negative
}

// Test reset
TEST_F(EnvelopeFollowerTest, Reset) {
  // Build up envelope
  for (int i = 0; i < 1000; i++) {
    env.Process(1.0f);
  }
  EXPECT_GT(env.GetEnvelope(), 0.5f);

  // Reset
  env.Reset();
  EXPECT_FLOAT_EQ(env.GetEnvelope(), 0.0f);
}

// Test output is always finite
TEST_F(EnvelopeFollowerTest, OutputFinite) {
  for (int i = -100; i <= 100; i++) {
    float in = static_cast<float>(i) * 0.01f;
    float out = env.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test SimpleEnvelopeFollower
TEST(SimpleEnvelopeFollowerTest, BasicOperation) {
  SimpleEnvelopeFollower simple;
  simple.Init(48000.0f, 0.01f);

  // Process some samples
  for (int i = 0; i < 1000; i++) {
    simple.Process(0.5f);
  }

  float envelope = simple.GetEnvelope();
  EXPECT_NEAR(envelope, 0.5f, 0.1f);
}

// Test different sample rates
TEST_F(EnvelopeFollowerTest, DifferentSampleRates) {
  EXPECT_NO_THROW(env.Init(44100.0f));
  EXPECT_NO_THROW(env.Init(48000.0f));
  EXPECT_NO_THROW(env.Init(96000.0f));
}
