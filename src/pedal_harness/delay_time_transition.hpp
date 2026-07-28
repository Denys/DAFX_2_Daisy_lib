#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace phh {

enum class DelayTransitionPolicy : std::uint8_t {
    SlewPitch,
    DualHeadCrossfade,
    ImmediateGlitch,
};

enum class CrossfadeLaw : std::uint8_t {
    LinearAmplitude,
    EqualPower,
};

struct DelayTransitionConfig {
    float minimum_delay_samples = 1.0F;
    float maximum_delay_samples = 96000.0F;
    std::uint32_t default_slew_samples = 2400U;
    std::uint32_t default_crossfade_samples = 2400U;
    CrossfadeLaw crossfade_law = CrossfadeLaw::EqualPower;
};

struct DelayReadPlan {
    float delay_a_samples = 1.0F;
    float delay_b_samples = 1.0F;
    float gain_a = 1.0F;
    float gain_b = 0.0F;
    bool second_head_active = false;
    bool transition_active = false;
    bool transition_completed = false;
};

class DelayTimeTransition final {
  public:
    [[nodiscard]] bool Prepare(const DelayTransitionConfig& config) noexcept {
        if(!std::isfinite(config.minimum_delay_samples)
           || !std::isfinite(config.maximum_delay_samples)
           || config.minimum_delay_samples <= 0.0F
           || config.maximum_delay_samples < config.minimum_delay_samples
           || config.default_slew_samples == 0U
           || config.default_crossfade_samples == 0U) {
            return false;
        }

        config_ = config;
        prepared_ = true;
        Reset(config.minimum_delay_samples);
        return true;
    }

    void Reset(float initial_delay_samples) noexcept {
        if(!prepared_) {
            return;
        }

        current_delay_samples_ = ClampDelay(initial_delay_samples);
        source_delay_samples_ = current_delay_samples_;
        target_delay_samples_ = current_delay_samples_;
        duration_samples_ = 1U;
        step_index_ = 0U;
        state_ = State::Idle;
        queued_request_valid_ = false;
        completion_pending_ = false;
    }

    [[nodiscard]] bool Request(float target_delay_samples,
                               DelayTransitionPolicy policy,
                               std::uint32_t duration_samples = 0U) noexcept {
        if(!prepared_ || !std::isfinite(target_delay_samples)) {
            return false;
        }

        const float bounded_target = ClampDelay(target_delay_samples);

        if(policy == DelayTransitionPolicy::ImmediateGlitch) {
            current_delay_samples_ = bounded_target;
            source_delay_samples_ = bounded_target;
            target_delay_samples_ = bounded_target;
            state_ = State::Idle;
            step_index_ = 0U;
            duration_samples_ = 1U;
            queued_request_valid_ = false;
            completion_pending_ = true;
            return true;
        }

        const std::uint32_t resolved_duration =
            ResolveDuration(policy, duration_samples);

        if(state_ == State::Crossfading) {
            queued_request_ =
                RequestRecord{bounded_target, policy, resolved_duration};
            queued_request_valid_ = true;
            return true;
        }

        StartRequest(bounded_target, policy, resolved_duration);
        return true;
    }

    [[nodiscard]] DelayReadPlan Next() noexcept {
        DelayReadPlan plan{};

        if(!prepared_) {
            return plan;
        }

        BeginQueuedRequestIfIdle();

        if(state_ == State::Idle) {
            plan.delay_a_samples = current_delay_samples_;
            plan.delay_b_samples = current_delay_samples_;
            plan.transition_completed = completion_pending_;
            completion_pending_ = false;
            return plan;
        }

        if(state_ == State::Slewing) {
            return NextSlewPlan();
        }

        return NextCrossfadePlan();
    }

    [[nodiscard]] float CurrentDelaySamples() const noexcept {
        return current_delay_samples_;
    }

    [[nodiscard]] bool IsActive() const noexcept {
        return state_ != State::Idle;
    }

    [[nodiscard]] bool HasQueuedTarget() const noexcept {
        return queued_request_valid_;
    }

  private:
    enum class State : std::uint8_t {
        Idle,
        Slewing,
        Crossfading,
    };

    struct RequestRecord {
        float target_delay_samples = 1.0F;
        DelayTransitionPolicy policy = DelayTransitionPolicy::SlewPitch;
        std::uint32_t duration_samples = 1U;
    };

