// Unit Tests for Circular Buffer Utility
// Tests initialization, read/write, interpolation, and wrap-around

#include "utility/circularbuffer.h"
#include <gtest/gtest.h>


using namespace daisysp;

class CircularBufferTest : public ::testing::Test {
protected:
  CircularBuffer<float, 1024> buffer;

  void SetUp() override { buffer.Init(); }
};

// Test initialization
TEST_F(CircularBufferTest, Initialization) {
  EXPECT_EQ(buffer.GetSize(), 1024);
  EXPECT_EQ(buffer.GetWritePtr(), 0);

  // Buffer should be cleared to zero
  for (size_t i = 0; i < 100; i++) {
    EXPECT_FLOAT_EQ(buffer.Read(i), 0.0f);
  }
}

// Test write and read
TEST_F(CircularBufferTest, WriteRead) {
  buffer.Write(0.5f);
  buffer.Write(0.75f);
  buffer.Write(1.0f);

  // Most recent sample is at delay 1, not 0
  EXPECT_FLOAT_EQ(buffer.Read(1), 1.0f);
  EXPECT_FLOAT_EQ(buffer.Read(2), 0.75f);
  EXPECT_FLOAT_EQ(buffer.Read(3), 0.5f);
}

// Test wrap-around
TEST_F(CircularBufferTest, WrapAround) {
  // Fill buffer completely
  for (size_t i = 0; i < 1024; i++) {
    buffer.Write(static_cast<float>(i));
  }

  // Write more to wrap around
  buffer.Write(1024.0f);
  buffer.Write(1025.0f);

  // Check that wrap-around works
  EXPECT_FLOAT_EQ(buffer.Read(1), 1025.0f);
  EXPECT_FLOAT_EQ(buffer.Read(2), 1024.0f);
}

// Test linear interpolation
TEST_F(CircularBufferTest, LinearInterpolation) {
  buffer.Write(0.0f);
  buffer.Write(1.0f);

  // Midpoint should be interpolated
  float interpolated = buffer.ReadInterpolated(1.5f);
  EXPECT_NEAR(interpolated, 0.5f, 0.01f);

  // Quarter points
  EXPECT_NEAR(buffer.ReadInterpolated(1.25f), 0.75f, 0.01f);
  EXPECT_NEAR(buffer.ReadInterpolated(1.75f), 0.25f, 0.01f);
}

// Test cubic interpolation
TEST_F(CircularBufferTest, CubicInterpolation) {
  // Write a smooth curve
  for (size_t i = 0; i < 100; i++) {
    buffer.Write(std::sin(static_cast<float>(i) * 0.1f));
  }

  // Cubic interpolation should give a smooth result
  float cubic = buffer.ReadCubic(50.5f);
  EXPECT_TRUE(std::isfinite(cubic));
  EXPECT_GT(cubic, -1.1f);
  EXPECT_LT(cubic, 1.1f);
}

// Test tap access
TEST_F(CircularBufferTest, TapAccess) {
  buffer.Write(1.0f);
  buffer.Write(2.0f);
  buffer.Write(3.0f);

  EXPECT_FLOAT_EQ(buffer.Tap(1), 3.0f);
  EXPECT_FLOAT_EQ(buffer.Tap(2), 2.0f);
  EXPECT_FLOAT_EQ(buffer.Tap(3), 1.0f);
}

// Test custom size initialization
TEST_F(CircularBufferTest, CustomSize) {
  CircularBuffer<float, 2048> large_buffer;
  large_buffer.Init(512); // Use only 512 samples

  EXPECT_EQ(large_buffer.GetSize(), 512);
}

// Test clear
TEST_F(CircularBufferTest, Clear) {
  buffer.Write(1.0f);
  buffer.Write(2.0f);
  buffer.Write(3.0f);

  buffer.Clear();

  EXPECT_FLOAT_EQ(buffer.Read(1), 0.0f);
  EXPECT_FLOAT_EQ(buffer.Read(2), 0.0f);
}

// Test delay bounds checking
TEST_F(CircularBufferTest, DelayBounds) {
  buffer.Write(1.0f);

  // Reading beyond buffer size should clamp
  float out = buffer.Read(10000); // Way beyond size
  EXPECT_TRUE(std::isfinite(out));
}

// Test dynamic circular buffer
TEST(DynamicCircularBufferTest, BasicOperations) {
  DynamicCircularBuffer<float> dyn_buffer;
  dyn_buffer.Init(256);

  EXPECT_EQ(dyn_buffer.GetSize(), 256);

  dyn_buffer.Write(0.5f);
  dyn_buffer.Write(1.0f);

  EXPECT_FLOAT_EQ(dyn_buffer.Read(1), 1.0f);
  EXPECT_FLOAT_EQ(dyn_buffer.Read(2), 0.5f);

  // Test interpolation
  EXPECT_NEAR(dyn_buffer.ReadInterpolated(1.5f), 0.75f, 0.01f);
}
