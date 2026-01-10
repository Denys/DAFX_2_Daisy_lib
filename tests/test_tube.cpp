// Unit Tests for Tube Distortion Effect
// Tests initialization, parameter setting, and basic processing

#include <gtest/gtest.h>
#include "effects/tube.h"

using namespace daisysp;

class TubeTest : public ::testing::Test
{
protected:
    Tube tube;

    void SetUp() override
    {
        // Initialize with standard sample rate
        tube.Init(48000.0f);
    }
};

// Test initialization
TEST_F(TubeTest, Initialization)
{
    // Verify default parameters are set
    // These are the default values from Tube::Init()
    EXPECT_FLOAT_EQ(tube.GetDrive(), 1.0f);
    EXPECT_FLOAT_EQ(tube.GetBias(), 0.0f);
    EXPECT_FLOAT_EQ(tube.GetDistortion(), 1.0f);
    EXPECT_FLOAT_EQ(tube.GetHighPassPole(), 0.99f);
    EXPECT_FLOAT_EQ(tube.GetLowPassPole(), 0.5f);
    EXPECT_FLOAT_EQ(tube.GetMix(), 1.0f);
}

// Test parameter setting
TEST_F(TubeTest, ParameterSetting)
{
    // Test drive parameter
    tube.SetDrive(2.0f);
    EXPECT_FLOAT_EQ(tube.GetDrive(), 2.0f);

    // Test bias parameter
    tube.SetBias(0.5f);
    EXPECT_FLOAT_EQ(tube.GetBias(), 0.5f);

    // Test distortion parameter
    tube.SetDistortion(2.0f);
    EXPECT_FLOAT_EQ(tube.GetDistortion(), 2.0f);

    // Test high pass pole
    tube.SetHighPassPole(0.95f);
    EXPECT_FLOAT_EQ(tube.GetHighPassPole(), 0.95f);

    // Test low pass pole
    tube.SetLowPassPole(0.7f);
    EXPECT_FLOAT_EQ(tube.GetLowPassPole(), 0.7f);

    // Test mix parameter
    tube.SetMix(0.5f);
    EXPECT_FLOAT_EQ(tube.GetMix(), 0.5f);
}

// Test zero input
TEST_F(TubeTest, ZeroInput)
{
    float in = 0.0f;
    float out = tube.Process(in);
    
    // Zero input should produce zero output (or very close)
    EXPECT_NEAR(out, 0.0f, 1e-6f);
}

// Test unity gain with no distortion
TEST_F(TubeTest, UnityGain)
{
    // Set parameters for unity gain
    tube.SetDrive(1.0f);
    tube.SetBias(0.0f);
    tube.SetDistortion(1.0f);
    tube.SetMix(1.0f);

    float in = 0.5f;
    float out = tube.Process(in);
    
    // With these settings, output should be close to input
    EXPECT_NEAR(out, in, 0.1f);
}

// Test dry/wet mix
TEST_F(TubeTest, DryWetMix)
{
    float in = 0.5f;
    
    // Test fully wet (100% effect)
    tube.SetMix(1.0f);
    float wet = tube.Process(in);
    
    // Test fully dry (0% effect)
    tube.SetMix(0.0f);
    float dry = tube.Process(in);
    
    // Dry should equal input
    EXPECT_NEAR(dry, in, 1e-6f);
    
    // Wet should be different from dry (distortion applied)
    EXPECT_NE(wet, dry);
}

// Test parameter ranges
TEST_F(TubeTest, ParameterRanges)
{
    // Test that parameters accept reasonable ranges
    EXPECT_NO_THROW(tube.SetDrive(0.1f));
    EXPECT_NO_THROW(tube.SetDrive(10.0f));
    
    EXPECT_NO_THROW(tube.SetBias(-1.0f));
    EXPECT_NO_THROW(tube.SetBias(1.0f));
    
    EXPECT_NO_THROW(tube.SetDistortion(0.1f));
    EXPECT_NO_THROW(tube.SetDistortion(10.0f));
    
    EXPECT_NO_THROW(tube.SetHighPassPole(0.9f));
    EXPECT_NO_THROW(tube.SetHighPassPole(0.999f));
    
    EXPECT_NO_THROW(tube.SetLowPassPole(0.1f));
    EXPECT_NO_THROW(tube.SetLowPassPole(0.9f));
    
    EXPECT_NO_THROW(tube.SetMix(0.0f));
    EXPECT_NO_THROW(tube.SetMix(1.0f));
}

// Test output range
TEST_F(TubeTest, OutputRange)
{
    // Process a range of inputs
    for (int i = -10; i <= 10; i++)
    {
        float in = static_cast<float>(i) * 0.1f;
        float out = tube.Process(in);
        
        // Output should be finite (not NaN or infinity)
        EXPECT_TRUE(std::isfinite(out));
    }
}

// Test state preservation
TEST_F(TubeTest, StatePreservation)
{
    float in1 = 0.5f;
    float out1 = tube.Process(in1);
    
    float in2 = 0.3f;
    float out2 = tube.Process(in2);
    
    // Outputs should be different for different inputs
    EXPECT_NE(out1, out2);
}

// Test different sample rates
TEST_F(TubeTest, DifferentSampleRates)
{
    // Test with different sample rates
    EXPECT_NO_THROW(tube.Init(44100.0f));
    EXPECT_NO_THROW(tube.Init(48000.0f));
    EXPECT_NO_THROW(tube.Init(96000.0f));
}

// Main function for Google Test
int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
