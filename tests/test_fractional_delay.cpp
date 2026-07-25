#include "pedal_harness/fractional_delay.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

constexpr float kPi = 3.14159265358979323846F;

float IdealSine(float sample_index, float normalized_frequency) {
    return std::sin(2.0F * kPi * normalized_frequency * sample_index);
}

float RmsError(phh::InterpolationPolicy policy,
               float normalized_frequency,
               float fraction) {
    constexpr std::size_t kCount = 4096U;
    std::array<float, kCount> buffer{};
    for(std::size_t index = 0; index < kCount; ++index) {
        buffer[index] = IdealSine(static_cast<float>(index), normalized_frequency);
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    double sum_squared = 0.0;
    constexpr std::size_t kStart = 32U;
    constexpr std::size_t kSamples = 2048U;
    for(std::size_t index = kStart; index < kStart + kSamples; ++index) {
        const float actual = reader.Read(static_cast<float>(index) + fraction, policy);
        const float ideal = IdealSine(static_cast<float>(index) + fraction,
                                      normalized_frequency);
        const double error = static_cast<double>(actual - ideal);
        sum_squared += error * error;
    }
    return static_cast<float>(std::sqrt(sum_squared / static_cast<double>(kSamples)));
}

} // namespace

TEST(FractionalDelay, IntegerPositionsAreExactForBothPolicies) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    for(std::size_t index = 0; index < buffer.size(); ++index) {
        EXPECT_FLOAT_EQ(reader.Read(static_cast<float>(index),
                                    phh::InterpolationPolicy::Linear),
                        buffer[index]);
        EXPECT_NEAR(reader.Read(static_cast<float>(index),
                                phh::InterpolationPolicy::CubicLagrange),
                    buffer[index],
                    1.0e-6F);
    }
}

TEST(FractionalDelay, CubicLagrangeReducesStaticFractionalError) {
    for(const float frequency : {0.05F, 0.15F, 0.30F, 0.40F}) {
        for(const float fraction : {0.125F, 0.25F, 0.5F, 0.75F, 0.875F}) {
            const float linear = RmsError(phh::InterpolationPolicy::Linear,
                                          frequency,
                                          fraction);
            const float cubic = RmsError(phh::InterpolationPolicy::CubicLagrange,
                                         frequency,
                                         fraction);
            EXPECT_LT(cubic, linear)
                << "frequency=" << frequency << " fraction=" << fraction;
        }
    }
}

TEST(FractionalDelay, WrapsNegativeAndPositivePositions) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    EXPECT_NEAR(reader.Read(-0.5F, phh::InterpolationPolicy::Linear), 3.5F, 1.0e-6F);
    EXPECT_NEAR(reader.Read(7.5F, phh::InterpolationPolicy::Linear), 3.5F, 1.0e-6F);
    EXPECT_NEAR(reader.Read(15.5F, phh::InterpolationPolicy::Linear), 3.5F, 1.0e-6F);

    const float cubic_negative =
        reader.Read(-0.25F, phh::InterpolationPolicy::CubicLagrange);
    const float cubic_positive =
        reader.Read(7.75F, phh::InterpolationPolicy::CubicLagrange);
    EXPECT_NEAR(cubic_negative, cubic_positive, 1.0e-6F);
}

TEST(FractionalDelay, RejectsInvalidReaderAndNonFinitePosition) {
    const std::array<float, 3> too_small{1.0F, 2.0F, 3.0F};
    const phh::CircularFractionalReader invalid(too_small.data(), too_small.size());
    EXPECT_FALSE(invalid.IsValid());
    EXPECT_FLOAT_EQ(invalid.Read(0.5F, phh::InterpolationPolicy::Linear), 0.0F);

    const std::array<float, 4> valid_buffer{1.0F, 2.0F, 3.0F, 4.0F};
    const phh::CircularFractionalReader valid(valid_buffer.data(), valid_buffer.size());
    EXPECT_FLOAT_EQ(valid.Read(std::numeric_limits<float>::infinity(),
                               phh::InterpolationPolicy::CubicLagrange),
                    0.0F);
}
