#include "pedal_harness/fractional_delay.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <limits>
#include <vector>

namespace {

constexpr double kPi = 3.14159265358979323846;

float IdealSine(double sample_index, double normalized_frequency) {
    return static_cast<float>(
        std::sin(2.0 * kPi * normalized_frequency * sample_index));
}

double RmsError(phh::InterpolationPolicy policy,
                double normalized_frequency,
                float fraction) {
    constexpr std::size_t kCount = 4096U;
    std::array<float, kCount> buffer{};
    for(std::size_t index = 0; index < kCount; ++index) {
        buffer[index] = IdealSine(static_cast<double>(index), normalized_frequency);
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    double sum_squared = 0.0;
    constexpr std::size_t kStart = 32U;
    constexpr std::size_t kSamples = 2048U;
    for(std::size_t index = kStart; index < kStart + kSamples; ++index) {
        const float actual = reader.Read(static_cast<std::ptrdiff_t>(index),
                                         fraction,
                                         policy);
        const float ideal = IdealSine(static_cast<double>(index) + fraction,
                                      normalized_frequency);
        const double error = static_cast<double>(actual - ideal);
        sum_squared += error * error;
    }
    return std::sqrt(sum_squared / static_cast<double>(kSamples));
}

std::complex<double> AnalyticalResponse(phh::InterpolationPolicy policy,
                                        double normalized_frequency,
                                        double fraction) {
    const double omega = 2.0 * kPi * normalized_frequency;
    if(policy == phh::InterpolationPolicy::Linear) {
        return (1.0 - fraction)
               + fraction * std::exp(std::complex<double>(0.0, omega));
    }

    const std::array<double, 4> coefficients{
        -fraction * (fraction - 1.0) * (fraction - 2.0) / 6.0,
        (fraction + 1.0) * (fraction - 1.0) * (fraction - 2.0) / 2.0,
        -(fraction + 1.0) * fraction * (fraction - 2.0) / 2.0,
        (fraction + 1.0) * fraction * (fraction - 1.0) / 6.0,
    };
    const std::array<int, 4> offsets{-1, 0, 1, 2};

    std::complex<double> response{0.0, 0.0};
    for(std::size_t index = 0; index < coefficients.size(); ++index) {
        response += coefficients[index]
                    * std::exp(std::complex<double>(
                        0.0, omega * static_cast<double>(offsets[index])));
    }
    return response;
}

std::complex<double> MeasuredResponse(phh::InterpolationPolicy policy,
                                      std::size_t frequency_bin,
                                      float fraction) {
    constexpr std::size_t kCount = 4096U;
    const double normalized_frequency =
        static_cast<double>(frequency_bin) / static_cast<double>(kCount);
    const double omega = 2.0 * kPi * normalized_frequency;

    std::vector<float> buffer(kCount);
    for(std::size_t index = 0; index < kCount; ++index) {
        buffer[index] = static_cast<float>(
            std::cos(omega * static_cast<double>(index)));
    }

    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    std::complex<double> input_bin{0.0, 0.0};
    std::complex<double> output_bin{0.0, 0.0};
    for(std::size_t index = 0; index < kCount; ++index) {
        const auto basis = std::exp(std::complex<double>(
            0.0, -omega * static_cast<double>(index)));
        input_bin += static_cast<double>(buffer[index]) * basis;
        output_bin += static_cast<double>(reader.Read(
                          static_cast<std::ptrdiff_t>(index), fraction, policy))
                      * basis;
    }
    return output_bin / input_bin;
}

double WrappedPhaseError(double measured, double expected) {
    return std::remainder(measured - expected, 2.0 * kPi);
}

} // namespace

TEST(FractionalDelay, IntegerPositionsAreExactForBothPolicies) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    for(std::size_t index = 0; index < buffer.size(); ++index) {
        EXPECT_FLOAT_EQ(reader.Read(static_cast<std::ptrdiff_t>(index),
                                    0.0F,
                                    phh::InterpolationPolicy::Linear),
                        buffer[index]);
        EXPECT_NEAR(reader.Read(static_cast<std::ptrdiff_t>(index),
                                0.0F,
                                phh::InterpolationPolicy::CubicLagrange),
                    buffer[index],
                    1.0e-6F);
    }
}

