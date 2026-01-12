// Unit Tests for Spectral Filter
// Tests initialization, parameter setting, and basic processing

#include "spectral/spectral_filter.h"
#include <cmath>
#include <gtest/gtest.h>


using namespace daisysp;

class SpectralFilterTest : public ::testing::Test {
protected:
  SpectralFilter<256> filter_;

  void SetUp() override { filter_.Init(48000.0f); }
};

TEST_F(SpectralFilterTest, Initialization) {
  // Should initialize without errors
  EXPECT_EQ(filter_.GetFIRLength(), 256u);
  EXPECT_EQ(filter_.GetFFTSize(), 512u);
}

TEST_F(SpectralFilterTest, ZeroInput) {
  // Zero input should produce zero or near-zero output after settling
  float out = 0.0f;
  for (int i = 0; i < 1000; i++) {
    out = filter_.Process(0.0f);
  }
  EXPECT_NEAR(out, 0.0f, 1e-6f);
}

TEST_F(SpectralFilterTest, OutputRange) {
  // Process various inputs, verify output is finite
  filter_.SetBandpass(1000.0f, 0.002f);

  for (int i = 0; i < 1000; i++) {
    float in = std::sin(2.0f * M_PI * 1000.0f * i / 48000.0f);
    float out = filter_.Process(in);
    EXPECT_TRUE(std::isfinite(out)) << "Failed at sample " << i;
  }
}

TEST_F(SpectralFilterTest, SetBandpass) {
  // Should accept bandpass configuration
  EXPECT_NO_THROW(filter_.SetBandpass(500.0f, 0.001f));
  EXPECT_NO_THROW(filter_.SetBandpass(2000.0f, 0.01f));
}

TEST_F(SpectralFilterTest, SetLowpass) {
  // Should accept lowpass configuration
  EXPECT_NO_THROW(filter_.SetLowpass(1000.0f));
  EXPECT_NO_THROW(filter_.SetLowpass(5000.0f));
}

TEST_F(SpectralFilterTest, SetHighpass) {
  // Should accept highpass configuration
  EXPECT_NO_THROW(filter_.SetHighpass(500.0f));
  EXPECT_NO_THROW(filter_.SetHighpass(2000.0f));
}

TEST_F(SpectralFilterTest, DifferentSampleRates) {
  // Should work at different sample rates
  SpectralFilter<256> filter1, filter2, filter3;
  EXPECT_NO_THROW(filter1.Init(44100.0f));
  EXPECT_NO_THROW(filter2.Init(48000.0f));
  EXPECT_NO_THROW(filter3.Init(96000.0f));
}

TEST_F(SpectralFilterTest, ProcessContinuity) {
  // Processing should produce continuous output
  filter_.SetBandpass(1000.0f, 0.002f);

  float prev_out = 0.0f;
  bool found_non_zero = false;

  for (int i = 0; i < 2000; i++) {
    float in = std::sin(2.0f * M_PI * 1000.0f * i / 48000.0f);
    float out = filter_.Process(in);

    if (std::fabs(out) > 0.001f) {
      found_non_zero = true;
    }

    prev_out = out;
  }

  // After 2000 samples, bandpass should produce some output for in-band signal
  EXPECT_TRUE(found_non_zero) << "Filter produced no output for in-band signal";
}
