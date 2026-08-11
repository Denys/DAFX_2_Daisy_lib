#include "pedal_harness/digital_delay_node.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>
#include <string>

namespace {

using phh::DigitalDelayNode;

constexpr float kSampleRateHz = 48000.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr std::size_t kMaxDelaySamples = 128U;

using Parameters = std::array<float, DigitalDelayNode::ParameterCount>;

Parameters DefaultParameters() {
    Parameters parameters{};
    parameters[DigitalDelayNode::DelaySamples] = 64.5F;
    parameters[DigitalDelayNode::Feedback] = 0.0F;
    parameters[DigitalDelayNode::InputSend] = 1.0F;
    parameters[DigitalDelayNode::DryMix] = 0.0F;
    parameters[DigitalDelayNode::WetMix] = 1.0F;
    parameters[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    parameters[DigitalDelayNode::FeedbackHighCutHz] = 23520.0F;
    parameters[DigitalDelayNode::InterpolationMode] = 0.0F;
    return parameters;
}

phh::ParameterSnapshot Snapshot(const Parameters& parameters,
                                std::uint32_t generation) {
    return {parameters.data(), parameters.size(), generation};
}

struct FeedbackMetrics {
    double peak_abs = 0.0;
    double early_energy = 0.0;
    double late_energy = 0.0;
    std::uint64_t non_finite_samples = 0U;
};

FeedbackMetrics CharacterizeFeedback(float interpolation_mode) {
    constexpr std::size_t kFrames = 32768U;
    constexpr std::size_t kWindow = 4096U;

    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        return {std::numeric_limits<double>::infinity(), 0.0, 1.0, 1U};
    }

    Parameters parameters = DefaultParameters();
    parameters[DigitalDelayNode::DelaySamples] = 64.5F;
    parameters[DigitalDelayNode::Feedback] = 0.95F;
    parameters[DigitalDelayNode::InterpolationMode] = interpolation_mode;

    phh::DiagnosticsCounters diagnostics{};
    FeedbackMetrics metrics{};

    for(std::size_t frame = 0U; frame < kFrames; ++frame) {
        float input_left = frame == 0U ? 1.0F : 0.0F;
        float input_right = 0.0F;
        float output_left = 0.0F;
        float output_right = 0.0F;
        phh::AudioBlock block{{&input_left, &input_right},
                              {&output_left, &output_right},
                              1U};
        delay.Process(block, Snapshot(parameters, 1U), diagnostics);

        const double sample = static_cast<double>(output_left);
        if(std::isfinite(sample)) {
            metrics.peak_abs = std::max(metrics.peak_abs, std::fabs(sample));
            if(frame < kWindow) {
                metrics.early_energy += sample * sample;
            }
            if(frame >= kFrames - kWindow) {
                metrics.late_energy += sample * sample;
            }
        }
    }

    metrics.non_finite_samples = diagnostics.non_finite_samples;
    return metrics;
}

struct JumpMetrics {
    double peak_abs = 0.0;
    double baseline_max_delta = 0.0;
    double transition_max_delta = 0.0;
    std::uint64_t non_finite_samples = 0U;
};

JumpMetrics CharacterizeDirectTimeJump(float interpolation_mode) {
    constexpr std::size_t kFrames = 4096U;
    constexpr std::size_t kJumpFrame = 2048U;
    constexpr std::size_t kTransitionHalfWindow = 16U;
    constexpr float kToneHz = 4000.0F;
    constexpr float kDelayBefore = 32.25F;
    constexpr float kDelayAfter = 38.25F;

    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        return {std::numeric_limits<double>::infinity(), 0.0, 0.0, 1U};
    }

    Parameters parameters = DefaultParameters();
    parameters[DigitalDelayNode::Feedback] = 0.0F;
    parameters[DigitalDelayNode::InterpolationMode] = interpolation_mode;

    phh::DiagnosticsCounters diagnostics{};
    JumpMetrics metrics{};
    double previous = 0.0;
    bool have_previous = false;

