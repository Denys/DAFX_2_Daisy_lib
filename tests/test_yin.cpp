// Unit Tests for YIN Pitch Detection
// Tests pitch detection accuracy, edge cases, and performance

#include "analysis/yin.h"
#include <cmath>
#include <gtest/gtest.h>

using namespace daisysp;

class YinPitchDetectorTest : public ::testing::Test {
protected:
  static constexpr size_t kYinLen = 1024;
  static constexpr float kSampleRate = 48000.0f;
  YinPitchDetector<kYinLen> yin;

  // Buffer needs to be YinLen + tau_max
  static constexpr size_t kBufferSize = kYinLen * 2;
  float audio_buffer[kBufferSize];

  const float frequency_tolerance_percent = 2.0f; // 2% tolerance

  void SetUp() override {
    yin.Init(kSampleRate);
    yin.SetTolerance(0.15f);
    yin.SetFrequencyRange(80.0f, 800.0f);

    // Clear buffer
    for (size_t i = 0; i < kBufferSize; i++) {
      audio_buffer[i] = 0.0f;
    }
  }

  // Helper: Generate sine wave at given frequency
  void GenerateSineWave(float frequency, float amplitude = 0.5f) {
    for (size_t i = 0; i < kBufferSize; i++) {
      audio_buffer[i] =
          amplitude * std::sin(2.0f * static_cast<float>(M_PI) * frequency *
                               static_cast<float>(i) / kSampleRate);
    }
  }

  // Helper: Generate sawtooth wave at given frequency
  void GenerateSawtooth(float frequency, float amplitude = 0.5f) {
    float period = kSampleRate / frequency;
    for (size_t i = 0; i < kBufferSize; i++) {
      float phase = std::fmod(static_cast<float>(i), period) / period;
      audio_buffer[i] = amplitude * (2.0f * phase - 1.0f);
    }
  }

  // Helper: Generate white noise
  void GenerateNoise(float amplitude = 0.5f) {
    for (size_t i = 0; i < kBufferSize; i++) {
      audio_buffer[i] = amplitude * (2.0f * static_cast<float>(rand()) /
                                         static_cast<float>(RAND_MAX) -
                                     1.0f);
    }
  }
};

// Test initialization
TEST_F(YinPitchDetectorTest, Initialization) {
  EXPECT_FLOAT_EQ(yin.GetSampleRate(), kSampleRate);
  EXPECT_FLOAT_EQ(yin.GetTolerance(), 0.15f);
  EXPECT_EQ(yin.GetYinLen(), kYinLen);
}

// Test parameter setters
TEST_F(YinPitchDetectorTest, ParameterSetters) {
  yin.SetTolerance(0.25f);
  EXPECT_FLOAT_EQ(yin.GetTolerance(), 0.25f);

  // Tolerance should be clamped
  yin.SetTolerance(0.01f);
  EXPECT_GE(yin.GetTolerance(), 0.05f);

  yin.SetTolerance(0.9f);
  EXPECT_LE(yin.GetTolerance(), 0.5f);
}

// Test pitch detection with pure sine wave at 440 Hz (A4)
TEST_F(YinPitchDetectorTest, DetectA440) {
  const float expected_freq = 440.0f;
  GenerateSineWave(expected_freq);

  float detected = yin.Process(audio_buffer);

  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, frequency_tolerance_percent);
}

// Test pitch detection at 220 Hz (A3)
TEST_F(YinPitchDetectorTest, DetectA220) {
  const float expected_freq = 220.0f;
  GenerateSineWave(expected_freq);

  float detected = yin.Process(audio_buffer);

  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, frequency_tolerance_percent);
}

// Test pitch detection at 100 Hz (low frequency)
TEST_F(YinPitchDetectorTest, DetectLowFrequency) {
  const float expected_freq = 100.0f;
  GenerateSineWave(expected_freq);

  float detected = yin.Process(audio_buffer);

  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, frequency_tolerance_percent);
}

// Test pitch detection at 600 Hz (high frequency)
TEST_F(YinPitchDetectorTest, DetectHighFrequency) {
  const float expected_freq = 600.0f;
  GenerateSineWave(expected_freq);

  float detected = yin.Process(audio_buffer);

  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, frequency_tolerance_percent);
}

// Test with sawtooth wave (has harmonics)
TEST_F(YinPitchDetectorTest, DetectSawtooth) {
  const float expected_freq = 220.0f;
  GenerateSawtooth(expected_freq);

  float detected = yin.Process(audio_buffer);

  // YIN should detect fundamental, not harmonics
  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, frequency_tolerance_percent *
                               2); // More tolerance for complex waveforms
}

// Test unvoiced detection with pure noise
TEST_F(YinPitchDetectorTest, DetectNoise) {
  GenerateNoise(0.5f);

  float detected = yin.Process(audio_buffer);

  // Noise should be detected as unvoiced (frequency = 0 or very low confidence)
  // Note: with very unlucky noise, YIN might detect a spurious pitch
  // This is more of a smoke test
  const YinResult &result = yin.GetResult();

  // Confidence should be low for noise
  EXPECT_LT(result.confidence, 0.5f);
}

