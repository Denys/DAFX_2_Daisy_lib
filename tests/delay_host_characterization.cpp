#include "pedal_harness/digital_delay_node.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <ostream>

namespace {

using phh::DigitalDelayNode;
using Parameters = std::array<float, DigitalDelayNode::ParameterCount>;

constexpr float kSampleRateHz = 48000.0F;
constexpr float kPi = 3.14159265358979323846F;
constexpr std::size_t kMaxDelaySamples = 128U;

// Frozen before verdict capture. Host-only candidate gates, not product specs.
constexpr double kCrossfadeMaxDeltaRatioGate = 1.10;
constexpr double kCrossfadeDeltaRmsRatioGate = 1.10;
constexpr double kTransientPeakGate = 1.01;
constexpr double kTransientEnergyGate = 1.05;
constexpr double kPostCrossfadeTargetErrorGate = 1.0e-6;

Parameters DefaultParameters() {
    Parameters p{};
    p[DigitalDelayNode::DelaySamples] = 64.5F;
    p[DigitalDelayNode::Feedback] = 0.0F;
    p[DigitalDelayNode::InputSend] = 1.0F;
    p[DigitalDelayNode::DryMix] = 0.0F;
    p[DigitalDelayNode::WetMix] = 1.0F;
    p[DigitalDelayNode::FeedbackLowCutHz] = 0.0F;
    p[DigitalDelayNode::FeedbackHighCutHz] = 23520.0F;
    p[DigitalDelayNode::InterpolationMode] = 0.0F;
    p[DigitalDelayNode::TimeTransitionMode] = 0.0F;
    return p;
}

phh::ParameterSnapshot Snapshot(const Parameters& p, std::uint32_t gen) {
    return {p.data(), p.size(), gen};
}

struct FeedbackMetrics {
    double peak_abs = 0.0;
    double early_energy = 0.0;
    double late_energy = 0.0;
    std::uint64_t non_finite = 0U;
};

FeedbackMetrics FeedbackCell(float interp) {
    constexpr std::size_t kFrames = 32768U;
    constexpr std::size_t kWindow = 4096U;
    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        return {std::numeric_limits<double>::infinity(), 0.0, 1.0, 1U};
    }

    Parameters p = DefaultParameters();
    p[DigitalDelayNode::Feedback] = 0.95F;
    p[DigitalDelayNode::InterpolationMode] = interp;
    phh::DiagnosticsCounters d{};
    FeedbackMetrics m{};

    for(std::size_t i = 0; i < kFrames; ++i) {
        float in_l = i == 0U ? 1.0F : 0.0F;
        float in_r = 0.0F;
        float out_l = 0.0F;
        float out_r = 0.0F;
        phh::AudioBlock block{{&in_l, &in_r}, {&out_l, &out_r}, 1U};
        delay.Process(block, Snapshot(p, 1U), d);
        const double s = static_cast<double>(out_l);
        if(std::isfinite(s)) {
            m.peak_abs = std::max(m.peak_abs, std::fabs(s));
            if(i < kWindow) m.early_energy += s * s;
            if(i >= kFrames - kWindow) m.late_energy += s * s;
        }
    }
    m.non_finite = d.non_finite_samples;
    return m;
}

struct TransitionMetrics {
    double peak_abs = 0.0;
    double baseline_max_delta = 0.0;
    double transition_max_delta = 0.0;
    double baseline_delta_energy = 0.0;
    double transition_delta_energy = 0.0;
    std::size_t baseline_count = 0U;
    std::size_t transition_count = 0U;
    std::uint64_t non_finite = 0U;
};