    static constexpr float kHalfPi = 1.57079632679489661923F;

    [[nodiscard]] float ClampDelay(float delay_samples) const noexcept {
        if(!std::isfinite(delay_samples)) {
            delay_samples = config_.minimum_delay_samples;
        }
        return std::clamp(delay_samples,
                          config_.minimum_delay_samples,
                          config_.maximum_delay_samples);
    }

    [[nodiscard]] std::uint32_t ResolveDuration(
        DelayTransitionPolicy policy,
        std::uint32_t requested_duration) const noexcept {
        if(requested_duration != 0U) {
            return requested_duration;
        }

        if(policy == DelayTransitionPolicy::DualHeadCrossfade) {
            return config_.default_crossfade_samples;
        }
        return config_.default_slew_samples;
    }

    void StartRequest(float target_delay_samples,
                      DelayTransitionPolicy policy,
                      std::uint32_t duration_samples) noexcept {
        source_delay_samples_ = current_delay_samples_;
        target_delay_samples_ = target_delay_samples;
        duration_samples_ = std::max<std::uint32_t>(duration_samples, 1U);
        step_index_ = 0U;
        completion_pending_ = false;

        state_ = (policy == DelayTransitionPolicy::DualHeadCrossfade)
                     ? State::Crossfading
                     : State::Slewing;
    }

    void BeginQueuedRequestIfIdle() noexcept {
        if(state_ != State::Idle || !queued_request_valid_) {
            return;
        }

        const RequestRecord queued = queued_request_;
        queued_request_valid_ = false;
        StartRequest(queued.target_delay_samples,
                     queued.policy,
                     queued.duration_samples);
    }

    [[nodiscard]] float Progress() const noexcept {
        if(duration_samples_ <= 1U) {
            return 1.0F;
        }

        return static_cast<float>(step_index_)
               / static_cast<float>(duration_samples_ - 1U);
    }

    [[nodiscard]] bool IsFinalStep() const noexcept {
        return step_index_ + 1U >= duration_samples_;
    }

    void AdvanceStep() noexcept {
        if(step_index_ + 1U < duration_samples_) {
            step_index_ += 1U;
        }
    }

    [[nodiscard]] DelayReadPlan NextSlewPlan() noexcept {
        const float progress = Progress();
        const float delay = source_delay_samples_
                            + progress
                                  * (target_delay_samples_
                                     - source_delay_samples_);

        DelayReadPlan plan{};
        plan.delay_a_samples = delay;
        plan.delay_b_samples = delay;
        plan.transition_active = true;

        current_delay_samples_ = delay;

        if(IsFinalStep()) {
            current_delay_samples_ = target_delay_samples_;
            plan.delay_a_samples = target_delay_samples_;
            plan.delay_b_samples = target_delay_samples_;
            plan.transition_completed = true;
            state_ = State::Idle;
            step_index_ = 0U;
        } else {
            AdvanceStep();
        }

        return plan;
    }

    [[nodiscard]] DelayReadPlan NextCrossfadePlan() noexcept {
        const float progress = Progress();

        DelayReadPlan plan{};
        plan.delay_a_samples = source_delay_samples_;
        plan.delay_b_samples = target_delay_samples_;
        plan.second_head_active = true;
        plan.transition_active = true;

        if(config_.crossfade_law == CrossfadeLaw::EqualPower) {
            plan.gain_a = std::cos(kHalfPi * progress);
            plan.gain_b = std::sin(kHalfPi * progress);
        } else {
            plan.gain_a = 1.0F - progress;
            plan.gain_b = progress;
        }

        if(IsFinalStep()) {
            current_delay_samples_ = target_delay_samples_;
            plan.gain_a = 0.0F;
            plan.gain_b = 1.0F;
            plan.transition_completed = true;
            state_ = State::Idle;
            step_index_ = 0U;
        } else {
            AdvanceStep();
        }

        return plan;
    }

    DelayTransitionConfig config_{};
    RequestRecord queued_request_{};
    float current_delay_samples_ = 1.0F;
    float source_delay_samples_ = 1.0F;
    float target_delay_samples_ = 1.0F;
    std::uint32_t duration_samples_ = 1U;
    std::uint32_t step_index_ = 0U;
    State state_ = State::Idle;
    bool prepared_ = false;
    bool queued_request_valid_ = false;
    bool completion_pending_ = false;
};

} // namespace phh
