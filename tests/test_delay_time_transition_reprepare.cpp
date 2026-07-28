#include "pedal_harness/delay_time_transition.hpp"

#include <gtest/gtest.h>

TEST(DelayTimeTransitionReprepare, FailedReprepareInvalidatesPriorState) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    ASSERT_TRUE(transition.Request(
        20.0F, phh::DelayTransitionPolicy::SlewPitch, 5U));

    phh::DelayTransitionConfig invalid{};
    invalid.minimum_delay_samples = 0.0F;
    EXPECT_FALSE(transition.Prepare(invalid));

    EXPECT_FALSE(transition.IsActive());
    EXPECT_FALSE(transition.HasQueuedTarget());
    EXPECT_FALSE(transition.Request(
        30.0F, phh::DelayTransitionPolicy::SlewPitch, 5U));

    const auto plan = transition.Next();
    EXPECT_FALSE(plan.transition_active);
    EXPECT_FALSE(plan.transition_completed);
}
