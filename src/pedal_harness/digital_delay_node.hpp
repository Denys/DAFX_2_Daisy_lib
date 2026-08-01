#pragma once

#include "pedal_harness/dsp_contract.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace phh {

class DigitalDelayNode final : public DspNode {
  public:
    enum Parameter : std::size_t {
        DelaySamples = 0,
        Feedback,
        InputSend,
        DryMix,
        WetMix,
        FeedbackLowCutHz,
        FeedbackHighCutHz,
        InterpolationMode,
        ParameterCount,
    };

    enum class InterpolationPolicy : std::uint8_t {
        Linear = 0,
        CubicLagrange = 1,
    };

    explicit DigitalDelayNode(std::size_t max_delay_samples) noexcept
        : max_delay_samples_(max_delay_samples) {}

    [[nodiscard]] NodeDescriptor Describe() const noexcept override {
        NodeDescriptor descriptor{};
        descriptor.stable_id = "phh.delay.digi.v0";
        descriptor.schema_version = 2;
        descriptor.supported_layout = ChannelLayout::Stereo;
        descriptor.default_tail_policy = TailPolicy::Preserve;
        descriptor.resources.persistent_bytes =
            2U * (max_delay_samples_ + kInterpolationGuardSamples)
            * sizeof(float);
        descriptor.resources.requires_external_memory =
            descriptor.resources.persistent_bytes > 64U * 1024U;
        return descriptor;
    }

    [[nodiscard]] bool Prepare(const PrepareSpec& spec,
                               StaticArena& arena) noexcept override {
        prepared_ = false;

        if(spec.sample_rate_hz <= 0.0F || spec.max_block_frames == 0U
           || spec.layout != ChannelLayout::Stereo
           || max_delay_samples_ < 1U) {
            return false;
        }

        sample_rate_hz_ = spec.sample_rate_hz;
        capacity_ = max_delay_samples_ + kInterpolationGuardSamples;
        buffers_[0] = arena.AllocateArray<float>(capacity_);
        buffers_[1] = arena.AllocateArray<float>(capacity_);

        if(buffers_[0] == nullptr || buffers_[1] == nullptr) {
            buffers_[0] = nullptr;
            buffers_[1] = nullptr;
            capacity_ = 0U;
            return false;
        }

        prepared_ = true;
        ClearState();
        return true;
    }

    void Reset(ResetReason) noexcept override {
        if(prepared_) {
            ClearState();
        }
    }

    void Process(const AudioBlock& block,
                 const ParameterSnapshot& parameters,
                 DiagnosticsCounters& diagnostics) noexcept override {
        if(!prepared_ || block.frames == 0U || block.in[0] == nullptr
           || block.in[1] == nullptr || block.out[0] == nullptr
           || block.out[1] == nullptr) {
            return;
        }

        const ReadHead read_head = ResolveReadHead(parameters);
        const float feedback = ClampFinite(
            parameters.GetOr(Feedback, kDefaultFeedback),
            kDefaultFeedback,
            -kFeedbackLimit,
            kFeedbackLimit);
        const float input_send = ClampFinite(
            parameters.GetOr(InputSend, kDefaultInputSend),
            kDefaultInputSend,
            0.0F,
            1.0F);
        const float dry_mix = ClampFinite(parameters.GetOr(DryMix, kDefaultDryMix),
                                          kDefaultDryMix,
                                          0.0F,
                                          2.0F);
        const float wet_mix = ClampFinite(parameters.GetOr(WetMix, kDefaultWetMix),
                                          kDefaultWetMix,
                                          0.0F,
                                          2.0F);

        UpdateFeedbackFilters(parameters);

        for(std::size_t frame = 0; frame < block.frames; ++frame) {
            for(std::size_t channel = 0; channel < 2U; ++channel) {
                float input = block.in[channel][frame];
                if(!std::isfinite(input)) {
                    input = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }

                float delayed = ReadDelayed(channel, read_head);
                if(!std::isfinite(delayed)) {
                    delayed = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }

                const float conditioned =
                    ProcessFeedbackFilters(channel, delayed);
                float write_sample = input_send * input + feedback * conditioned;
                if(!std::isfinite(write_sample)) {
                    write_sample = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }
                buffers_[channel][write_index_] = write_sample;

                float output = dry_mix * input + wet_mix * delayed;
                if(!std::isfinite(output)) {
                    output = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }
                block.out[channel][frame] = output;
            }

            write_index_ += 1U;
            if(write_index_ == capacity_) {
                write_index_ = 0U;
            }
        }
    }

    [[nodiscard]] std::size_t MaxDelaySamples() const noexcept {
        return max_delay_samples_;
    }

  private:
    struct ReadHead {
        std::size_t base_delay = 1U;
        float fraction = 0.0F;
        float one_minus_fraction = 1.0F;
        float cubic_minus_one = 0.0F;
        float cubic_zero = 1.0F;
        float cubic_plus_one = 0.0F;
        float cubic_plus_two = 0.0F;
        bool cubic = false;
    };

    static constexpr std::size_t kInterpolationGuardSamples = 3U;
    static constexpr float kPi = 3.14159265358979323846F;
    static constexpr float kFeedbackLimit = 0.999F;
    static constexpr float kDefaultFeedback = 0.0F;
    static constexpr float kDefaultInputSend = 1.0F;
    static constexpr float kDefaultDryMix = 1.0F;
    static constexpr float kDefaultWetMix = 0.0F;
    static constexpr float kDefaultLowCutHz = 0.0F;

