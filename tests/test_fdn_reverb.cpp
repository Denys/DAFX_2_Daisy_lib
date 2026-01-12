// Unit Tests for FDN Reverb
// Tests feedback delay network behavior

#include "effects/fdn_reverb.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class FDNReverbTest : public ::testing::Test {
protected:
  static constexpr size_t kMaxDelay = 8192;
  static constexpr float kSampleRate = 48000.0f;
  FDNReverb<kMaxDelay> reverb;
  const float tolerance = 1e-5f;

  void SetUp() override { reverb.Init(kSampleRate); }
};

// Test initialization
TEST_F(FDNReverbTest, Initialization) {
  EXPECT_FLOAT_EQ(reverb.GetDecay(), 0.97f);
  EXPECT_FLOAT_EQ(reverb.GetMix(), 0.5f);
  EXPECT_FLOAT_EQ(reverb.GetDamping(), 0.3f);
}

// Test parameter setters
TEST_F(FDNReverbTest, ParameterSetters) {
  reverb.SetDecay(0.9f);
  EXPECT_FLOAT_EQ(reverb.GetDecay(), 0.9f);

  reverb.SetMix(0.7f);
  EXPECT_FLOAT_EQ(reverb.GetMix(), 0.7f);

  reverb.SetDamping(0.5f);
  EXPECT_FLOAT_EQ(reverb.GetDamping(), 0.5f);

  reverb.SetDelayScale(1.5f);
  EXPECT_FLOAT_EQ(reverb.GetDelayScale(), 1.5f);
}

// Test decay clamping
TEST_F(FDNReverbTest, DecayClamping) {
  reverb.SetDecay(1.5f);
  EXPECT_LE(reverb.GetDecay(), 0.999f);

  reverb.SetDecay(-0.5f);
  EXPECT_GE(reverb.GetDecay(), 0.0f);
}

// Test mix clamping
TEST_F(FDNReverbTest, MixClamping) {
  reverb.SetMix(1.5f);
  EXPECT_LE(reverb.GetMix(), 1.0f);

  reverb.SetMix(-0.5f);
  EXPECT_GE(reverb.GetMix(), 0.0f);
}

// Test impulse response (should produce decaying tail)
TEST_F(FDNReverbTest, ImpulseResponse) {
  reverb.SetDecay(0.95f);
  reverb.SetMix(1.0f); // Full wet

  // Send impulse
  float out = reverb.Process(1.0f);

  // Process for reverb tail
  float energy = 0.0f;
  for (int i = 0; i < 10000; i++) {
    out = reverb.Process(0.0f);
    energy += out * out;
  }

  // Should have significant reverb energy
  EXPECT_GT(energy, 0.1f);
}

// Test decay behavior
TEST_F(FDNReverbTest, DecayBehavior) {
  reverb.SetMix(1.0f);

  // Test with high decay
  reverb.SetDecay(0.99f);
  reverb.Clear();
  reverb.Process(1.0f);

  float energy_high = 0.0f;
  for (int i = 0; i < 5000; i++) {
    float out = reverb.Process(0.0f);
    energy_high += out * out;
  }

  // Test with low decay
  reverb.SetDecay(0.8f);
  reverb.Clear();
  reverb.Process(1.0f);

  float energy_low = 0.0f;
  for (int i = 0; i < 5000; i++) {
    float out = reverb.Process(0.0f);
    energy_low += out * out;
  }

  // Higher decay should produce more energy
  EXPECT_GT(energy_high, energy_low);
}

// Test dry/wet mix
TEST_F(FDNReverbTest, DryWetMix) {
  reverb.SetDecay(0.95f);

  // Full dry
  reverb.SetMix(0.0f);
  float dry_out = reverb.Process(1.0f);
  EXPECT_FLOAT_EQ(dry_out, 1.0f);

  // Full wet - output depends on reverb state
  reverb.Clear();
  reverb.SetMix(1.0f);
  float wet_out = reverb.Process(1.0f);
  EXPECT_NE(wet_out, 1.0f); // Should differ from dry
}

// Test stereo processing
TEST_F(FDNReverbTest, StereoProcessing) {
  reverb.SetMix(1.0f);
  reverb.SetDecay(0.95f);

  float out_l, out_r;

  // Send impulse to left only
  reverb.ProcessStereo(1.0f, 0.0f, &out_l, &out_r);

  // Both outputs should have signal (due to feedback matrix)
  float energy_l = 0.0f;
  float energy_r = 0.0f;

  for (int i = 0; i < 2000; i++) {
    reverb.ProcessStereo(0.0f, 0.0f, &out_l, &out_r);
    energy_l += out_l * out_l;
    energy_r += out_r * out_r;
  }

  // Both channels should have reverb
  EXPECT_GT(energy_l, 0.01f);
  EXPECT_GT(energy_r, 0.01f);
}

// Test RT60 setting
TEST_F(FDNReverbTest, ReverbTimeRT60) {
  reverb.SetReverbTime(2.0f); // 2 second RT60

  // Decay should be set based on RT60
  float decay = reverb.GetDecay();
  EXPECT_GT(decay, 0.9f);
  EXPECT_LT(decay, 1.0f);
}

// Test clear function
TEST_F(FDNReverbTest, ClearFunction) {
  reverb.SetMix(1.0f);

  // Fill with signal
  for (int i = 0; i < 1000; i++) {
    reverb.Process(0.5f);
  }

  reverb.Clear();

  // After clear, output should be minimal
  reverb.SetDecay(0.0f);
  float out = reverb.Process(0.0f);
  EXPECT_NEAR(out, 0.0f, 0.01f);
}
