// Unit Tests for Cross-Correlation Utility
// Tests autocorrelation, cross-correlation, and YIN helper functions

#include "utility/xcorr.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class CrossCorrelationTest : public ::testing::Test {
protected:
  static constexpr size_t kLength = 256;
  float signal1[kLength];
  float signal2[kLength];
  float output[kLength];
  const float tolerance = 1e-4f;

  void SetUp() override {
    // Clear buffers
    for (size_t i = 0; i < kLength; i++) {
      signal1[i] = 0.0f;
      signal2[i] = 0.0f;
      output[i] = 0.0f;
    }
  }
};

// Test autocorrelation of impulse
TEST_F(CrossCorrelationTest, ImpulseAutocorrelation) {
  // Unit impulse at position 0
  signal1[0] = 1.0f;

  CrossCorrelation::Autocorrelation(signal1, kLength, output, 64);

  // Autocorrelation of impulse is impulse
  EXPECT_NEAR(output[0], 1.0f, tolerance);
  for (size_t i = 1; i < 64; i++) {
    EXPECT_NEAR(output[i], 0.0f, tolerance);
  }
}

// Test autocorrelation of constant signal
TEST_F(CrossCorrelationTest, ConstantAutocorrelation) {
  // Constant signal
  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = 1.0f;
  }

  CrossCorrelation::Autocorrelation(signal1, kLength, output, 64);

  // Autocorrelation decreases linearly (due to reduced overlap)
  EXPECT_NEAR(output[0], static_cast<float>(kLength), tolerance);
}

// Test autocorrelation of periodic signal
TEST_F(CrossCorrelationTest, PeriodicSignalAutocorrelation) {
  // Generate periodic signal with period 32
  const size_t period = 32;
  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = std::sin(2.0f * static_cast<float>(M_PI) *
                          static_cast<float>(i) / static_cast<float>(period));
  }

  CrossCorrelation::Autocorrelation(signal1, kLength, output, 64);

  // Peak at lag 0
  float peak0 = output[0];

  // Second peak should be at lag = period
  float peak_at_period = output[period];

  // Correlation at period should be close to peak (allowing for edge effects)
  EXPECT_GT(peak_at_period, peak0 * 0.8f);
}

// Test cross-correlation with delayed signal
TEST_F(CrossCorrelationTest, DelayedSignalCorrelation) {
  const size_t delay = 16;

  // Generate original signal
  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = std::sin(static_cast<float>(i) * 0.2f);
  }

  // Create delayed copy
  for (size_t i = 0; i < kLength; i++) {
    if (i + delay < kLength) {
      signal2[i] = signal1[i + delay];
    }
  }

  CrossCorrelation::Compute(signal1, signal2, kLength, output, 32);

  // Peak lag should be at the delay value
  size_t peak_lag = CrossCorrelation::FindPeakLag(output, 32);
  EXPECT_EQ(peak_lag, delay);
}

// Test normalized cross-correlation
TEST_F(CrossCorrelationTest, NormalizedCorrelation) {
  // Generate two scaled copies of the same signal
  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = std::sin(static_cast<float>(i) * 0.15f);
    signal2[i] = 5.0f * std::sin(static_cast<float>(i) * 0.15f);
  }

  CrossCorrelation::ComputeNormalized(signal1, signal2, kLength, output, 32);

  // Normalized correlation of identical signals at lag 0 should be ~1
  EXPECT_NEAR(output[0], 1.0f, 0.01f);
}

// Test peak finding with interpolation
TEST_F(CrossCorrelationTest, InterpolatedPeakFinding) {
  // Create a parabola centered at 10.3
  for (size_t i = 0; i < 32; i++) {
    float x = static_cast<float>(i) - 10.3f;
    output[i] = 100.0f - x * x;
  }

  float interpolated = CrossCorrelation::FindPeakLagInterpolated(output, 32);

  // Should find peak near 10.3
  EXPECT_NEAR(interpolated, 10.3f, 0.1f);
}

// Test difference function (for YIN)
TEST_F(CrossCorrelationTest, DifferenceFunction) {
  // Generate periodic signal
  const size_t period = 20;
  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = std::sin(2.0f * static_cast<float>(M_PI) *
                          static_cast<float>(i) / static_cast<float>(period));
  }

  CrossCorrelation::DifferenceFunction(signal1, kLength / 2, output, 64);

  // Difference function at lag 0 should be 0
  EXPECT_NEAR(output[0], 0.0f, tolerance);

  // Difference should have minimum near the period
  float min_val = output[1];
  size_t min_lag = 1;
  for (size_t i = 2; i < 64; i++) {
    if (output[i] < min_val) {
      min_val = output[i];
      min_lag = i;
    }
  }

  // Minimum should be near the period (within ±2 samples)
  EXPECT_NEAR(static_cast<float>(min_lag), static_cast<float>(period), 2.0f);
}

// Test cumulative mean normalized difference
TEST_F(CrossCorrelationTest, CumulativeMeanNormalize) {
  // Create synthetic difference function
  float diff_func[64];
  diff_func[0] = 0.0f;
  for (size_t i = 1; i < 64; i++) {
    diff_func[i] =
        static_cast<float>(i) * static_cast<float>(i); // Parabolic growth
  }

  CrossCorrelation::CumulativeMeanNormalize(diff_func, output, 64);

  // First value should be 1
  EXPECT_NEAR(output[0], 1.0f, tolerance);

  // Values should be normalized
  for (size_t i = 1; i < 64; i++) {
    EXPECT_GT(output[i], 0.0f);
  }
}

// Test edge case: zero length
TEST_F(CrossCorrelationTest, EmptySignal) {
  // Should not crash with zero-length input
  // (function should handle gracefully)
  CrossCorrelation::Compute(signal1, signal2, 0, output, 0);
  // Just verify it doesn't crash - no assertion needed
}

// Test real-world pitch detection scenario
TEST_F(CrossCorrelationTest, PitchDetectionScenario) {
  // Simulate a vocal pitch at ~200 Hz (period ~240 samples at 48kHz)
  // Using scaled-down period for our buffer size
  const float sample_rate = 48000.0f;
  const float f0 = 200.0f;
  const size_t period =
      static_cast<size_t>(sample_rate / f0 / (48000.0f / kLength));

  for (size_t i = 0; i < kLength; i++) {
    signal1[i] = std::sin(2.0f * static_cast<float>(M_PI) *
                          static_cast<float>(i) / static_cast<float>(period));
  }

  CrossCorrelation::Autocorrelation(signal1, kLength, output, 64);

  // Find peak after lag 0
  float max_val = 0.0f;
  size_t max_lag = 1;
  for (size_t i = period / 2; i < period * 2 && i < 64; i++) {
    if (output[i] > max_val) {
      max_val = output[i];
      max_lag = i;
    }
  }

  // Should find period (or close to it)
  EXPECT_NEAR(static_cast<float>(max_lag), static_cast<float>(period), 3.0f);
}
