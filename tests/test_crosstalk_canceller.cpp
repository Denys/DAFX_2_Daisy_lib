// Unit Tests for Crosstalk Canceller
// Tests initialization, parameter setting, and stereo processing

#include "spatial/crosstalk_canceller.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class CrosstalkCancellerTest : public ::testing::Test {
protected:
  CrosstalkCanceller<128> ctc_;

  void SetUp() override { ctc_.Init(48000.0f); }
};

TEST_F(CrosstalkCancellerTest, Initialization) {
  // Should initialize without errors
  EXPECT_FLOAT_EQ(ctc_.GetSpeakerAngle(), 10.0f);
}

TEST_F(CrosstalkCancellerTest, SetSpeakerAngle) {
  // Should accept speaker angle changes
  ctc_.SetSpeakerAngle(15.0f);
  EXPECT_FLOAT_EQ(ctc_.GetSpeakerAngle(), 15.0f);

  ctc_.SetSpeakerAngle(5.0f);
  EXPECT_FLOAT_EQ(ctc_.GetSpeakerAngle(), 5.0f);
}

TEST_F(CrosstalkCancellerTest, SetRegularization) {
  // Should accept regularization changes
  EXPECT_NO_THROW(ctc_.SetRegularization(1e-4f));
  EXPECT_NO_THROW(ctc_.SetRegularization(1e-3f));
}

TEST_F(CrosstalkCancellerTest, ZeroInput) {
  // Zero input should produce zero or near-zero output
  float left_out, right_out;
  for (int i = 0; i < 1000; i++) {
    ctc_.Process(0.0f, 0.0f, &left_out, &right_out);
  }
  EXPECT_NEAR(left_out, 0.0f, 1e-4f);
  EXPECT_NEAR(right_out, 0.0f, 1e-4f);
}

TEST_F(CrosstalkCancellerTest, StereoOutput) {
  // Stereo input should produce stereo output
  float left_out, right_out;

  // Process some stereo signal
  for (int i = 0; i < 500; i++) {
    float left_in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float right_in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f + 0.5f);
    ctc_.Process(left_in, right_in, &left_out, &right_out);
  }

  // Track max output after warmup
  float max_left = 0.0f, max_right = 0.0f;
  for (int i = 500; i < 1000; i++) {
    float left_in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float right_in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f + 0.5f);
    ctc_.Process(left_in, right_in, &left_out, &right_out);

    if (std::fabs(left_out) > max_left)
      max_left = std::fabs(left_out);
    if (std::fabs(right_out) > max_right)
      max_right = std::fabs(right_out);
  }

  EXPECT_GT(max_left, 0.001f) << "No left output";
  EXPECT_GT(max_right, 0.001f) << "No right output";
}

TEST_F(CrosstalkCancellerTest, OutputFinite) {
  // All outputs should be finite
  float left_out, right_out;

  for (int i = 0; i < 2000; i++) {
    float left_in = std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float right_in = std::sin(2.0f * M_PI * 550.0f * i / 48000.0f);
    ctc_.Process(left_in, right_in, &left_out, &right_out);

    EXPECT_TRUE(std::isfinite(left_out)) << "Non-finite left at " << i;
    EXPECT_TRUE(std::isfinite(right_out)) << "Non-finite right at " << i;
  }
}

TEST_F(CrosstalkCancellerTest, DifferentSampleRates) {
  // Should work at different sample rates
  CrosstalkCanceller<128> ctc1, ctc2, ctc3;
  EXPECT_NO_THROW(ctc1.Init(44100.0f));
  EXPECT_NO_THROW(ctc2.Init(48000.0f));
  EXPECT_NO_THROW(ctc3.Init(96000.0f));
}

TEST_F(CrosstalkCancellerTest, OutputRange) {
  // Output should be bounded
  float left_out, right_out;

  for (int i = 0; i < 5000; i++) {
    float left_in = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / 48000.0f);
    float right_in = 0.5f * std::sin(2.0f * M_PI * 550.0f * i / 48000.0f);
    ctc_.Process(left_in, right_in, &left_out, &right_out);

    // Output should not explode (crosstalk cancellers can have gain >1)
    EXPECT_LE(std::fabs(left_out), 20.0f) << "Left too large at " << i;
    EXPECT_LE(std::fabs(right_out), 20.0f) << "Right too large at " << i;
  }
}