TransitionMetrics TimeJumpCell(float interp, float transition_mode) {
    constexpr std::size_t kFrames = 4096U;
    constexpr std::size_t kJump = 2048U;
    constexpr std::size_t kHalfWindow = 96U;
    constexpr float kToneHz = 4000.0F;

    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        TransitionMetrics failed{};
        failed.peak_abs = std::numeric_limits<double>::infinity();
        failed.non_finite = 1U;
        return failed;
    }

    Parameters p = DefaultParameters();
    p[DigitalDelayNode::InterpolationMode] = interp;
    p[DigitalDelayNode::TimeTransitionMode] = transition_mode;
    phh::DiagnosticsCounters d{};
    TransitionMetrics m{};
    double previous = 0.0;
    bool have_previous = false;

    for(std::size_t i = 0; i < kFrames; ++i) {
        p[DigitalDelayNode::DelaySamples] = i < kJump ? 32.25F : 38.25F;
        float in_l = std::sin(2.0F * kPi * kToneHz
                              * static_cast<float>(i) / kSampleRateHz);
        float in_r = 0.0F;
        float out_l = 0.0F;
        float out_r = 0.0F;
        phh::AudioBlock block{{&in_l, &in_r}, {&out_l, &out_r}, 1U};
        delay.Process(block, Snapshot(p, 1U), d);

        const double s = static_cast<double>(out_l);
        if(std::isfinite(s)) {
            m.peak_abs = std::max(m.peak_abs, std::fabs(s));
            if(have_previous) {
                const double delta = std::fabs(s - previous);
                const double e = delta * delta;
                if(i >= 1024U && i < kJump - 128U) {
                    m.baseline_max_delta = std::max(m.baseline_max_delta, delta);
                    m.baseline_delta_energy += e;
                    ++m.baseline_count;
                }
                if(i >= kJump - kHalfWindow && i <= kJump + kHalfWindow) {
                    m.transition_max_delta =
                        std::max(m.transition_max_delta, delta);
                    m.transition_delta_energy += e;
                    ++m.transition_count;
                }
            }
            previous = s;
            have_previous = true;
        }
    }
    m.non_finite = d.non_finite_samples;
    return m;
}

struct TransientMetrics {
    double peak_abs = 0.0;
    double energy = 0.0;
    std::uint64_t non_finite = 0U;
};

TransientMetrics TransientCell(float interp, float transition_mode) {
    constexpr std::size_t kFrames = 512U;
    constexpr std::size_t kJump = 256U;
    constexpr std::size_t kImpulse = kJump - 32U;
    constexpr std::size_t kStart = kJump - 8U;
    constexpr std::size_t kEnd = kJump + 96U;

    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        return {std::numeric_limits<double>::infinity(), 0.0, 1U};
    }

    Parameters p = DefaultParameters();
    p[DigitalDelayNode::InterpolationMode] = interp;
    p[DigitalDelayNode::TimeTransitionMode] = transition_mode;
    phh::DiagnosticsCounters d{};
    TransientMetrics m{};

    for(std::size_t i = 0; i < kFrames; ++i) {
        p[DigitalDelayNode::DelaySamples] = i < kJump ? 32.25F : 38.25F;
        float in_l = i == kImpulse ? 1.0F : 0.0F;
        float in_r = 0.0F;
        float out_l = 0.0F;
        float out_r = 0.0F;
        phh::AudioBlock block{{&in_l, &in_r}, {&out_l, &out_r}, 1U};
        delay.Process(block, Snapshot(p, 1U), d);
        const double s = static_cast<double>(out_l);
        if(std::isfinite(s) && i >= kStart && i <= kEnd) {
            m.peak_abs = std::max(m.peak_abs, std::fabs(s));
            m.energy += s * s;
        }
    }
    m.non_finite = d.non_finite_samples;
    return m;
}

struct BlockCompletionMetrics {
    double target_max_error = 0.0;
    std::uint64_t non_finite = 0U;
};

