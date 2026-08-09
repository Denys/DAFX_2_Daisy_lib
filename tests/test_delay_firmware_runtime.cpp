#include "pedal_harness/delay_firmware_runtime.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace {

using phh::DelayFirmwareRuntime;
using phh::DigitalDelayNode;

DelayFirmwareRuntime::ParameterArray DefaultParameters() {
    DelayFirmwareRuntime::ParameterArray parameters{};
    parameters[DigitalDelayNode::DelaySamples] = 2.0F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;
    parameters[DigitalDelayNode::InputSend] = 1.0F;
    parameters[DigitalDelayNode::DryMix] = 0.0F;
    parameters[DigitalDelayNode::WetMix] = 1.0F;
    parameters[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    parameters[DigitalDelayNode::FeedbackHighCutHz] = 23520.0F;
    parameters[DigitalDelayNode::InterpolationMode] = 0.0F;
    return parameters;
}

template <std::size_t Frames>
phh::AudioBlock MakeBlock(std::array<float, Frames>& input_left,
                          std::array<float, Frames>& input_right,
                          std::array<float, Frames>& output_left,
                          std::array<float, Frames>& output_right) {
    return phh::AudioBlock{{input_left.data(), input_right.data()},
                           {output_left.data(), output_right.data()},
                           Frames};
}

TEST(DelayFirmwareRuntime, PublishesParametersAtBlockBoundary) {
    constexpr std::size_t kFrames = 4U;
    std::array<std::uint8_t, 1024U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DelayFirmwareRuntime runtime(8U);
    auto parameters = DefaultParameters();

    ASSERT_TRUE(runtime.Prepare(
        {48000.0F, kFrames, phh::ChannelLayout::Stereo}, arena, parameters));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    phh::DiagnosticsCounters diagnostics{};

    auto first = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(first, diagnostics));
    EXPECT_EQ(runtime.ActiveParameterGeneration(), 1U);

    parameters[DigitalDelayNode::DelaySamples] = 3.0F;
    runtime.PublishParameters(parameters);
    auto second = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(second, diagnostics));
    EXPECT_EQ(runtime.ActiveParameterGeneration(), 2U);
    EXPECT_EQ(runtime.SnapshotMisses(), 0U);
}

TEST(DelayFirmwareRuntime, PreserveTailsBypassStopsNewInputInjection) {
    constexpr std::size_t kFrames = 4U;
    std::array<std::uint8_t, 1024U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DelayFirmwareRuntime runtime(8U);
    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 2.0F;
    parameters[DigitalDelayNode::Feedback] = 0.5F;

    ASSERT_TRUE(runtime.Prepare(
        {48000.0F, kFrames, phh::ChannelLayout::Stereo}, arena, parameters));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    phh::DiagnosticsCounters diagnostics{};

    auto first = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(first, diagnostics));
    EXPECT_FLOAT_EQ(output_left[2], 1.0F);

    runtime.SetBypass(true,
                      DelayFirmwareRuntime::BypassPolicy::PreserveTails);
    input_left.fill(0.0F);
    input_left[0] = 0.25F;
    output_left.fill(0.0F);
    output_right.fill(0.0F);

    auto second = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(second, diagnostics));

    EXPECT_EQ(runtime.PublishedAudioState(),
              DelayFirmwareRuntime::AudioState::BypassTails);
    EXPECT_FLOAT_EQ(output_left[0], 0.75F);
    EXPECT_FLOAT_EQ(output_left[2], 0.25F);
}

TEST(DelayFirmwareRuntime, FlushBypassClearsHistoryWithBoundedAudioWork) {
    constexpr std::size_t kFrames = 4U;
    std::array<std::uint8_t, 1024U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DelayFirmwareRuntime runtime(4U);
    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 4.0F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;

    ASSERT_TRUE(runtime.Prepare(
        {48000.0F, kFrames, phh::ChannelLayout::Stereo}, arena, parameters));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    phh::DiagnosticsCounters diagnostics{};

    auto seed = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(seed, diagnostics));

    runtime.SetBypass(true, DelayFirmwareRuntime::BypassPolicy::Flush);
    input_left.fill(0.0F);
    output_left.fill(0.0F);
    output_right.fill(0.0F);

    for(std::size_t block = 0U; block < 2U; ++block) {
        auto clearing =
            MakeBlock(input_left, input_right, output_left, output_right);
        ASSERT_TRUE(runtime.ProcessAudio(clearing, diagnostics));
        for(const float sample : output_left) {
            EXPECT_FLOAT_EQ(sample, 0.0F);
        }
    }

    EXPECT_EQ(runtime.FlushFramesRemaining(), 0U);
    EXPECT_EQ(runtime.PublishedAudioState(),
              DelayFirmwareRuntime::AudioState::BypassDry);

    runtime.SetBypass(false);
    output_left.fill(0.0F);
    output_right.fill(0.0F);
    auto resumed = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(resumed, diagnostics));
    EXPECT_EQ(runtime.PublishedAudioState(),
              DelayFirmwareRuntime::AudioState::Effect);
    for(const float sample : output_left) {
        EXPECT_FLOAT_EQ(sample, 0.0F);
    }
}

TEST(DelayFirmwareRuntime, NonRealtimeResetFlushesTailWhenAudioIsStopped) {
    constexpr std::size_t kFrames = 4U;
    std::array<std::uint8_t, 1024U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DelayFirmwareRuntime runtime(4U);
    auto parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 4.0F;

    ASSERT_TRUE(runtime.Prepare(
        {48000.0F, kFrames, phh::ChannelLayout::Stereo}, arena, parameters));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    input_left[0] = 1.0F;
    phh::DiagnosticsCounters diagnostics{};

    auto seed = MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(seed, diagnostics));

    runtime.ResetNonRealtime(phh::ResetReason::UserRequest);
    input_left.fill(0.0F);
    output_left.fill(0.0F);
    output_right.fill(0.0F);
    auto after_reset =
        MakeBlock(input_left, input_right, output_left, output_right);
    ASSERT_TRUE(runtime.ProcessAudio(after_reset, diagnostics));

    for(const float sample : output_left) {
        EXPECT_FLOAT_EQ(sample, 0.0F);
    }
}

TEST(DelayFirmwareRuntime, RejectsInvalidAudioBlocks) {
    constexpr std::size_t kFrames = 5U;
    std::array<std::uint8_t, 1024U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DelayFirmwareRuntime runtime(8U);
    const auto parameters = DefaultParameters();

    ASSERT_TRUE(runtime.Prepare(
        {48000.0F, 4U, phh::ChannelLayout::Stereo}, arena, parameters));

    std::array<float, kFrames> input_left{};
    std::array<float, kFrames> input_right{};
    std::array<float, kFrames> output_left{};
    std::array<float, kFrames> output_right{};
    phh::DiagnosticsCounters diagnostics{};

    auto oversized =
        MakeBlock(input_left, input_right, output_left, output_right);
    EXPECT_FALSE(runtime.ProcessAudio(oversized, diagnostics));
    EXPECT_EQ(diagnostics.processed_blocks, 0U);
}

} // namespace
