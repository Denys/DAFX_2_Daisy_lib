// Unit Tests for Low Shelving Filter
// Tests initialization, parameter setting, and basic processing

#include <gtest/gtest.h>
#include "filters/lowshelving.h"

using namespace daisysp;

class LowShelvingTest : public ::testing::Test
{
protected:
    LowShelving filter;

    void SetUp() override
    {
        filter.Init(48000.0f);
    }
};

// Test initialization
TEST_F(LowShelvingTest, Initialization)
{
    // Verify default parameters
    EXPECT_GT(filter.GetFrequency(), 0.0f);
    EXPECT_FLOAT_EQ(filter.GetGain(), 0.0f); // 0 dB default
}

// Test parameter setting
TEST_F(LowShelvingTest, ParameterSetting)
{
    filter.SetFrequency(200.0f);
    EXPECT_FLOAT_EQ(filter.GetFrequency(), 200.0f);

    filter.SetGain(6.0f);
    EXPECT_FLOAT_EQ(filter.GetGain(), 6.0f);

    filter.SetGain(-6.0f);
    EXPECT_FLOAT_EQ(filter.GetGain(), -6.0f);
}

// Test zero input
TEST_F(LowShelvingTest, ZeroInput)
{
    float in = 0.0f;
    float out = filter.Process(in);
    EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test unity gain (0 dB)
TEST_F(LowShelvingTest, UnityGain)
{
    filter.SetGain(0.0f);
    float in = 0.5f;
    float out = filter.Process(in);
    // First sample may differ due to state, process a few more
    for (int i = 0; i < 100; i++) {
        out = filter.Process(in);
    }
    EXPECT_NEAR(out, in, 0.05f);
}

// Test output is finite
TEST_F(LowShelvingTest, OutputRange)
{
    filter.SetGain(12.0f); // +12 dB boost
    for (int i = -10; i <= 10; i++)
    {
        float in = static_cast<float>(i) * 0.1f;
        float out = filter.Process(in);
        EXPECT_TRUE(std::isfinite(out));
    }
}

// Test different sample rates
TEST_F(LowShelvingTest, DifferentSampleRates)
{
    EXPECT_NO_THROW(filter.Init(44100.0f));
    EXPECT_NO_THROW(filter.Init(48000.0f));
    EXPECT_NO_THROW(filter.Init(96000.0f));
}

// Test frequency range
TEST_F(LowShelvingTest, FrequencyRange)
{
    EXPECT_NO_THROW(filter.SetFrequency(20.0f));
    EXPECT_NO_THROW(filter.SetFrequency(1000.0f));
    EXPECT_NO_THROW(filter.SetFrequency(5000.0f));
}

// Test gain range
TEST_F(LowShelvingTest, GainRange)
{
    EXPECT_NO_THROW(filter.SetGain(-20.0f));
    EXPECT_NO_THROW(filter.SetGain(0.0f));
    EXPECT_NO_THROW(filter.SetGain(20.0f));
}