BlockCompletionMetrics BlockCompletionCell(float interp) {
    constexpr std::size_t kWarmup = 512U;
    constexpr std::size_t kBlock = 96U;
    std::array<std::uint8_t, 4096U> candidate_storage{};
    std::array<std::uint8_t, 4096U> reference_storage{};
    phh::StaticArena candidate_arena(candidate_storage.data(),
                                     candidate_storage.size());
    phh::StaticArena reference_arena(reference_storage.data(),
                                     reference_storage.size());
    DigitalDelayNode candidate(kMaxDelaySamples);
    DigitalDelayNode reference(kMaxDelaySamples);
    if(!candidate.Prepare({kSampleRateHz, kBlock, phh::ChannelLayout::Stereo},
                          candidate_arena)
       || !reference.Prepare({kSampleRateHz, kBlock, phh::ChannelLayout::Stereo},
                             reference_arena)) {
        return {std::numeric_limits<double>::infinity(), 1U};
    }

    Parameters cp = DefaultParameters();
    Parameters rp = DefaultParameters();
    cp[DigitalDelayNode::DelaySamples] = 32.25F;
    cp[DigitalDelayNode::InterpolationMode] = interp;
    cp[DigitalDelayNode::TimeTransitionMode] = 1.0F;
    rp[DigitalDelayNode::DelaySamples] = 38.25F;
    rp[DigitalDelayNode::InterpolationMode] = interp;
    phh::DiagnosticsCounters cd{};
    phh::DiagnosticsCounters rd{};

    for(std::size_t i = 0; i < kWarmup; ++i) {
        float in_l = std::sin(2.0F * kPi * 997.0F
                              * static_cast<float>(i) / kSampleRateHz);
        float in_r = in_l * 0.5F;
        float cl = 0.0F, cr = 0.0F, rl = 0.0F, rr = 0.0F;
        phh::AudioBlock cb{{&in_l, &in_r}, {&cl, &cr}, 1U};
        phh::AudioBlock rb{{&in_l, &in_r}, {&rl, &rr}, 1U};
        candidate.Process(cb, Snapshot(cp, 1U), cd);
        reference.Process(rb, Snapshot(rp, 1U), rd);
    }

    cp[DigitalDelayNode::DelaySamples] = 38.25F;
    std::array<float, kBlock> in_l{}, in_r{}, cl{}, cr{}, rl{}, rr{};
    for(std::size_t i = 0; i < kBlock; ++i) {
        const float n = static_cast<float>(kWarmup + i);
        in_l[i] = std::sin(2.0F * kPi * 997.0F * n / kSampleRateHz);
        in_r[i] = in_l[i] * 0.5F;
    }
    phh::AudioBlock cb{{in_l.data(), in_r.data()}, {cl.data(), cr.data()}, kBlock};
    phh::AudioBlock rb{{in_l.data(), in_r.data()}, {rl.data(), rr.data()}, kBlock};
    candidate.Process(cb, Snapshot(cp, 2U), cd);
    reference.Process(rb, Snapshot(rp, 2U), rd);

    BlockCompletionMetrics m{};
    for(std::size_t i = DigitalDelayNode::kDualReadCrossfadeFrames;
        i < kBlock;
        ++i) {
        m.target_max_error =
            std::max(m.target_max_error,
                     std::fabs(static_cast<double>(cl[i])
                               - static_cast<double>(rl[i])));
    }
    m.non_finite = cd.non_finite_samples + rd.non_finite_samples;
    return m;
}

struct HostCostMetrics {
    double ns_per_frame = 0.0;
    double checksum = 0.0;
};

