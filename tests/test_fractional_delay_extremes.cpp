#include "pedal_harness/fractional_delay.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <limits>

TEST(FractionalDelay, ExtremeIntegerBasesWrapWithoutNeighborOverflow) {
    const std::array<float, 8> buffer{0.0F, 1.0F, 2.0F, 3.0F,
                                      4.0F, 5.0F, 6.0F, 7.0F};
    const phh::CircularFractionalReader reader(buffer.data(), buffer.size());
    const auto maximum = std::numeric_limits<std::ptrdiff_t>::max();
    const auto minimum = std::numeric_limits<std::ptrdiff_t>::min();
    const auto capacity = static_cast<std::ptrdiff_t>(buffer.size());

    for(const auto policy : {phh::InterpolationPolicy::Linear,
                             phh::InterpolationPolicy::CubicLagrange}) {
        EXPECT_NEAR(reader.Read(maximum, 0.25F, policy),
                    reader.Read(maximum % capacity, 0.25F, policy),
                    1.0e-6F);
        EXPECT_NEAR(reader.Read(minimum, 0.25F, policy),
                    reader.Read(minimum % capacity, 0.25F, policy),
                    1.0e-6F);

        EXPECT_FLOAT_EQ(reader.Read(maximum, 1.25F, policy), 0.0F);
        EXPECT_FLOAT_EQ(reader.Read(minimum, -0.25F, policy), 0.0F);
    }
}
