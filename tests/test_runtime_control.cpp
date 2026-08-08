#include "pedal_harness/runtime_control.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <limits>

namespace {

TEST(RuntimeControlMailbox, PublishesCoherentFramesAndGeneration) {
    phh::RealtimeControlMailbox<4U> mailbox;
    phh::RealtimeControlFrame<4U> frame{};

    const std::array<float, 4U> first{{0.1F, 0.2F, 0.3F, 0.4F}};
    mailbox.Publish(first, false);

    ASSERT_TRUE(mailbox.ReadBounded(frame));
    EXPECT_EQ(frame.generation, 1U);
    EXPECT_FALSE(frame.bypass_with_trails);
    EXPECT_EQ(frame.parameters, first);

    const std::array<float, 4U> second{{0.9F, 0.8F, 0.7F, 0.6F}};
    mailbox.Publish(second, true);

    ASSERT_TRUE(mailbox.ReadBounded(frame));
    EXPECT_EQ(frame.generation, 2U);
    EXPECT_TRUE(frame.bypass_with_trails);
    EXPECT_EQ(frame.parameters, second);
}

TEST(RuntimeControlMapping, ClampNormalizedRejectsInvalidInputs) {
    EXPECT_FLOAT_EQ(phh::ClampNormalized(-0.25F), 0.0F);
    EXPECT_FLOAT_EQ(phh::ClampNormalized(0.5F), 0.5F);
    EXPECT_FLOAT_EQ(phh::ClampNormalized(1.25F), 1.0F);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FLOAT_EQ(phh::ClampNormalized(nan, 0.375F), 0.375F);

    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(phh::ClampNormalized(inf, 0.625F), 0.625F);
}

TEST(RuntimeControlGestures, ToggleEdgesAreIndependentAndDeterministic) {
    phh::ToggleGestureState state{};

    state.Update(false, false);
    EXPECT_FALSE(state.cubic_interpolation);
    EXPECT_FALSE(state.bypass_with_trails);

    state.Update(true, false);
    EXPECT_TRUE(state.cubic_interpolation);
    EXPECT_FALSE(state.bypass_with_trails);

    state.Update(false, true);
    EXPECT_TRUE(state.cubic_interpolation);
    EXPECT_TRUE(state.bypass_with_trails);

    state.Update(true, true);
    EXPECT_FALSE(state.cubic_interpolation);
    EXPECT_FALSE(state.bypass_with_trails);
}

} // namespace
