#include "pedal_harness/digital_delay_node.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace {

using phh::DigitalDelayNode;

constexpr float kPi = 3.14159265358979323846F;

std::array<float, DigitalDelayNode::ParameterCount> DefaultFractionalParameters() {
    std::array<float, DigitalDelayNode::ParameterCount> parameters{};
    parameters[DigitalDelayNode::DelaySamples] = 1.0F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;
    parameters[DigitalDelayNode::InputSend] = 1.0F;
    parameters[DigitalDelayNode::DryMix] = 0.0F;
    parameters[DigitalDelayNode::WetMix] = 1.0F;
    parameters[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    parameters[DigitalDelayNode::FeedbackHighCutHz] = 23520.0F;
    parameters[DigitalDelayNode::InterpolationMode] = 0.0F;
    return parameters;
}

phh::ParameterSnapshot FractionalSnapshot(
    const std::array<float, DigitalDelayNode::ParameterCount>& parameters) {
    return phh::ParameterSnapshot{parameters.data(), parameters.size(), 1U};
}

std::vector<float> RenderFractionalImpulse(float delay_samples,
                                           float interpolation_mode,
                                           float feedback,
                                           std::size_t frames) {
    std::vector<std::uint8_t> storage(4096U);
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(128U);
    EXPECT_TRUE(delay.Prepare(
        {48000.0F, 1U, phh::ChannelLayout::Stereo}, arena));

    auto parameters = DefaultFractionalParameters();
    parameters[DigitalDelayNode::DelaySamples] = delay_samples;
    parameters[DigitalDelayNode::Feedback] = feedback;
    parameters[DigitalDelayNode::InterpolationMode] = interpolation_mode;

    std::vector<float> output(frames);
    phh::DiagnosticsCounters diagnostics{};
    for(std::size_t frame = 0; frame < frames; ++frame) {
        float input_left = frame == 0U ? 1.0F : 0.0F;
        float input_right = 0.0F;
        float output_left = 0.0F;
        float output_right = 0.0F;
        phh::AudioBlock block{{&input_left, &input_right},
                              {&output_left, &output_right},
                              1U};
        delay.Process(block, FractionalSnapshot(parameters), diagnostics);
        output[frame] = output_left;
    }
    EXPECT_EQ(diagnostics.non_finite_samples, 0U);
    return output;
}

double MovingSineRmsError(float interpolation_mode) {
    constexpr std::size_t kFrames = 48000U;
    constexpr float kSampleRate = 48000.0F;
    constexpr float kToneHz = 8000.0F;
    constexpr float kModulationHz = 5.0F;

    std::vector<std::uint8_t> storage(4096U);
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(128U);
    EXPECT_TRUE(delay.Prepare(
        {kSampleRate, 1U, phh::ChannelLayout::Stereo}, arena));

    auto parameters = DefaultFractionalParameters();
    parameters[DigitalDelayNode::InterpolationMode] = interpolation_mode;
    phh::DiagnosticsCounters diagnostics{};
    double squared_error = 0.0;
    std::size_t measured_frames = 0U;

    for(std::size_t frame = 0; frame < kFrames; ++frame) {
        const float delay_samples =
            32.5F
            + 0.45F
                  * std::sin(2.0F * kPi * kModulationHz
                             * static_cast<float>(frame) / kSampleRate);
        parameters[DigitalDelayNode::DelaySamples] = delay_samples;

        float input_left =
            std::sin(2.0F * kPi * kToneHz * static_cast<float>(frame)
                     / kSampleRate);
        float input_right = 0.0F;
        float output_left = 0.0F;
        float output_right = 0.0F;
        phh::AudioBlock block{{&input_left, &input_right},
                              {&output_left, &output_right},
                              1U};
        delay.Process(block, FractionalSnapshot(parameters), diagnostics);

        if(frame > 256U) {
            const double ideal =
                std::sin(2.0 * static_cast<double>(kPi)
                         * static_cast<double>(kToneHz)
                         * (static_cast<double>(frame)
                            - static_cast<double>(delay_samples))
                         / static_cast<double>(kSampleRate));
            const double error = static_cast<double>(output_left) - ideal;
            squared_error += error * error;
            measured_frames += 1U;
        }
    }

    EXPECT_EQ(diagnostics.non_finite_samples, 0U);
    return std::sqrt(squared_error / static_cast<double>(measured_frames));
}

double ToneProjection(const std::vector<float>& signal,
                      double center,
                      std::size_t half_window,
                      double frequency_hz,
                      double sample_rate_hz) {
    const long center_index = std::lround(center);
    std::complex<double> projection{0.0, 0.0};
    for(long index = center_index - static_cast<long>(half_window);
        index <= center_index + static_cast<long>(half_window);
        ++index) {
        if(index < 0 || static_cast<std::size_t>(index) >= signal.size()) {
            continue;
        }
        const double phase =
            -2.0 * static_cast<double>(kPi) * frequency_hz
            * static_cast<double>(index) / sample_rate_hz;
        projection +=
            static_cast<double>(signal[static_cast<std::size_t>(index)])
            * std::complex<double>(std::cos(phase), std::sin(phase));
    }
    return std::abs(projection);
}

TEST(DigitalDelayNodeFractional,
     LinearImpulseMatchesTwoTapHalfSampleKernel) {
    const auto output = RenderFractionalImpulse(3.5F, 0.0F, 0.0F, 8U);
    EXPECT_NEAR(output[3], 0.5F, 1.0e-6F);
    EXPECT_NEAR(output[4], 0.5F, 1.0e-6F);
}

TEST(DigitalDelayNodeFractional,
     CubicImpulseMatchesThirdOrderLagrangeKernel) {
    const auto output = RenderFractionalImpulse(3.5F, 1.0F, 0.0F, 8U);
    EXPECT_NEAR(output[2], -0.0625F, 1.0e-6F);
    EXPECT_NEAR(output[3], 0.5625F, 1.0e-6F);
    EXPECT_NEAR(output[4], 0.5625F, 1.0e-6F);
    EXPECT_NEAR(output[5], -0.0625F, 1.0e-6F);
}

TEST(DigitalDelayNodeFractional,
     CubicReducesMovingHeadSineErrorAgainstLinear) {
    const double linear_error = MovingSineRmsError(0.0F);
    const double cubic_error = MovingSineRmsError(1.0F);
    EXPECT_LT(cubic_error, linear_error * 0.30);
}

TEST(DigitalDelayNodeFractional,
     CubicRetainsMoreHighFrequencyEnergyAcrossFeedbackRepeats) {
    const auto linear = RenderFractionalImpulse(64.5F, 0.0F, 0.8F, 700U);
    const auto cubic = RenderFractionalImpulse(64.5F, 1.0F, 0.8F, 700U);

    const double linear_first =
        ToneProjection(linear, 64.5, 12U, 12000.0, 48000.0);
    const double linear_sixth =
        ToneProjection(linear, 6.0 * 64.5, 12U, 12000.0, 48000.0);
    const double cubic_first =
        ToneProjection(cubic, 64.5, 12U, 12000.0, 48000.0);
    const double cubic_sixth =
        ToneProjection(cubic, 6.0 * 64.5, 12U, 12000.0, 48000.0);

    ASSERT_GT(linear_first, 0.0);
    ASSERT_GT(cubic_first, 0.0);
    EXPECT_GT(cubic_sixth / cubic_first,
              3.0 * (linear_sixth / linear_first));
}

TEST(DigitalDelayNodeFractional,
     CubicBoundaryAndNonFiniteParametersRemainFinite) {
    std::vector<std::uint8_t> storage(1024U);
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(8U);
    ASSERT_TRUE(delay.Prepare(
        {48000.0F, 16U, phh::ChannelLayout::Stereo}, arena));

    auto parameters = DefaultFractionalParameters();
    parameters[DigitalDelayNode::DelaySamples] = 8.0F;
    parameters[DigitalDelayNode::InterpolationMode] = 1.0F;

    std::array<float, 16U> input_left{};
    std::array<float, 16U> input_right{};
    std::array<float, 16U> output_left{};
    std::array<float, 16U> output_right{};
    input_left[0] = 1.0F;
    phh::AudioBlock block{{input_left.data(), input_right.data()},
                          {output_left.data(), output_right.data()},
                          input_left.size()};
    phh::DiagnosticsCounters diagnostics{};
    delay.Process(block, FractionalSnapshot(parameters), diagnostics);
    EXPECT_FLOAT_EQ(output_left[8], 1.0F);

    parameters[DigitalDelayNode::DelaySamples] =
        std::numeric_limits<float>::quiet_NaN();
    parameters[DigitalDelayNode::InterpolationMode] =
        std::numeric_limits<float>::infinity();
    input_left.fill(0.0F);
    output_left.fill(0.0F);
    output_right.fill(0.0F);
    delay.Process(block, FractionalSnapshot(parameters), diagnostics);

    for(const float sample : output_left) {
        EXPECT_TRUE(std::isfinite(sample));
    }
}

} // namespace
