// Unit Tests for SOLA Time Stretch
// Tests time stretching behavior

#include "effects/sola_time_stretch.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class SOLATimeStretchTest : public ::testing::Test {
protected:
  static constexpr float kSampleRate = 48000.0f;
  SOLATimeStretch<4096, 2048> sola;
  const float tolerance = 1e-4f;

  void SetUp() override { sola.Init(kSampleRate); }

  // Generate sine wave
  void GenerateSine(float *buffer, size_t length, float freq) {
    for (size_t i = 0; i < length; i++) {
      buffer[i] = std::sin(2.0f * 3.14159265f * freq * static_cast<float>(i) /
                           kSampleRate);
    }
  }
};

// Test initialization
TEST_F(SOLATimeStretchTest, Initialization) {
  EXPECT_FLOAT_EQ(sola.GetSampleRate(), kSampleRate);
  EXPECT_FLOAT_EQ(sola.GetTimeStretch(), 1.0f);
  EXPECT_EQ(sola.GetGrainSize(), 2048);
}

// Test parameter setters
TEST_F(SOLATimeStretchTest, ParameterSetters) {
  sola.SetTimeStretch(0.75f);
  EXPECT_FLOAT_EQ(sola.GetTimeStretch(), 0.75f);

  sola.SetGrainSize(1024);
  EXPECT_EQ(sola.GetGrainSize(), 1024);

  sola.SetAnalysisHop(128);
  EXPECT_EQ(sola.GetAnalysisHop(), 128);
}

// Test time stretch clamping
TEST_F(SOLATimeStretchTest, TimeStretchClamping) {
  sola.SetTimeStretch(0.1f);
  EXPECT_GE(sola.GetTimeStretch(), 0.25f);

  sola.SetTimeStretch(5.0f);
  EXPECT_LE(sola.GetTimeStretch(), 2.0f);
}

// Test block processing produces output
TEST_F(SOLATimeStretchTest, BlockProcessingProducesOutput) {
  const size_t input_len = 8192;
  const size_t max_output = 16384;

  float input[input_len];
  float output[max_output];

  // Generate test signal
  GenerateSine(input, input_len, 440.0f);

  // Time stretch 1.0 (no change)
  sola.SetTimeStretch(1.0f);
  size_t out_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Should produce output
  EXPECT_GT(out_len, 0);
}

// Test slow down produces more samples
TEST_F(SOLATimeStretchTest, SlowDownProducesMore) {
  const size_t input_len = 8192;
  const size_t max_output = 32768;

  float input[input_len];
  float output[max_output];

  GenerateSine(input, input_len, 440.0f);

  // Unity stretch
  sola.Init(kSampleRate);
  sola.SetTimeStretch(1.0f);
  size_t unity_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Slow down (0.5 = half speed, should produce ~2x samples)
  sola.Init(kSampleRate);
  sola.SetTimeStretch(0.5f);
  size_t slow_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Slower playback should produce more output (roughly 2x)
  EXPECT_GT(slow_len, unity_len);
}

// Test speed up produces fewer samples
TEST_F(SOLATimeStretchTest, SpeedUpProducesLess) {
  const size_t input_len = 16384;
  const size_t max_output = 32768;

  float input[input_len];
  float output[max_output];

  GenerateSine(input, input_len, 440.0f);

  // Unity stretch
  sola.Init(kSampleRate);
  sola.SetTimeStretch(1.0f);
  size_t unity_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Speed up (2.0 = double speed, should produce ~0.5x samples)
  sola.Init(kSampleRate);
  sola.SetTimeStretch(2.0f);
  size_t fast_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Faster playback should produce less output
  EXPECT_LT(fast_len, unity_len);
}

// Test output is not just zeros
TEST_F(SOLATimeStretchTest, OutputNotZeros) {
  const size_t input_len = 8192;
  const size_t max_output = 16384;

  float input[input_len];
  float output[max_output];

  GenerateSine(input, input_len, 440.0f);

  sola.SetTimeStretch(0.75f);
  size_t out_len = sola.ProcessBlock(input, input_len, output, max_output);

  // Check output has energy
  float energy = 0.0f;
  for (size_t i = 0; i < out_len; i++) {
    energy += output[i] * output[i];
  }

  EXPECT_GT(energy, 0.1f);
}

// Test streaming mode
TEST_F(SOLATimeStretchTest, StreamingMode) {
  sola.SetTimeStretch(1.0f);

  int output_count = 0;

  // Feed samples until we get output
  for (int i = 0; i < 5000; i++) {
    float sample = std::sin(2.0f * 3.14159265f * 440.0f *
                            static_cast<float>(i) / kSampleRate);

    if (sola.FeedInput(sample)) {
      while (sola.OutputAvailable()) {
        float out = sola.GetOutput();
        (void)out;
        output_count++;
      }
    }
  }

  // Should have produced some output
  EXPECT_GT(output_count, 0);
}
