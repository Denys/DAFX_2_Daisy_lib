#include "pedal_harness/delay_time_transition.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <limits>

namespace {

constexpr float kTolerance = 1.0e-5F;

TEST(DelayTimeTransition, RejectsInvalidConfiguration) {
    phh::DelayTimeTransition transition;

    phh::DelayTransitionConfig config{};
    config.minimum_delay_samples = 0.0F;
    EXPECT_FALSE(transition.Prepare(config));

    config.minimum_delay_samples = 10.0F;
    config.maximum_delay_samples = 5.0F;
    EXPECT_FALSE(transition.Prepare(config));

    config.maximum_delay_samples = 100.0F;
    config.default_slew_samples = 0U;
    EXPECT_FALSE(transition.Prepare(config));
}

TEST(DelayTimeTransition, ResetClampsAndProducesIdlePlan) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{
        10.0F, 100.0F, 5U, 5U, phh::CrossfadeLaw::EqualPower}));

    transition.Reset(500.0F);
    const auto plan = transition.Next();

    EXPECT_FLOAT_EQ(plan.delay_a_samples, 100.0F);
    EXPECT_FLOAT_EQ(plan.delay_b_samples, 100.0F);
    EXPECT_FLOAT_EQ(plan.gain_a, 1.0F);
    EXPECT_FLOAT_EQ(plan.gain_b, 0.0F);
    EXPECT_FALSE(plan.transition_active);
    EXPECT_FALSE(plan.transition_completed);
    EXPECT_FALSE(transition.IsActive());
}

TEST(DelayTimeTransition, ImmediateGlitchCommitsOnNextPlan) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(transition.Request(
        40.0F, phh::DelayTransitionPolicy::ImmediateGlitch, 99U));

    EXPECT_FLOAT_EQ(transition.CurrentDelaySamples(), 40.0F);
    EXPECT_FALSE(transition.IsActive());

    const auto completion = transition.Next();
    EXPECT_FLOAT_EQ(completion.delay_a_samples, 40.0F);
    EXPECT_TRUE(completion.transition_completed);

    const auto idle = transition.Next();
    EXPECT_FALSE(idle.transition_completed);
}

TEST(DelayTimeTransition, SlewIncludesExactSourceAndTarget) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(
        transition.Request(20.0F, phh::DelayTransitionPolicy::SlewPitch, 5U));

    const std::array<float, 5> expected{10.0F, 12.5F, 15.0F, 17.5F, 20.0F};

    for(std::size_t index = 0; index < expected.size(); ++index) {
        const auto plan = transition.Next();
        EXPECT_NEAR(plan.delay_a_samples, expected[index], kTolerance);
        EXPECT_NEAR(plan.delay_b_samples, expected[index], kTolerance);
        EXPECT_FALSE(plan.second_head_active);
        EXPECT_TRUE(plan.transition_active);
        EXPECT_EQ(plan.transition_completed, index + 1U == expected.size());
    }

    EXPECT_FALSE(transition.IsActive());
    EXPECT_FLOAT_EQ(transition.CurrentDelaySamples(), 20.0F);
}

TEST(DelayTimeTransition, SlewRetargetsFromCurrentInstantaneousDelay) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(
        transition.Request(20.0F, phh::DelayTransitionPolicy::SlewPitch, 5U));

    EXPECT_NEAR(transition.Next().delay_a_samples, 10.0F, kTolerance);
    EXPECT_NEAR(transition.Next().delay_a_samples, 12.5F, kTolerance);

    ASSERT_TRUE(
        transition.Request(30.0F, phh::DelayTransitionPolicy::SlewPitch, 3U));

    EXPECT_NEAR(transition.Next().delay_a_samples, 12.5F, kTolerance);
    EXPECT_NEAR(transition.Next().delay_a_samples, 21.25F, kTolerance);
    const auto final = transition.Next();
    EXPECT_NEAR(final.delay_a_samples, 30.0F, kTolerance);
    EXPECT_TRUE(final.transition_completed);
}

TEST(DelayTimeTransition, EqualPowerCrossfadeHasExactEndpointsAndEnergy) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{
        1.0F, 1000.0F, 5U, 5U, phh::CrossfadeLaw::EqualPower}));
    transition.Reset(100.0F);

    ASSERT_TRUE(transition.Request(
        200.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 5U));

    for(std::size_t index = 0; index < 5U; ++index) {
        const auto plan = transition.Next();
        EXPECT_FLOAT_EQ(plan.delay_a_samples, 100.0F);
        EXPECT_FLOAT_EQ(plan.delay_b_samples, 200.0F);
        EXPECT_TRUE(plan.second_head_active);
        EXPECT_TRUE(plan.transition_active);
        EXPECT_NEAR(plan.gain_a * plan.gain_a + plan.gain_b * plan.gain_b,
                    1.0F,
                    kTolerance);

        if(index == 0U) {
            EXPECT_NEAR(plan.gain_a, 1.0F, kTolerance);
            EXPECT_NEAR(plan.gain_b, 0.0F, kTolerance);
        }
        if(index == 4U) {
            EXPECT_FLOAT_EQ(plan.gain_a, 0.0F);
            EXPECT_FLOAT_EQ(plan.gain_b, 1.0F);
            EXPECT_TRUE(plan.transition_completed);
        }
    }

    EXPECT_FALSE(transition.IsActive());
    EXPECT_FLOAT_EQ(transition.CurrentDelaySamples(), 200.0F);
}

