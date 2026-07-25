#include "pedal_harness/digital_delay_node.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace {

using phh::DigitalDelayNode;

std::array<float, DigitalDelayNode::ParameterCount> DefaultParameters() {
    std::array<float, DigitalDelayNode::ParameterCount> parameters{};
    parameters[DigitalDelayNode::DelaySamples] = 1.0F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;
    parameters[DigitalDelayNode::InputSend] = 1.0F;
    parameters[DigitalDelayNode::DryMix] = 0.0F;
    parameters[DigitalDelayNode::WetMix] = 1.0F;
    parameters[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    parameters[DigitalDelayNode::FeedbackHighCutHz] = 23520.0F;
    return parameters;
}

phh::ParameterSnapshot Snapshot(
    const std::array<float, DigitalDelayNode::ParameterCount>& parameters) {
    return phh::ParameterSnapshot{parameters.data(), parameters.size(), 1U};
}

TEST(DigitalDelayNode, DelaysStereoImpulseByRequestedIntegerSamples) {
    constexpr std::size_t kFrames = 8U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(8U);
    ASSERT_TRUE(delay.Prepare({48000.0F, kFrames, phh::ChannelLayout::Stereo},
                              arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 3.0F;

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    input_right[0] = -1.0F;

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(block, Snapshot(parameters), diagnostics);

    EXPECT_FLOAT_EQ(output_left[3], 1.0F);
    EXPECT_FLOAT_EQ(output_right[3], -1.0F);
    EXPECT_FLOAT_EQ(output_left[2], 0.0F);
}

TEST(DigitalDelayNode, KeepsInputInjectionIndependentFromFeedback) {
    constexpr std::size_t kFrames = 6U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(8U);
    ASSERT_TRUE(delay.Prepare({48000.0F, kFrames, phh::ChannelLayout::Stereo},
                              arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 2.0F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(block, Snapshot(parameters), diagnostics);

    EXPECT_FLOAT_EQ(output_left[2], 1.0F);
    EXPECT_FLOAT_EQ(output_left[4], 0.0F);
}

TEST(DigitalDelayNode, ProducesExpectedFeedbackDecay) {
    constexpr std::size_t kFrames = 8U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(8U);
    ASSERT_TRUE(delay.Prepare({48000.0F, kFrames, phh::ChannelLayout::Stereo},
                              arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 2.0F;
    parameters[DigitalDelayNode::Feedback] = 0.5F;

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(block, Snapshot(parameters), diagnostics);

    EXPECT_FLOAT_EQ(output_left[2], 1.0F);
    EXPECT_FLOAT_EQ(output_left[4], 0.5F);
    EXPECT_FLOAT_EQ(output_left[6], 0.25F);
}

TEST(DigitalDelayNode, PreservesStateAcrossBlockBoundariesAndWrap) {
    constexpr std::size_t kBlockFrames = 3U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(5U);
    ASSERT_TRUE(delay.Prepare(
        {48000.0F, kBlockFrames, phh::ChannelLayout::Stereo}, arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 4.0F;
    std::array<float, 9U> collected{};
    phh::DiagnosticsCounters diagnostics{};

    for(std::size_t block_index = 0; block_index < 3U; ++block_index) {
        std::array<float, kBlockFrames> input_left{};
        std::array<float, kBlockFrames> input_right{};
        std::array<float, kBlockFrames> output_left{};
        std::array<float, kBlockFrames> output_right{};
        if(block_index == 0U) {
            input_left[0] = 1.0F;
        }

        phh::AudioBlock block{{input_left.data(), input_right.data()},
                              {output_left.data(), output_right.data()},
                              kBlockFrames};
        delay.Process(block, Snapshot(parameters), diagnostics);
        for(std::size_t frame = 0; frame < kBlockFrames; ++frame) {
            collected[block_index * kBlockFrames + frame] = output_left[frame];
        }
    }

    EXPECT_FLOAT_EQ(collected[4], 1.0F);
    EXPECT_FLOAT_EQ(collected[3], 0.0F);
    EXPECT_FLOAT_EQ(collected[5], 0.0F);
}

TEST(DigitalDelayNode, ResetFlushesStoredTail) {
    constexpr std::size_t kFrames = 4U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(8U);
    ASSERT_TRUE(delay.Prepare({48000.0F, kFrames, phh::ChannelLayout::Stereo},
                              arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 4.0F;

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    phh::AudioBlock first{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(first, Snapshot(parameters), diagnostics);

    delay.Reset(phh::ResetReason::UserRequest);
    input_left.fill(0.0F);
    output_left.fill(0.0F);
    output_right.fill(0.0F);
    phh::AudioBlock second{{input_left.data(), input_right.data()},
                           {output_left.data(), output_right.data()},
                           kFrames};
    delay.Process(second, Snapshot(parameters), diagnostics);

    for(const float sample : output_left) {
        EXPECT_FLOAT_EQ(sample, 0.0F);
    }
}

TEST(DigitalDelayNode, ClampsLimitsAndSanitizesNonFiniteInput) {
    constexpr std::size_t kFrames = 8U;
    std::array<std::uint8_t, 512> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(5U);
    ASSERT_TRUE(delay.Prepare({48000.0F, kFrames, phh::ChannelLayout::Stereo},
                              arena));

    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 1000.0F;
    parameters[DigitalDelayNode::Feedback] = 5.0F;

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = std::numeric_limits<float>::quiet_NaN();
    input_left[1] = 1.0F;

    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          kFrames};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(block, Snapshot(parameters), diagnostics);

    EXPECT_GE(diagnostics.non_finite_samples, 1U);
    EXPECT_FLOAT_EQ(output_left[6], 1.0F);
    for(const float sample : output_left) {
        EXPECT_TRUE(std::isfinite(sample));
    }
}

TEST(DigitalDelayNode, RejectsUnsupportedLayoutAndInsufficientMemory) {
    std::array<std::uint8_t, 32> small_storage{};
    phh::StaticArena small_arena(small_storage.data(), small_storage.size());
    DigitalDelayNode delay(64U);
    EXPECT_FALSE(delay.Prepare(
        {48000.0F, 8U, phh::ChannelLayout::Stereo}, small_arena));

    std::array<std::uint8_t, 1024> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    EXPECT_FALSE(
        delay.Prepare({48000.0F, 8U, phh::ChannelLayout::Mono}, arena));
}

} // namespace