TEST(FractionalDelay, CubicBasisAndEndpointsAreExact) {
    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(1.0F, 0.0F, 0.0F, 0.0F, 0.5F),
                    -1.0F / 16.0F);
    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(0.0F, 1.0F, 0.0F, 0.0F, 0.5F),
                    9.0F / 16.0F);
    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(0.0F, 0.0F, 1.0F, 0.0F, 0.5F),
                    9.0F / 16.0F);
    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(0.0F, 0.0F, 0.0F, 1.0F, 0.5F),
                    -1.0F / 16.0F);

    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(-3.0F, 2.0F, 7.0F, 11.0F, 0.0F),
                    2.0F);
    EXPECT_FLOAT_EQ(phh::InterpolateCubicLagrange(-3.0F, 2.0F, 7.0F, 11.0F, 1.0F),
                    7.0F);
}

TEST(FractionalDelay, CubicLagrangeReducesStaticFractionalError) {
    for(const double frequency : {0.05, 0.15, 0.30, 0.40, 0.45}) {
        for(const float fraction : {0.125F, 0.25F, 0.5F, 0.75F, 0.875F}) {
            const double linear = RmsError(phh::InterpolationPolicy::Linear,
                                           frequency,
                                           fraction);
            const double cubic = RmsError(phh::InterpolationPolicy::CubicLagrange,
                                          frequency,
                                          fraction);
            EXPECT_LT(cubic, linear)
                << "frequency=" << frequency << " fraction=" << fraction;
        }
    }
}

TEST(FractionalDelay, MeetsDocumentedStaticErrorLimits) {
    struct Limit {
        double frequency;
        double linear_max;
        double cubic_max;
    };
    const std::array<Limit, 3> limits{{
        {0.05, 0.010, 0.0003},
        {0.15, 0.080, 0.0140},
        {0.30, 0.310, 0.1700},
    }};

    for(const auto& limit : limits) {
        EXPECT_LT(RmsError(phh::InterpolationPolicy::Linear,
                           limit.frequency,
                           0.5F),
                  limit.linear_max);
        EXPECT_LT(RmsError(phh::InterpolationPolicy::CubicLagrange,
                           limit.frequency,
                           0.5F),
                  limit.cubic_max);
    }

    // Third-order interpolation still degrades strongly close to Nyquist.
    EXPECT_GT(RmsError(phh::InterpolationPolicy::CubicLagrange, 0.45, 0.5F),
              0.50);
}

TEST(FractionalDelay, AnalyticalAndMeasuredComplexGainAgree) {
    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        for(const std::size_t bin : {205U, 614U, 1229U, 1638U}) {
            for(const float fraction : {0.125F, 0.5F, 0.875F}) {
                constexpr double kCount = 4096.0;
                const auto expected = AnalyticalResponse(
                    policy, static_cast<double>(bin) / kCount, fraction);
                const auto measured = MeasuredResponse(policy, bin, fraction);

                EXPECT_NEAR(std::abs(measured), std::abs(expected), 2.0e-4);
                EXPECT_NEAR(WrappedPhaseError(std::arg(measured),
                                              std::arg(expected)),
                            0.0,
                            2.0e-4);
            }
        }
    }
}

TEST(FractionalDelay, WrapsAcrossBoundaryAndMultiplePeriods) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        EXPECT_NEAR(reader.Read(7, 0.5F, policy), 3.5F, 1.0e-6F);
        for(const std::ptrdiff_t period : {-32, -8, 0, 8, 56}) {
            EXPECT_NEAR(reader.Read(7 + period, 0.25F, policy),
                        reader.Read(7, 0.25F, policy),
                        1.0e-6F);
        }
    }
}

TEST(FractionalDelay, NormalizesFractionWithoutAbsoluteFloatPosition) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());

    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        EXPECT_NEAR(reader.Read(1, -0.25F, policy),
                    reader.Read(0, 0.75F, policy),
                    1.0e-6F);
        EXPECT_NEAR(reader.Read(1, 1.25F, policy),
                    reader.Read(2, 0.25F, policy),
                    1.0e-6F);
    }
}

TEST(FractionalDelay, RejectsInvalidReaderAndNonFiniteFraction) {
    const std::array<float, 3> too_small{1.0F, 2.0F, 3.0F};
    const phh::CircularFractionalReader invalid(too_small.data(), too_small.size());
    EXPECT_FALSE(invalid.IsValid());
    EXPECT_FLOAT_EQ(invalid.Read(0, 0.5F, phh::InterpolationPolicy::Linear), 0.0F);

    const std::array<float, 4> valid_buffer{1.0F, 2.0F, 3.0F, 4.0F};
    const phh::CircularFractionalReader valid(valid_buffer.data(), valid_buffer.size());
    for(const float invalid_fraction : {
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()}) {
        EXPECT_FLOAT_EQ(valid.Read(0,
                                   invalid_fraction,
                                   phh::InterpolationPolicy::CubicLagrange),
                        0.0F);
    }
}