    for(std::size_t frame = 0U; frame < kFrames; ++frame) {
        parameters[DigitalDelayNode::DelaySamples] =
            frame < kJumpFrame ? kDelayBefore : kDelayAfter;

        float input_left =
            std::sin(2.0F * kPi * kToneHz * static_cast<float>(frame)
                     / kSampleRateHz);
        float input_right = 0.0F;
        float output_left = 0.0F;
        float output_right = 0.0F;
        phh::AudioBlock block{{&input_left, &input_right},
                              {&output_left, &output_right},
                              1U};
        delay.Process(block, Snapshot(parameters, 1U), diagnostics);

        const double sample = static_cast<double>(output_left);
        if(std::isfinite(sample)) {
            metrics.peak_abs = std::max(metrics.peak_abs, std::fabs(sample));
            if(have_previous) {
                const double delta = std::fabs(sample - previous);
                if(frame >= 1024U && frame < kJumpFrame - 64U) {
                    metrics.baseline_max_delta =
                        std::max(metrics.baseline_max_delta, delta);
                }
                if(frame >= kJumpFrame - kTransitionHalfWindow
                   && frame <= kJumpFrame + kTransitionHalfWindow) {
                    metrics.transition_max_delta =
                        std::max(metrics.transition_max_delta, delta);
                }
            }
            previous = sample;
            have_previous = true;
        }
    }

    metrics.non_finite_samples = diagnostics.non_finite_samples;
    return metrics;
}

const char* InterpolationName(float mode) {
    return mode < 0.5F ? "linear" : "cubic_lagrange";
}

bool WriteRows(std::ostream& output) {
    output << "scenario,interpolation,sample_rate_hz,frames,delay_start_samples,"
              "delay_end_samples,feedback,peak_abs,early_energy,late_energy,"
              "late_to_early_ratio,baseline_max_delta,transition_max_delta,"
              "transition_delta_ratio,non_finite_samples,verdict\n";
    output << std::setprecision(12);

    bool pass = true;
    for(const float interpolation_mode : {0.0F, 1.0F}) {
        const FeedbackMetrics feedback =
            CharacterizeFeedback(interpolation_mode);
        const double decay_ratio = feedback.early_energy > 0.0
                                       ? feedback.late_energy
                                             / feedback.early_energy
                                       : std::numeric_limits<double>::infinity();
        const bool feedback_pass = feedback.non_finite_samples == 0U
                                   && std::isfinite(feedback.peak_abs)
                                   && feedback.early_energy > 0.0
                                   && feedback.late_energy
                                          < feedback.early_energy;
        pass = pass && feedback_pass;

        output << "feedback_impulse," << InterpolationName(interpolation_mode)
               << ',' << kSampleRateHz << ",32768,64.5,64.5,0.95,"
               << feedback.peak_abs << ',' << feedback.early_energy << ','
               << feedback.late_energy << ',' << decay_ratio
               << ",0,0,0," << feedback.non_finite_samples << ','
               << (feedback_pass ? "PASS_BOUNDED" : "FAIL") << '\n';

        const JumpMetrics jump = CharacterizeDirectTimeJump(interpolation_mode);
        const double jump_ratio = jump.baseline_max_delta > 0.0
                                      ? jump.transition_max_delta
                                            / jump.baseline_max_delta
                                      : std::numeric_limits<double>::infinity();
        const bool jump_pass = jump.non_finite_samples == 0U
                               && std::isfinite(jump.peak_abs)
                               && jump.baseline_max_delta > 0.0
                               && std::isfinite(jump_ratio);
        pass = pass && jump_pass;

        output << "direct_time_jump," << InterpolationName(interpolation_mode)
               << ',' << kSampleRateHz << ",4096,32.25,38.25,0,"
               << jump.peak_abs << ",0,0,0," << jump.baseline_max_delta << ','
               << jump.transition_max_delta << ',' << jump_ratio << ','
               << jump.non_finite_samples << ','
               << (jump_pass ? "CHARACTERIZED_HOLD_POLICY" : "FAIL") << '\n';
    }
    return pass;
}

} // namespace

int main(int argc, char** argv) {
    if(argc > 2) {
        std::cerr << "usage: delay_host_characterization [output.csv]\n";
        return 2;
    }

    if(argc == 2) {
        std::ofstream file(argv[1], std::ios::out | std::ios::trunc);
        if(!file) {
            std::cerr << "failed to open output path\n";
            return 2;
        }
        return WriteRows(file) ? 0 : 1;
    }

    return WriteRows(std::cout) ? 0 : 1;
}