HostCostMetrics CostCell(float interp, float transition_mode) {
    constexpr std::size_t kFrames = 262144U;
    constexpr std::size_t kTogglePeriod = 64U;
    std::array<std::uint8_t, 4096U> storage{};
    phh::StaticArena arena(storage.data(), storage.size());
    DigitalDelayNode delay(kMaxDelaySamples);
    if(!delay.Prepare({kSampleRateHz, 1U, phh::ChannelLayout::Stereo}, arena)) {
        return {std::numeric_limits<double>::infinity(), 0.0};
    }

    Parameters p = DefaultParameters();
    p[DigitalDelayNode::InterpolationMode] = interp;
    p[DigitalDelayNode::TimeTransitionMode] = transition_mode;
    p[DigitalDelayNode::Feedback] = 0.35F;
    phh::DiagnosticsCounters d{};
    double checksum = 0.0;
    const auto start = std::chrono::steady_clock::now();

    for(std::size_t i = 0; i < kFrames; ++i) {
        p[DigitalDelayNode::DelaySamples] =
            ((i / kTogglePeriod) & 1U) == 0U ? 32.25F : 38.25F;
        float in_l = std::sin(2.0F * kPi * 997.0F
                              * static_cast<float>(i) / kSampleRateHz);
        float in_r = in_l * 0.5F;
        float out_l = 0.0F, out_r = 0.0F;
        phh::AudioBlock block{{&in_l, &in_r}, {&out_l, &out_r}, 1U};
        delay.Process(block, Snapshot(p, 1U), d);
        checksum += static_cast<double>(out_l) + static_cast<double>(out_r);
    }

    const double elapsed_ns =
        std::chrono::duration<double, std::nano>(
            std::chrono::steady_clock::now() - start)
            .count();
    return {elapsed_ns / static_cast<double>(kFrames), checksum};
}

double DeltaRms(const TransitionMetrics& m, bool transition) {
    const double e = transition ? m.transition_delta_energy
                                : m.baseline_delta_energy;
    const std::size_t n = transition ? m.transition_count : m.baseline_count;
    return n > 0U ? std::sqrt(e / static_cast<double>(n))
                  : std::numeric_limits<double>::infinity();
}

const char* InterpName(float mode) {
    return mode < 0.5F ? "linear" : "cubic_lagrange";
}

