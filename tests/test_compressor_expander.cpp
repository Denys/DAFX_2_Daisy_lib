// Unit Tests for Compressor/Expander
// Tests dynamics processing behavior

#include "dynamics/compressor_expander.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class CompressorExpanderTest : public ::testing::Test {
protected:
  static constexpr size_t kMaxDelay = 256;
  static constexpr float kSampleRate = 48000.0f;
  CompressorExpander<kMaxDelay> comp;
  const float tolerance = 0.01f;

  void SetUp() override { comp.Init(kSampleRate); }

  // Generate sine wave
  void GenerateSine(float *buffer, size_t length, float freq, float amp) {
    for (size_t i = 0; i < length; i++) {
      buffer[i] = amp * std::sin(2.0f * static_cast<float>(M_PI) * freq *
                                 static_cast<float>(i) / kSampleRate);
    }
  }
};

// Test initialization
TEST_F(CompressorExpanderTest, Initialization) {
  EXPECT_FLOAT_EQ(comp.GetCompThreshold(), -20.0f);
  EXPECT_FLOAT_EQ(comp.GetCompSlope(), 0.5f);
  EXPECT_FLOAT_EQ(comp.GetExpThreshold(), -40.0f);
  EXPECT_FLOAT_EQ(comp.GetExpSlope(), 2.0f);
}

// Test parameter setters
TEST_F(CompressorExpanderTest, ParameterSetters) {
  comp.SetCompThreshold(-10.0f);
  EXPECT_FLOAT_EQ(comp.GetCompThreshold(), -10.0f);

  comp.SetCompSlope(0.75f);
  EXPECT_FLOAT_EQ(comp.GetCompSlope(), 0.75f);

  comp.SetCompRatio(4.0f); // 4:1 = 0.75 slope
  EXPECT_FLOAT_EQ(comp.GetCompSlope(), 0.75f);

  comp.SetExpThreshold(-50.0f);
  EXPECT_FLOAT_EQ(comp.GetExpThreshold(), -50.0f);

  comp.SetAttackTime(0.01f);
  EXPECT_FLOAT_EQ(comp.GetAttackTime(), 0.01f);

  comp.SetReleaseTime(0.1f);
  EXPECT_FLOAT_EQ(comp.GetReleaseTime(), 0.1f);
}

// Test unity gain with no compression/expansion
TEST_F(CompressorExpanderTest, UnityGainBelowThresholds) {
  comp.SetCompThreshold(0.0f);   // Very high threshold
  comp.SetExpThreshold(-100.0f); // Very low threshold

  // Process a low-level signal (should be unity gain)
  float buffer[1024];
  GenerateSine(buffer, 1024, 440.0f, 0.1f);

  // Process through to stabilize
  for (size_t i = 0; i < 1024; i++) {
    comp.Process(buffer[i]);
  }

  // After settling, gain should be near 1
  EXPECT_NEAR(comp.GetCurrentGain(), 1.0f, 0.1f);
}

// Test compression reduces gain above threshold
TEST_F(CompressorExpanderTest, CompressionReducesGain) {
  comp.SetCompThreshold(-20.0f);
  comp.SetCompSlope(0.5f);       // 2:1 ratio
  comp.SetExpThreshold(-100.0f); // Disable expansion

  // Process a loud signal
  float buffer[4096];
  GenerateSine(buffer, 4096, 440.0f, 0.9f); // High amplitude

  for (size_t i = 0; i < 4096; i++) {
    comp.Process(buffer[i]);
  }

  // Gain should be reduced
  EXPECT_LT(comp.GetCurrentGain(), 1.0f);
  EXPECT_GT(comp.GetCurrentGain(), 0.0f);
}

// Test expansion reduces gain below threshold
TEST_F(CompressorExpanderTest, ExpansionReducesGain) {
  comp.SetCompThreshold(0.0f); // Disable compression
  comp.SetExpThreshold(-20.0f);
  comp.SetExpSlope(2.0f); // 1:2 ratio

  // Process a quiet signal
  float buffer[4096];
  GenerateSine(buffer, 4096, 440.0f, 0.01f); // Low amplitude

  for (size_t i = 0; i < 4096; i++) {
    comp.Process(buffer[i]);
  }

  // Expansion should reduce gain for quiet signals
  EXPECT_LT(comp.GetCurrentGain(), 1.0f);
}

// Test attack time response
TEST_F(CompressorExpanderTest, AttackTimeResponse) {
  comp.SetCompThreshold(-30.0f);
  comp.SetCompSlope(0.5f);
  comp.SetAttackTime(0.01f); // 10ms attack
  comp.SetReleaseTime(0.1f); // 100ms release
  comp.SetExpThreshold(-100.0f);

  // Start with silence to settle
  for (size_t i = 0; i < 1000; i++) {
    comp.Process(0.0f);
  }

  float initial_gain = comp.GetCurrentGain();

  // Apply loud signal
  float buffer[1000];
  GenerateSine(buffer, 1000, 440.0f, 0.9f);

  for (size_t i = 0; i < 1000; i++) {
    comp.Process(buffer[i]);
  }

  // Gain should have decreased
  EXPECT_LT(comp.GetCurrentGain(), initial_gain);
}

// Test release time response
TEST_F(CompressorExpanderTest, ReleaseTimeResponse) {
  comp.SetCompThreshold(-30.0f);
  comp.SetCompSlope(0.5f);
  comp.SetAttackTime(0.001f); // Fast attack
  comp.SetReleaseTime(0.05f); // 50ms release
  comp.SetExpThreshold(-100.0f);

  // Apply loud signal to compress
  float loud[2000];
  GenerateSine(loud, 2000, 440.0f, 0.9f);
  for (size_t i = 0; i < 2000; i++) {
    comp.Process(loud[i]);
  }

  float compressed_gain = comp.GetCurrentGain();

  // Apply silence - gain should recover
  for (size_t i = 0; i < 5000; i++) {
    comp.Process(0.0f);
  }

  // Gain should have increased (released)
  EXPECT_GT(comp.GetCurrentGain(), compressed_gain);
}

// Test lookahead delay
TEST_F(CompressorExpanderTest, LookaheadDelay) {
  comp.SetLookahead(100);

  // Impulse input
  float out = comp.Process(1.0f);

  // Due to lookahead, immediate output should be from delay buffer (zero)
  EXPECT_NEAR(out, 0.0f, tolerance);

  // After lookahead samples, we should see the impulse
  for (size_t i = 0; i < 99; i++) {
    comp.Process(0.0f);
  }

  // The 100th sample should start showing the impulse effect
  float delayed_out = comp.Process(0.0f);
  EXPECT_NE(delayed_out, 0.0f);
}

// Test dB level reporting
TEST_F(CompressorExpanderTest, LevelReporting) {
  float buffer[1000];
  GenerateSine(buffer, 1000, 440.0f, 0.5f);

  for (size_t i = 0; i < 1000; i++) {
    comp.Process(buffer[i]);
  }

  // Level should be non-zero dB value
  float level_db = comp.GetCurrentLevelDb();
  EXPECT_GT(level_db, -100.0f);
  EXPECT_LT(level_db, 0.0f);
}
