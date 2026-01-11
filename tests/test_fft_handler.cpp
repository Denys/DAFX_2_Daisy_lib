// Unit Tests for FFT Handler
// Tests forward/inverse FFT correctness with known signals

#include "utility/fft_handler.h"
#include <cmath>
#include <gtest/gtest.h>


using namespace daisysp;

class FFTHandlerTest : public ::testing::Test {
protected:
  static constexpr size_t kFFTSize = 256;
  FFTHandler<kFFTSize> fft;
  float input[kFFTSize];
  float output[kFFTSize];
  float real[kFFTSize];
  float imag[kFFTSize];
  const float tolerance = 1e-4f;

  void SetUp() override {
    fft.Init();

    // Clear buffers
    for (size_t i = 0; i < kFFTSize; i++) {
      input[i] = 0.0f;
      output[i] = 0.0f;
      real[i] = 0.0f;
      imag[i] = 0.0f;
    }
  }
};

// Test initialization
TEST_F(FFTHandlerTest, Initialization) {
  EXPECT_TRUE(fft.IsInitialized());
  EXPECT_EQ(fft.Size(), kFFTSize);
}

// Test DC signal
TEST_F(FFTHandlerTest, DCSignal) {
  // Constant signal should have energy only in bin 0
  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = 1.0f;
  }

  fft.Forward(input, real, imag);

  // DC bin should have value N
  EXPECT_NEAR(real[0], static_cast<float>(kFFTSize), tolerance);
  EXPECT_NEAR(imag[0], 0.0f, tolerance);

  // All other bins should be ~0
  for (size_t i = 1; i < kFFTSize; i++) {
    EXPECT_NEAR(real[i], 0.0f, tolerance);
    EXPECT_NEAR(imag[i], 0.0f, tolerance);
  }
}

// Test single frequency sinusoid
TEST_F(FFTHandlerTest, SingleFrequency) {
  // Generate sine wave at bin 8
  const size_t freq_bin = 8;
  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = std::sin(2.0f * static_cast<float>(M_PI) *
                        static_cast<float>(freq_bin) * static_cast<float>(i) /
                        static_cast<float>(kFFTSize));
  }

  fft.Forward(input, real, imag);

  // Calculate magnitude spectrum
  float magnitude[kFFTSize];
  fft.GetMagnitude(real, imag, magnitude);

  // Peak should be at freq_bin and (N - freq_bin)
  float max_mag = 0.0f;
  size_t max_bin = 0;
  for (size_t i = 0; i < kFFTSize / 2; i++) {
    if (magnitude[i] > max_mag) {
      max_mag = magnitude[i];
      max_bin = i;
    }
  }

  EXPECT_EQ(max_bin, freq_bin);
}

// Test forward-inverse round trip
TEST_F(FFTHandlerTest, RoundTrip) {
  // Generate arbitrary signal
  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = std::sin(static_cast<float>(i) * 0.1f) +
               0.5f * std::cos(static_cast<float>(i) * 0.3f);
  }

  // Forward FFT
  fft.Forward(input, real, imag);

  // Inverse FFT
  fft.Inverse(real, imag, output);

  // Output should match input
  for (size_t i = 0; i < kFFTSize; i++) {
    EXPECT_NEAR(output[i], input[i], tolerance);
  }
}

// Test impulse response
TEST_F(FFTHandlerTest, ImpulseResponse) {
  // Unit impulse at position 0
  input[0] = 1.0f;

  fft.Forward(input, real, imag);

  // FFT of impulse should be flat (all 1s)
  for (size_t i = 0; i < kFFTSize; i++) {
    EXPECT_NEAR(real[i], 1.0f, tolerance);
    EXPECT_NEAR(imag[i], 0.0f, tolerance);
  }
}

// Test delayed impulse
TEST_F(FFTHandlerTest, DelayedImpulse) {
  // Unit impulse at position kFFTSize/4
  const size_t delay = kFFTSize / 4;
  input[delay] = 1.0f;

  fft.Forward(input, real, imag);

  // Magnitude should be flat
  float magnitude[kFFTSize];
  fft.GetMagnitude(real, imag, magnitude);

  for (size_t i = 0; i < kFFTSize; i++) {
    EXPECT_NEAR(magnitude[i], 1.0f, tolerance);
  }
}

