// Unit Tests for Phase Vocoder
// Tests initialization, pitch ratio setting, and basic processing

#include "spectral/phase_vocoder.h"
#include <cmath>
#include <gtest/gtest.h>


using namespace daisysp;

class PhaseVocoderTest : public ::testing::Test {
protected:
  PhaseVocoder<1024> pv_;

  void SetUp() override { pv_.Init(48000.0f); }
};

TEST_F(PhaseVocoderTest, Initialization) {
  // Should initialize without errors
  EXPECT_EQ(pv_.GetFFTSize(), 1024u);
  EXPECT_EQ(pv_.GetHopSize(), 256u);
  EXPECT_FLOAT_EQ(pv_.GetPitchRatio(), 1.0f);
}

TEST_F(PhaseVocoderTest, SetPitchRatio) {
  // Set and verify pitch ratio
  pv_.SetPitchRatio(1.5f);
  EXPECT_FLOAT_EQ(pv_.GetPitchRatio(), 1.5f);

  pv_.SetPitchRatio(0.75f);
  EXPECT_FLOAT_EQ(pv_.GetPitchRatio(), 0.75f);
}

TEST_F(PhaseVocoderTest, PitchRatioClamping) {
  // Pitch ratio should be clamped to [0.5, 2.0]
  pv_.SetPitchRatio(0.3f);
  EXPECT_FLOAT_EQ(pv_.GetPitchRatio(), 0.5f);

  pv_.SetPitchRatio(3.0f);
  EXPECT_FLOAT_EQ(pv_.GetPitchRatio(), 2.0f);
}

TEST_F(PhaseVocoderTest, ZeroInput) {
  // Zero input should produce zero or near-zero output
  float out = 0.0f;
  for (int i = 0; i < 5000; i++) {
    out = pv_.Process(0.0f);
  }
  EXPECT_NEAR(out, 0.0f, 1e-4f);
}

TEST_F(PhaseVocoderTest, OutputFinite) {
  // All outputs should be finite
  pv_.SetPitchRatio(1.2f);

  for (int i = 0; i < 5000; i++) {
    float in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float out = pv_.Process(in);
    EXPECT_TRUE(std::isfinite(out)) << "Non-finite output at sample " << i;
  }
}

TEST_F(PhaseVocoderTest, UnityRatio) {
  // Unity pitch ratio should pass signal through (with latency)
  pv_.SetPitchRatio(1.0f);

  // Process enough to fill buffers
  for (int i = 0; i < 4000; i++) {
    float in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    pv_.Process(in);
  }

  // Verify output is not zero (signal is passing)
  float max_out = 0.0f;
  for (int i = 4000; i < 6000; i++) {
    float in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float out = pv_.Process(in);
    if (std::fabs(out) > max_out)
      max_out = std::fabs(out);
  }

  EXPECT_GT(max_out, 0.01f) << "No output detected after warmup";
}

TEST_F(PhaseVocoderTest, DifferentSampleRates) {
  // Should work at different sample rates
  PhaseVocoder<1024> pv1, pv2, pv3;
  EXPECT_NO_THROW(pv1.Init(44100.0f));
  EXPECT_NO_THROW(pv2.Init(48000.0f));
  EXPECT_NO_THROW(pv3.Init(96000.0f));
}

TEST_F(PhaseVocoderTest, OutputRange) {
  // Output should be bounded
  pv_.SetPitchRatio(1.5f);

  for (int i = 0; i < 10000; i++) {
    float in = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float out = pv_.Process(in);

    // Output should not explode
    EXPECT_LE(std::fabs(out), 10.0f) << "Output too large at sample " << i;
  }
}