    [[nodiscard]] ReadHead ResolveReadHead(
        const ParameterSnapshot& parameters) const noexcept {
        float requested = parameters.GetOr(DelaySamples, 1.0F);
        if(!std::isfinite(requested)) {
            requested = 1.0F;
        }
        requested = std::clamp(requested,
                               1.0F,
                               static_cast<float>(max_delay_samples_));

        ReadHead head{};
        head.base_delay = static_cast<std::size_t>(requested);
        head.fraction = requested - static_cast<float>(head.base_delay);
        head.one_minus_fraction = 1.0F - head.fraction;

        const float policy_value = ClampFinite(
            parameters.GetOr(InterpolationMode, 0.0F),
            0.0F,
            0.0F,
            1.0F);
        head.cubic = policy_value >= 0.5F && head.base_delay >= 2U
                     && head.fraction > 0.0F;
        if(head.cubic) {
            const float mu = head.fraction;
            head.cubic_minus_one =
                -mu * (mu - 1.0F) * (mu - 2.0F) / 6.0F;
            head.cubic_zero =
                (mu + 1.0F) * (mu - 1.0F) * (mu - 2.0F) / 2.0F;
            head.cubic_plus_one =
                -(mu + 1.0F) * mu * (mu - 2.0F) / 2.0F;
            head.cubic_plus_two =
                (mu + 1.0F) * mu * (mu - 1.0F) / 6.0F;
        }
        return head;
    }

    [[nodiscard]] std::size_t IndexAtDelay(std::size_t delay) const noexcept {
        return (write_index_ + capacity_ - delay) % capacity_;
    }

    [[nodiscard]] float ReadDelayed(std::size_t channel,
                                    const ReadHead& head) const noexcept {
        const float zero =
            buffers_[channel][IndexAtDelay(head.base_delay)];
        if(head.fraction <= 0.0F) {
            return zero;
        }

        const float plus_one =
            buffers_[channel][IndexAtDelay(head.base_delay + 1U)];
        if(!head.cubic) {
            return head.one_minus_fraction * zero
                   + head.fraction * plus_one;
        }

        const float minus_one =
            buffers_[channel][IndexAtDelay(head.base_delay - 1U)];
        const float plus_two =
            buffers_[channel][IndexAtDelay(head.base_delay + 2U)];
        return head.cubic_minus_one * minus_one
               + head.cubic_zero * zero
               + head.cubic_plus_one * plus_one
               + head.cubic_plus_two * plus_two;
    }

    static float ClampFinite(float value,
                             float fallback,
                             float minimum,
                             float maximum) noexcept {
        if(!std::isfinite(value)) {
            value = fallback;
        }
        return std::clamp(value, minimum, maximum);
    }

    void UpdateFeedbackFilters(const ParameterSnapshot& parameters) noexcept {
        const float nyquist_guard = sample_rate_hz_ * 0.49F;
        const float low_cut_hz = ClampFinite(
            parameters.GetOr(FeedbackLowCutHz, kDefaultLowCutHz),
            kDefaultLowCutHz,
            0.0F,
            nyquist_guard);
        const float high_cut_hz = ClampFinite(
            parameters.GetOr(FeedbackHighCutHz, nyquist_guard),
            nyquist_guard,
            0.0F,
            nyquist_guard);

        high_pass_enabled_ = low_cut_hz > 0.0F;
        low_pass_enabled_ = high_cut_hz < nyquist_guard;

        if(high_pass_enabled_) {
            high_pass_pole_ =
                std::exp(-2.0F * kPi * low_cut_hz / sample_rate_hz_);
        }
        if(low_pass_enabled_) {
            low_pass_alpha_ =
                1.0F
                - std::exp(-2.0F * kPi * high_cut_hz / sample_rate_hz_);
        }
    }

    float ProcessFeedbackFilters(std::size_t channel, float input) noexcept {
        float filtered = input;

        if(high_pass_enabled_) {
            const float output =
                high_pass_pole_
                * (high_pass_output_[channel] + filtered
                   - high_pass_input_[channel]);
            high_pass_input_[channel] = filtered;
            high_pass_output_[channel] = output;
            filtered = output;
        }

        if(low_pass_enabled_) {
            low_pass_output_[channel] +=
                low_pass_alpha_ * (filtered - low_pass_output_[channel]);
            filtered = low_pass_output_[channel];
        }

        return filtered;
    }

    void ClearState() noexcept {
        for(std::size_t channel = 0; channel < 2U; ++channel) {
            for(std::size_t index = 0; index < capacity_; ++index) {
                buffers_[channel][index] = 0.0F;
            }
            high_pass_input_[channel] = 0.0F;
            high_pass_output_[channel] = 0.0F;
            low_pass_output_[channel] = 0.0F;
        }
        write_index_ = 0U;
    }

    std::size_t max_delay_samples_ = 0U;
    std::size_t capacity_ = 0U;
    std::size_t write_index_ = 0U;
    float sample_rate_hz_ = 0.0F;
    float* buffers_[2] = {nullptr, nullptr};
    float high_pass_input_[2] = {0.0F, 0.0F};
    float high_pass_output_[2] = {0.0F, 0.0F};
    float low_pass_output_[2] = {0.0F, 0.0F};
    float high_pass_pole_ = 0.0F;
    float low_pass_alpha_ = 1.0F;
    bool high_pass_enabled_ = false;
    bool low_pass_enabled_ = false;
    bool prepared_ = false;
};

} // namespace phh
