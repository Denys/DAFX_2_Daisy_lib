// Unit Tests for Robotization Effect
// Tests initialization, parameter setting, and basic processing

#include <cmath>
#include <gtest/gtest.h>

#include "spectral/robotization.h"

using namespace daisysp;

class RobotizationTest : public ::testing::Test {
protected:
  Robotization<1024> effect_;

  void SetUp() override { effect_.Init(48000.0f); }
};

// Test default initialization
TEST_F(RobotizationTest, Initialization) {
  EXPECT_TRUE(effect_.IsInitialized());
  EXPECT_EQ(effect_.GetFFTSize(), 1024u);
  EXPECT_EQ(effect_.GetHopSize(), 256u); // Default N/4
  EXPECT_FLOAT_EQ(effect_.GetMix(), 1.0f);
}

// Test hop size parameter
TEST_F(RobotizationTest, HopSizeParameter) {
  effect_.SetHopSize(128);
  EXPECT_EQ(effect_.GetHopSize(), 128u);

  effect_.SetHopSize(512);
  EXPECT_EQ(effect_.GetHopSize(), 512u);

  // Invalid hop size should be ignored
  effect_.SetHopSize(0);
  EXPECT_EQ(effect_.GetHopSize(), 512u);

  effect_.SetHopSize(2048); // Greater than N
  EXPECT_EQ(effect_.GetHopSize(), 512u);
}

// Test mix parameter
TEST_F(RobotizationTest, MixParameter) {
  effect_.SetMix(0.0f);
  EXPECT_FLOAT_EQ(effect_.GetMix(), 0.0f);

  effect_.SetMix(0.5f);
  EXPECT_FLOAT_EQ(effect_.GetMix(), 0.5f);

  effect_.SetMix(1.0f);
  EXPECT_FLOAT_EQ(effect_.GetMix(), 1.0f);

  // Clamping
  effect_.SetMix(-0.5f);
  EXPECT_FLOAT_EQ(effect_.GetMix(), 0.0f);

  effect_.SetMix(1.5f);
  EXPECT_FLOAT_EQ(effect_.GetMix(), 1.0f);
}

// Test zero input produces near-zero output
TEST_F(RobotizationTest, ZeroInput) {
  const size_t block_size = 512;
  float input[block_size] = {0};
  float output[block_size] = {0};

  // Process several blocks to fill overlap buffer
  for (int block = 0; block < 10; ++block) {
    effect_.Process(input, output, block_size);
  }

  // Output should be near zero
  for (size_t i = 0; i < block_size; ++i) {
    EXPECT_NEAR(output[i], 0.0f, 1e-6f);
  }
}

// Test output is finite for valid input
TEST_F(RobotizationTest, OutputFinite) {
  const size_t block_size = 1024;
  float input[block_size];
  float output[block_size];

  // Generate sine wave input
  for (size_t i = 0; i < block_size; ++i) {
    input[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
  }

  // Process several blocks
  for (int block = 0; block < 5; ++block) {
    effect_.Process(input, output, block_size);

    for (size_t i = 0; i < block_size; ++i) {
      EXPECT_TRUE(std::isfinite(output[i]))
          << "Non-finite output at block " << block << ", sample " << i;
    }
  }
}

// Test dry mix passes input through
TEST_F(RobotizationTest, DryMix) {
  effect_.SetMix(0.0f); // Fully dry

  const size_t block_size = 256;
  float input[block_size];
  float output[block_size];

  // Generate test signal
  for (size_t i = 0; i < block_size; ++i) {
    input[i] = static_cast<float>(i) / block_size;
  }

  effect_.Process(input, output, block_size);

  // With 0% mix, output should equal input
  for (size_t i = 0; i < block_size; ++i) {
    EXPECT_NEAR(output[i], input[i], 1e-6f);
  }
}

// Test with different sample rates
TEST_F(RobotizationTest, DifferentSampleRates) {
  Robotization<1024> effect44;
  Robotization<1024> effect96;

  EXPECT_NO_THROW(effect44.Init(44100.0f));
  EXPECT_NO_THROW(effect96.Init(96000.0f));

  EXPECT_TRUE(effect44.IsInitialized());
  EXPECT_TRUE(effect96.IsInitialized());
}

// Test single sample processing
TEST_F(RobotizationTest, SingleSampleProcess) {
  for (int i = 0; i < 1000; ++i) {
    float in = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
    float out = effect_.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}
