// Unit Tests for Whisperization Effect
// Tests initialization, parameter setting, and basic processing

#include <cmath>
#include <gtest/gtest.h>

#include "spectral/whisperization.h"

using namespace daisysp;

class WhisperizationTest : public ::testing::Test {
protected:
  Whisperization<512> effect_;

  void SetUp() override { effect_.Init(48000.0f); }
};

// Test default initialization
TEST_F(WhisperizationTest, Initialization) {
  EXPECT_TRUE(effect_.IsInitialized());
  EXPECT_EQ(effect_.GetFFTSize(), 512u);
  EXPECT_EQ(effect_.GetHopSize(), 64u); // Default N/8
  EXPECT_FLOAT_EQ(effect_.GetMix(), 1.0f);
}

// Test hop size parameter
TEST_F(WhisperizationTest, HopSizeParameter) {
  effect_.SetHopSize(32);
  EXPECT_EQ(effect_.GetHopSize(), 32u);

  effect_.SetHopSize(128);
  EXPECT_EQ(effect_.GetHopSize(), 128u);

  // Invalid hop size should be ignored
  effect_.SetHopSize(0);
  EXPECT_EQ(effect_.GetHopSize(), 128u);

  effect_.SetHopSize(1024); // Greater than N
  EXPECT_EQ(effect_.GetHopSize(), 128u);
}

// Test mix parameter
TEST_F(WhisperizationTest, MixParameter) {
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
TEST_F(WhisperizationTest, ZeroInput) {
  const size_t block_size = 256;
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
TEST_F(WhisperizationTest, OutputFinite) {
  const size_t block_size = 512;
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
TEST_F(WhisperizationTest, DryMix) {
  effect_.SetMix(0.0f); // Fully dry

  const size_t block_size = 128;
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

// Test random phase generates variation
TEST_F(WhisperizationTest, RandomPhaseVariation) {
  effect_.SetMix(1.0f); // Fully wet

  const size_t block_size = 512;
  float input[block_size];
  float output1[block_size];
  float output2[block_size];

  // Generate identical input for both passes
  for (size_t i = 0; i < block_size; ++i) {
    input[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
  }

  // Process first block
  Whisperization<512> effect1;
  effect1.Init(48000.0f);
  effect1.SetSeed(12345);

  // Process multiple blocks to get past initial latency
  for (int i = 0; i < 5; ++i) {
    effect1.Process(input, output1, block_size);
  }

  // Process with different seed
  Whisperization<512> effect2;
  effect2.Init(48000.0f);
  effect2.SetSeed(54321); // Different seed

  for (int i = 0; i < 5; ++i) {
    effect2.Process(input, output2, block_size);
  }

  // Outputs should differ due to random phase
  bool differs = false;
  for (size_t i = 0; i < block_size; ++i) {
    if (std::fabs(output1[i] - output2[i]) > 1e-6f) {
      differs = true;
      break;
    }
  }
  EXPECT_TRUE(differs) << "Different seeds should produce different outputs";
}

// Test with different sample rates
TEST_F(WhisperizationTest, DifferentSampleRates) {
  Whisperization<512> effect44;
  Whisperization<512> effect96;

  EXPECT_NO_THROW(effect44.Init(44100.0f));
  EXPECT_NO_THROW(effect96.Init(96000.0f));

  EXPECT_TRUE(effect44.IsInitialized());
  EXPECT_TRUE(effect96.IsInitialized());
}

// Test single sample processing
TEST_F(WhisperizationTest, SingleSampleProcess) {
  for (int i = 0; i < 1000; ++i) {
    float in = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
    float out = effect_.Process(in);
    EXPECT_TRUE(std::isfinite(out));
  }
}

// Test reproducibility with same seed
TEST_F(WhisperizationTest, SeedReproducibility) {
  const size_t block_size = 256;
  float input[block_size];
  float output1[block_size];
  float output2[block_size];

  // Generate input
  for (size_t i = 0; i < block_size; ++i) {
    input[i] = 0.5f * std::sin(2.0f * 3.14159f * 440.0f * i / 48000.0f);
  }

  // First run with seed
  Whisperization<512> effect1;
  effect1.Init(48000.0f);
  effect1.SetSeed(99999);
  for (int i = 0; i < 3; ++i) {
    effect1.Process(input, output1, block_size);
  }

  // Second run with same seed
  Whisperization<512> effect2;
  effect2.Init(48000.0f);
  effect2.SetSeed(99999);
  for (int i = 0; i < 3; ++i) {
    effect2.Process(input, output2, block_size);
  }

  // Outputs should be identical
  for (size_t i = 0; i < block_size; ++i) {
    EXPECT_FLOAT_EQ(output1[i], output2[i])
        << "Same seed should produce identical results at sample " << i;
  }
}