bool WriteRows(std::ostream& out) {
    out << "scenario,transition_policy,interpolation,sample_rate_hz,frames,"
           "delay_start_samples,delay_end_samples,feedback,peak_abs,"
           "early_energy,late_energy,late_to_early_ratio,baseline_max_delta,"
           "transition_max_delta,transition_delta_ratio,baseline_delta_rms,"
           "transition_delta_rms,transition_rms_ratio,transient_peak_abs,"
           "transient_energy,post_crossfade_target_max_error,host_ns_per_frame,"
           "host_cost_ratio,non_finite_samples,verdict\n";
    out << std::setprecision(12);

    bool pass = true;
    for(const float interp : {0.0F, 1.0F}) {
        const auto feedback = FeedbackCell(interp);
        const double decay_ratio =
            feedback.early_energy > 0.0
                ? feedback.late_energy / feedback.early_energy
                : std::numeric_limits<double>::infinity();
        const bool feedback_pass =
            feedback.non_finite == 0U && std::isfinite(feedback.peak_abs)
            && feedback.early_energy > 0.0
            && feedback.late_energy < feedback.early_energy;
        pass = pass && feedback_pass;

        out << "feedback_impulse,direct," << InterpName(interp)
            << ',' << kSampleRateHz << ",32768,64.5,64.5,0.95,"
            << feedback.peak_abs << ',' << feedback.early_energy << ','
            << feedback.late_energy << ',' << decay_ratio
            << ",0,0,0,0,0,0,0,0,0,0,0," << feedback.non_finite << ','
            << (feedback_pass ? "PASS_BOUNDED" : "FAIL") << '\n';

        const auto direct = TimeJumpCell(interp, 0.0F);
        const auto cross = TimeJumpCell(interp, 1.0F);
        const double direct_base_rms = DeltaRms(direct, false);
        const double direct_transition_rms = DeltaRms(direct, true);
        const double cross_base_rms = DeltaRms(cross, false);
        const double cross_transition_rms = DeltaRms(cross, true);
        const double direct_delta_ratio =
            direct.baseline_max_delta > 0.0
                ? direct.transition_max_delta / direct.baseline_max_delta
                : std::numeric_limits<double>::infinity();
        const double direct_rms_ratio =
            direct_base_rms > 0.0
                ? direct_transition_rms / direct_base_rms
                : std::numeric_limits<double>::infinity();
        const double cross_delta_ratio =
            cross.baseline_max_delta > 0.0
                ? cross.transition_max_delta / cross.baseline_max_delta
                : std::numeric_limits<double>::infinity();
        const double cross_rms_ratio =
            cross_base_rms > 0.0
                ? cross_transition_rms / cross_base_rms
                : std::numeric_limits<double>::infinity();
        const bool direct_ok = direct.non_finite == 0U
                               && std::isfinite(direct_delta_ratio)
                               && std::isfinite(direct_rms_ratio);
        pass = pass && direct_ok;

        out << "time_jump,direct," << InterpName(interp) << ','
            << kSampleRateHz << ",4096,32.25,38.25,0," << direct.peak_abs
            << ",0,0,0," << direct.baseline_max_delta << ','
            << direct.transition_max_delta << ',' << direct_delta_ratio << ','
            << direct_base_rms << ',' << direct_transition_rms << ','
            << direct_rms_ratio << ",0,0,0,0,0," << direct.non_finite << ','
            << (direct_ok ? "CHARACTERIZED_BASELINE" : "FAIL") << '\n';

        const auto direct_transient = TransientCell(interp, 0.0F);
        const auto cross_transient = TransientCell(interp, 1.0F);
        const auto block = BlockCompletionCell(interp);
        const auto direct_cost = CostCell(interp, 0.0F);
        const auto cross_cost = CostCell(interp, 1.0F);
        const double cost_ratio =
            direct_cost.ns_per_frame > 0.0
                ? cross_cost.ns_per_frame / direct_cost.ns_per_frame
                : std::numeric_limits<double>::infinity();

        const bool cross_pass =
            cross.non_finite == 0U && cross_transient.non_finite == 0U
            && block.non_finite == 0U && std::isfinite(cross_delta_ratio)
            && std::isfinite(cross_rms_ratio) && std::isfinite(cost_ratio)
            && cross_delta_ratio <= kCrossfadeMaxDeltaRatioGate
            && cross_rms_ratio <= kCrossfadeDeltaRmsRatioGate
            && cross_transient.peak_abs <= kTransientPeakGate
            && cross_transient.energy <= kTransientEnergyGate
            && block.target_max_error <= kPostCrossfadeTargetErrorGate;
        pass = pass && cross_pass;

        out << "time_jump,dual_read_crossfade_64," << InterpName(interp)
            << ',' << kSampleRateHz << ",4096,32.25,38.25,0,"
            << cross.peak_abs << ",0,0,0," << cross.baseline_max_delta << ','
            << cross.transition_max_delta << ',' << cross_delta_ratio << ','
            << cross_base_rms << ',' << cross_transition_rms << ','
            << cross_rms_ratio << ',' << cross_transient.peak_abs << ','
            << cross_transient.energy << ',' << block.target_max_error << ','
            << cross_cost.ns_per_frame << ',' << cost_ratio << ','
            << (cross.non_finite + cross_transient.non_finite + block.non_finite)
            << ',' << (cross_pass
                            ? "ACCEPT_HOST_CANDIDATE_HOLD_PRODUCT_POLICY"
                            : "REJECT_HOST_CANDIDATE")
            << '\n';

        out << "transition_transient,direct," << InterpName(interp) << ','
            << kSampleRateHz << ",512,32.25,38.25,0,"
            << direct_transient.peak_abs
            << ",0,0,0,0,0,0,0,0,0," << direct_transient.peak_abs << ','
            << direct_transient.energy << ",0," << direct_cost.ns_per_frame
            << ",1," << direct_transient.non_finite
            << ",CHARACTERIZED_BASELINE\n";
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