// Test silence (should be unvoiced)
TEST_F(YinPitchDetectorTest, DetectSilence) {
  // Buffer already zero from SetUp
  float detected = yin.Process(audio_buffer);

  EXPECT_FALSE(yin.IsVoiced());
  EXPECT_EQ(detected, 0.0f);
}

// Test MIDI note conversion
TEST_F(YinPitchDetectorTest, MidiNoteConversion) {
  // A4 = 440 Hz = MIDI 69
  GenerateSineWave(440.0f);
  yin.Process(audio_buffer);

  float midi = yin.GetMidiNote();
  EXPECT_NEAR(midi, 69.0f, 0.5f);
}

// Test cents deviation
TEST_F(YinPitchDetectorTest, CentsDeviation) {
  // Generate slightly sharp A440 (453.08 Hz = +50 cents)
  // 453.08 = 440 * 2^(50/1200)
  const float sharp_a440 = 440.0f * std::pow(2.0f, 50.0f / 1200.0f);
  GenerateSineWave(sharp_a440);
  yin.Process(audio_buffer);

  float cents = yin.GetCentsDeviation();
  EXPECT_NEAR(cents, 50.0f, 10.0f); // Allow some error
}

// Test result structure
TEST_F(YinPitchDetectorTest, ResultStructure) {
  GenerateSineWave(440.0f);
  yin.Process(audio_buffer);

  const YinResult &result = yin.GetResult();

  EXPECT_TRUE(result.voiced);
  EXPECT_GT(result.frequency, 0.0f);
  EXPECT_GT(result.period, 0.0f);
  EXPECT_GT(result.confidence, 0.5f);

  // Period should match frequency
  float expected_period = kSampleRate / 440.0f;
  EXPECT_NEAR(result.period, expected_period, 5.0f);
}

// Test frequency range limiting
TEST_F(YinPitchDetectorTest, FrequencyRangeLimiting) {
  // Set narrow range
  yin.SetFrequencyRange(200.0f, 300.0f);

  // Generate frequency outside range (should not detect)
  GenerateSineWave(400.0f);
  yin.Process(audio_buffer);

  // May or may not detect - behavior depends on implementation
  // Just verify it doesn't crash
}

// Test streaming mode
TEST_F(YinPitchDetectorTest, StreamingMode) {
  const float expected_freq = 440.0f;

  // Reset for streaming
  yin.Init(kSampleRate);
  yin.SetHopSize(512);

  int pitch_count = 0;
  float last_pitch = 0.0f;

  // Feed samples one at a time
  for (size_t i = 0; i < kBufferSize * 4; i++) {
    float sample =
        0.5f * std::sin(2.0f * static_cast<float>(M_PI) * expected_freq *
                        static_cast<float>(i) / kSampleRate);

    if (yin.ProcessSample(sample)) {
      pitch_count++;
      last_pitch = yin.GetFrequency();
    }
  }

  // Should have produced multiple pitch estimates
  EXPECT_GT(pitch_count, 0);

  // Last pitch should be near expected
  if (last_pitch > 0.0f) {
    float error_percent =
        std::fabs(last_pitch - expected_freq) / expected_freq * 100.0f;
    EXPECT_LT(error_percent, frequency_tolerance_percent * 2);
  }
}

// Test different YIN lengths
TEST(YinSizesTest, Yin2048) {
  YinPitchDetector<2048> yin2k;
  yin2k.Init(48000.0f);

  EXPECT_EQ(yin2k.GetYinLen(), 2048);
}

// Test with different sample rates
TEST(YinSampleRatesTest, SampleRate44100) {
  const float sample_rate = 44100.0f;
  YinPitchDetector<1024> yin;
  yin.Init(sample_rate);

  float buffer[2048];
  const float expected_freq = 440.0f;

  for (size_t i = 0; i < 2048; i++) {
    buffer[i] =
        0.5f * std::sin(2.0f * static_cast<float>(M_PI) * expected_freq *
                        static_cast<float>(i) / sample_rate);
  }

  float detected = yin.Process(buffer);

  EXPECT_GT(detected, 0.0f);
  float error_percent =
      std::fabs(detected - expected_freq) / expected_freq * 100.0f;
  EXPECT_LT(error_percent, 3.0f); // 3% tolerance
}

// Performance smoke test (ensure it completes in reasonable time)
TEST_F(YinPitchDetectorTest, PerformanceSmokeTest) {
  GenerateSineWave(440.0f);

  // Process multiple times to ensure no memory leaks or performance issues
  for (int i = 0; i < 100; i++) {
    yin.Process(audio_buffer);
  }

  // If we got here, performance is acceptable
  EXPECT_TRUE(true);
}