TEST(DelayTimeTransition, LinearCrossfadeUsesLinearAmplitudeWeights) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{
        1.0F, 1000.0F, 4U, 3U, phh::CrossfadeLaw::LinearAmplitude}));
    transition.Reset(100.0F);

    ASSERT_TRUE(transition.Request(
        200.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 3U));

    const auto start = transition.Next();
    const auto middle = transition.Next();
    const auto end = transition.Next();

    EXPECT_FLOAT_EQ(start.gain_a, 1.0F);
    EXPECT_FLOAT_EQ(start.gain_b, 0.0F);
    EXPECT_NEAR(middle.gain_a, 0.5F, kTolerance);
    EXPECT_NEAR(middle.gain_b, 0.5F, kTolerance);
    EXPECT_FLOAT_EQ(end.gain_a, 0.0F);
    EXPECT_FLOAT_EQ(end.gain_b, 1.0F);
}

TEST(DelayTimeTransition, CrossfadeQueueIsLastWriterWinsAndBounded) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{
        1.0F, 1000.0F, 5U, 3U, phh::CrossfadeLaw::EqualPower}));
    transition.Reset(10.0F);

    ASSERT_TRUE(transition.Request(
        20.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 3U));
    EXPECT_FLOAT_EQ(transition.Next().delay_a_samples, 10.0F);

    ASSERT_TRUE(transition.Request(
        30.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 3U));
    ASSERT_TRUE(transition.Request(
        40.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 3U));
    EXPECT_TRUE(transition.HasQueuedTarget());

    static_cast<void>(transition.Next());
    const auto first_final = transition.Next();
    EXPECT_TRUE(first_final.transition_completed);
    EXPECT_FLOAT_EQ(transition.CurrentDelaySamples(), 20.0F);
    EXPECT_TRUE(transition.HasQueuedTarget());

    const auto queued_start = transition.Next();
    EXPECT_FLOAT_EQ(queued_start.delay_a_samples, 20.0F);
    EXPECT_FLOAT_EQ(queued_start.delay_b_samples, 40.0F);
    EXPECT_FALSE(transition.HasQueuedTarget());
}

TEST(DelayTimeTransition, CrossfadeQueuesSlewForNextTransition) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(transition.Request(
        20.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 2U));
    ASSERT_TRUE(
        transition.Request(30.0F, phh::DelayTransitionPolicy::SlewPitch, 3U));

    static_cast<void>(transition.Next());
    const auto first_final = transition.Next();
    EXPECT_TRUE(first_final.transition_completed);

    const auto slew_start = transition.Next();
    EXPECT_FLOAT_EQ(slew_start.delay_a_samples, 20.0F);
    EXPECT_FALSE(slew_start.second_head_active);

    EXPECT_NEAR(transition.Next().delay_a_samples, 25.0F, kTolerance);
    const auto slew_final = transition.Next();
    EXPECT_NEAR(slew_final.delay_a_samples, 30.0F, kTolerance);
    EXPECT_TRUE(slew_final.transition_completed);
}

TEST(DelayTimeTransition, OneSampleTransitionsCompleteAtTarget) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(
        transition.Request(20.0F, phh::DelayTransitionPolicy::SlewPitch, 1U));
    const auto slew = transition.Next();
    EXPECT_FLOAT_EQ(slew.delay_a_samples, 20.0F);
    EXPECT_TRUE(slew.transition_completed);

    ASSERT_TRUE(transition.Request(
        30.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 1U));
    const auto crossfade = transition.Next();
    EXPECT_FLOAT_EQ(crossfade.delay_a_samples, 20.0F);
    EXPECT_FLOAT_EQ(crossfade.delay_b_samples, 30.0F);
    EXPECT_FLOAT_EQ(crossfade.gain_a, 0.0F);
    EXPECT_FLOAT_EQ(crossfade.gain_b, 1.0F);
    EXPECT_TRUE(crossfade.transition_completed);
}

TEST(DelayTimeTransition, RejectsNonFiniteRequestWithoutCorruptingState) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(42.0F);

    EXPECT_FALSE(transition.Request(
        std::numeric_limits<float>::quiet_NaN(),
        phh::DelayTransitionPolicy::SlewPitch,
        5U));
    EXPECT_FALSE(transition.Request(
        std::numeric_limits<float>::infinity(),
        phh::DelayTransitionPolicy::DualHeadCrossfade,
        5U));

    const auto plan = transition.Next();
    EXPECT_FLOAT_EQ(plan.delay_a_samples, 42.0F);
    EXPECT_FALSE(plan.transition_active);
}

TEST(DelayTimeTransition, ResetCancelsActiveAndQueuedTransitions) {
    phh::DelayTimeTransition transition;
    ASSERT_TRUE(transition.Prepare(phh::DelayTransitionConfig{}));
    transition.Reset(10.0F);

    ASSERT_TRUE(transition.Request(
        20.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 5U));
    ASSERT_TRUE(transition.Request(
        30.0F, phh::DelayTransitionPolicy::DualHeadCrossfade, 5U));
    EXPECT_TRUE(transition.IsActive());
    EXPECT_TRUE(transition.HasQueuedTarget());

    transition.Reset(15.0F);

    EXPECT_FALSE(transition.IsActive());
    EXPECT_FALSE(transition.HasQueuedTarget());
    EXPECT_FLOAT_EQ(transition.Next().delay_a_samples, 15.0F);
}

} // namespace
