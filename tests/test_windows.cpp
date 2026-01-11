// Unit Tests for Window Functions
// Tests generation of common window types

#include "utility/windows.h"
#include <cmath>
#include <gtest/gtest.h>


using namespace daisysp;

class WindowsTest : public ::testing::Test {
protected:
  static constexpr size_t kWindowSize = 256;
  float window[kWindowSize];
  const float tolerance = 1e-5f;
};

// Test Hanning window properties
TEST_F(WindowsTest, Hanning) {
  Windows::Hanning(window, kWindowSize);

  // First sample should be ~0 (periodic window)
  EXPECT_NEAR(window[0], 0.0f, 0.01f);

  // Middle sample should be 1.0
  EXPECT_NEAR(window[kWindowSize / 2], 1.0f, 0.01f);

  // Window should be symmetric (periodic, so not exactly)
  for (size_t i = 1; i < kWindowSize / 2; i++) {
    EXPECT_NEAR(window[i], window[kWindowSize - i], 0.01f);
  }

  // All values should be in [0, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], 0.0f);
    EXPECT_LE(window[i], 1.0f);
  }
}

// Test Hamming window properties
TEST_F(WindowsTest, Hamming) {
  Windows::Hamming(window, kWindowSize);

  // First and last samples should be ~0.08 (not zero like Hanning)
  EXPECT_NEAR(window[0], 0.08f, 0.01f);

  // Middle sample should be 1.0
  EXPECT_NEAR(window[kWindowSize / 2], 1.0f, 0.02f);

  // All values should be in [0.08, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], 0.07f);
    EXPECT_LE(window[i], 1.01f);
  }
}

// Test Blackman-Harris window properties
TEST_F(WindowsTest, BlackmanHarris) {
  Windows::BlackmanHarris(window, kWindowSize);

  // First sample should be very small
  EXPECT_LT(window[0], 0.001f);

  // Middle sample should be ~1.0
  EXPECT_NEAR(window[kWindowSize / 2], 1.0f, 0.02f);

  // All values should be in [0, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], -0.01f); // Allow tiny negative from precision
    EXPECT_LE(window[i], 1.01f);
  }
}

// Test Blackman window properties
TEST_F(WindowsTest, Blackman) {
  Windows::Blackman(window, kWindowSize);

  // First sample should be very small
  EXPECT_LT(window[0], 0.01f);

  // Middle sample should be ~1.0
  EXPECT_NEAR(window[kWindowSize / 2], 1.0f, 0.02f);

  // All values should be in [0, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], -0.01f);
    EXPECT_LE(window[i], 1.01f);
  }
}

// Test Triangular window properties
TEST_F(WindowsTest, Triangular) {
  Windows::Triangular(window, kWindowSize);

  // First sample should be 0
  EXPECT_NEAR(window[0], 0.0f, 0.01f);

  // Middle sample should be 1.0
  size_t mid = (kWindowSize - 1) / 2;
  EXPECT_NEAR(window[mid], 1.0f, 0.02f);

  // Last sample should be ~0
  EXPECT_NEAR(window[kWindowSize - 1], 0.0f, 0.01f);

  // All values should be in [0, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], -0.01f);
    EXPECT_LE(window[i], 1.01f);
  }
}

// Test Rectangular window
TEST_F(WindowsTest, Rectangular) {
  Windows::Rectangular(window, kWindowSize);

  // All values should be 1.0
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_FLOAT_EQ(window[i], 1.0f);
  }
}

// Test Kaiser window
TEST_F(WindowsTest, Kaiser) {
  Windows::Kaiser(window, kWindowSize, 8.0f);

  // First sample should be small
  EXPECT_LT(window[0], 0.01f);

  // Middle sample should be 1.0
  EXPECT_NEAR(window[kWindowSize / 2], 1.0f, 0.02f);

  // All values should be in [0, 1]
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_GE(window[i], -0.01f);
    EXPECT_LE(window[i], 1.01f);
  }
}

// Test window application
TEST_F(WindowsTest, Apply) {
  float signal[kWindowSize];
  for (size_t i = 0; i < kWindowSize; i++) {
    signal[i] = 1.0f;
  }

  Windows::Hanning(window, kWindowSize);
  Windows::Apply(signal, window, kWindowSize);

  // Signal should now equal window
  for (size_t i = 0; i < kWindowSize; i++) {
    EXPECT_FLOAT_EQ(signal[i], window[i]);
  }
}

// Test window sum
TEST_F(WindowsTest, Sum) {
  Windows::Rectangular(window, kWindowSize);
  float sum = Windows::Sum(window, kWindowSize);
  EXPECT_FLOAT_EQ(sum, static_cast<float>(kWindowSize));

  Windows::Hanning(window, kWindowSize);
  sum = Windows::Sum(window, kWindowSize);
  // Hanning sum should be about half the window size
  EXPECT_NEAR(sum, static_cast<float>(kWindowSize) / 2.0f, 1.0f);
}

// Test sum of squares
TEST_F(WindowsTest, SumSquared) {
  Windows::Rectangular(window, kWindowSize);
  float sum_sq = Windows::SumSquared(window, kWindowSize);
  EXPECT_FLOAT_EQ(sum_sq, static_cast<float>(kWindowSize));
}

// Test different sizes
TEST_F(WindowsTest, DifferentSizes) {
  float small_window[64];
  float large_window[1024];

  Windows::Hanning(small_window, 64);
  Windows::Hanning(large_window, 1024);

  // Both should be valid
  EXPECT_NEAR(small_window[32], 1.0f, 0.01f);
  EXPECT_NEAR(large_window[512], 1.0f, 0.01f);
}
