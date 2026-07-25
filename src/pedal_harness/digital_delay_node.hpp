#pragma once

#include "pedal_harness/dsp_contract.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>

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
        ParameterCount,
    };

    explicit DigitalDelayNode(std::size_t max_delay_samples) noexcept
        : max_delay_samples_(max_delay_samples) {}

    [[nodiscard]] NodeDescriptor Describe() const noexcept override {
        NodeDescriptor descriptor{};
        descriptor.stable_id = "phh.delay.digi.v0";
        descriptor.schema_version = 1;
        descriptor.supported_layout = ChannelLayout::Stereo;
        descriptor.default_tail_policy = TailPolicy::Preserve;
        descriptor.resources.persistent_bytes =
            2U * (max_delay_samples_ + 1U) * sizeof(float);
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
        capacity_ = max_delay_samples_ + 1U;
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

        const auto delay_samples = ResolveDelaySamples(parameters);
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
            const std::size_t read_index =
                (write_index_ + capacity_ - delay_samples) % capacity_;

            for(std::size_t channel = 0; channel < 2U; ++channel) {
                float input = block.in[channel][frame];
                if(!std::isfinite(input)) {
                    input = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }

                float delayed = buffers_[channel][read_index];
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
    static constexpr float kPi = 3.14159265358979323846F;
    static constexpr float kFeedbackLimit = 0.999F;
    static constexpr float kDefaultFeedback = 0.0F;
    static constexpr float kDefaultInputSend = 1.0F;
    static constexpr float kDefaultDryMix = 1.0F;
    static constexpr float kDefaultWetMix = 0.0F;
    static constexpr float kDefaultLowCutHz = 0.0F;

    [[nodiscard]] std::size_t ResolveDelaySamples(
        const ParameterSnapshot& parameters) const noexcept {
        float requested = parameters.GetOr(DelaySamples, 1.0F);
        if(!std::isfinite(requested)) {
            requested = 1.0F;
        }
        requested = std::clamp(requested,
                               1.0F,
                               static_cast<float>(max_delay_samples_));
        return static_cast<std::size_t>(requested + 0.5F);
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