// Test magnitude/phase reconstruction
TEST_F(FFTHandlerTest, PolarReconstruction) {
  // Generate arbitrary signal
  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = std::sin(static_cast<float>(i) * 0.2f);
  }

  fft.Forward(input, real, imag);

  // Get magnitude and phase
  float magnitude[kFFTSize];
  float phase[kFFTSize];
  fft.GetMagnitude(real, imag, magnitude);
  fft.GetPhase(real, imag, phase);

  // Reconstruct from polar
  float real2[kFFTSize];
  float imag2[kFFTSize];
  fft.FromPolar(magnitude, phase, real2, imag2);

  // Should match original
  for (size_t i = 0; i < kFFTSize; i++) {
    EXPECT_NEAR(real2[i], real[i], tolerance);
    EXPECT_NEAR(imag2[i], imag[i], tolerance);
  }
}

// Test FFT shift
TEST_F(FFTHandlerTest, FFTShift) {
  // Create ramp
  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = static_cast<float>(i);
  }

  fft.FFTShift(input);

  // First half should now be second half
  EXPECT_FLOAT_EQ(input[0], static_cast<float>(kFFTSize / 2));
  EXPECT_FLOAT_EQ(input[kFFTSize / 2], 0.0f);
}

// Test complex buffer interface
TEST_F(FFTHandlerTest, ComplexInterface) {
  Complex freq[kFFTSize];

  for (size_t i = 0; i < kFFTSize; i++) {
    input[i] = std::sin(static_cast<float>(i) * 0.15f);
  }

  fft.Forward(input, freq);
  fft.Inverse(freq, output);

  for (size_t i = 0; i < kFFTSize; i++) {
    EXPECT_NEAR(output[i], input[i], tolerance);
  }
}

// Test different FFT sizes
TEST(FFTHandlerSizesTest, Size512) {
  FFTHandler<512> fft512;
  fft512.Init();

  EXPECT_EQ(fft512.Size(), 512);
  EXPECT_TRUE(fft512.IsInitialized());
}

TEST(FFTHandlerSizesTest, Size1024) {
  FFTHandler<1024> fft1024;
  fft1024.Init();

  float input[1024];
  float real[1024];
  float imag[1024];
  float output[1024];

  // Simple round-trip test
  for (size_t i = 0; i < 1024; i++) {
    input[i] = std::sin(static_cast<float>(i) * 0.1f);
  }

  fft1024.Forward(input, real, imag);
  fft1024.Inverse(real, imag, output);

  for (size_t i = 0; i < 1024; i++) {
    EXPECT_NEAR(output[i], input[i], 1e-4f);
  }
}

// Test Complex struct operations
TEST(ComplexTest, Arithmetic) {
  Complex a(3.0f, 4.0f);
  Complex b(1.0f, 2.0f);

  Complex sum = a + b;
  EXPECT_FLOAT_EQ(sum.real, 4.0f);
  EXPECT_FLOAT_EQ(sum.imag, 6.0f);

  Complex diff = a - b;
  EXPECT_FLOAT_EQ(diff.real, 2.0f);
  EXPECT_FLOAT_EQ(diff.imag, 2.0f);

  Complex prod = a * b;
  // (3+4i)(1+2i) = 3 + 6i + 4i + 8i² = 3 + 10i - 8 = -5 + 10i
  EXPECT_FLOAT_EQ(prod.real, -5.0f);
  EXPECT_FLOAT_EQ(prod.imag, 10.0f);
}

TEST(ComplexTest, MagnitudePhase) {
  Complex c(3.0f, 4.0f);

  EXPECT_FLOAT_EQ(c.Magnitude(), 5.0f);
  EXPECT_NEAR(c.Phase(), std::atan2(4.0f, 3.0f), 1e-6f);

  // Test polar construction
  Complex polar = Complex::FromPolar(5.0f, std::atan2(4.0f, 3.0f));
  EXPECT_NEAR(polar.real, 3.0f, 1e-5f);
  EXPECT_NEAR(polar.imag, 4.0f, 1e-5f);
}
