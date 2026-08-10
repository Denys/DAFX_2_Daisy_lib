#pragma once

#include "pedal_harness/digital_delay_node.hpp"
#include "pedal_harness/static_serial_graph.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace phh {

inline float ClampNormalized(float value, float fallback = 0.0F) noexcept {
    if(!std::isfinite(value)) {
        value = fallback;
    }
    if(value < 0.0F) {
        return 0.0F;
    }
    if(value > 1.0F) {
        return 1.0F;
    }
    return value;
}

template <std::size_t Count>
class RealtimeFloatSnapshotMailbox final {
  public:
    using ValueArray = std::array<float, Count>;

    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "32-bit atomics must be lock-free in the audio path");

    void Publish(const ValueArray& values) noexcept {
        sequence_.fetch_add(1U, std::memory_order_acq_rel); // odd: writer active
        for(std::size_t i = 0; i < Count; ++i) {
            bits_[i].store(FloatToBits(values[i]), std::memory_order_relaxed);
        }
        sequence_.fetch_add(1U, std::memory_order_release); // even: published
    }

    [[nodiscard]] bool ReadBounded(ValueArray& values,
                                   std::uint32_t& generation) const noexcept {
        // A control publication may pre-empt the callback. Make two bounded
        // attempts and otherwise retain the last complete block snapshot.
        for(std::size_t attempt = 0U; attempt < 2U; ++attempt) {
            const std::uint32_t before =
                sequence_.load(std::memory_order_acquire);
            if((before & 1U) != 0U) {
                continue;
            }

            ValueArray candidate{};
            for(std::size_t i = 0; i < Count; ++i) {
                candidate[i] = BitsToFloat(
                    bits_[i].load(std::memory_order_relaxed));
            }

            const std::uint32_t after =
                sequence_.load(std::memory_order_acquire);
            if(before == after && (after & 1U) == 0U) {
                values = candidate;
                generation = after / 2U;
                return true;
            }
        }
        return false;
    }

  private:
    static std::uint32_t FloatToBits(float value) noexcept {
        std::uint32_t bits = 0U;
        std::memcpy(&bits, &value, sizeof(bits));
        return bits;
    }

    static float BitsToFloat(std::uint32_t bits) noexcept {
        float value = 0.0F;
        std::memcpy(&value, &bits, sizeof(value));
        return value;
    }

    std::atomic<std::uint32_t> sequence_{0U};
    std::array<std::atomic<std::uint32_t>, Count> bits_{};
};

class DelayFirmwareRuntime final {
  public:
    using ParameterArray =
        std::array<float, DigitalDelayNode::ParameterCount>;

    enum class BypassPolicy : std::uint32_t {
        PreserveTails = 1U,
        Flush = 2U,
    };

    enum class AudioState : std::uint32_t {
        Effect = 0U,
        BypassTails = 1U,
        FlushClearing = 2U,
        BypassDry = 3U,
    };

    explicit DelayFirmwareRuntime(std::size_t max_delay_samples) noexcept
        : delay_(max_delay_samples) {}

    [[nodiscard]] bool Prepare(const PrepareSpec& spec,
                               StaticArena& arena,
                               const ParameterArray& initial_parameters) noexcept {
        if(prepared_ || spec.max_block_frames == 0U
           || spec.layout != ChannelLayout::Stereo) {
            return false;
        }

        if(!graph_.Add(delay_) || !graph_.Prepare(spec, arena)) {
            return false;
        }

        spec_ = spec;
        current_parameters_ = initial_parameters;
        parameter_mailbox_.Publish(initial_parameters);

        const NodeDescriptor descriptor = delay_.Describe();
        const std::size_t per_channel_bytes =
            descriptor.resources.persistent_bytes / 2U;
        flush_required_frames_ = per_channel_bytes / sizeof(float);
        if(flush_required_frames_ == 0U) {
            return false;
        }

        requested_state_.store(static_cast<std::uint32_t>(AudioState::Effect),
                               std::memory_order_release);
        published_audio_state_.store(
            static_cast<std::uint32_t>(AudioState::Effect),
            std::memory_order_release);
        flush_remaining_status_.store(0U, std::memory_order_release);
        prepared_ = true;
        return true;
    }

    void PublishParameters(const ParameterArray& parameters) noexcept {
        parameter_mailbox_.Publish(parameters);
    }

    void SetBypass(bool bypass,
                   BypassPolicy policy = BypassPolicy::PreserveTails) noexcept {
        const AudioState request = !bypass
                                       ? AudioState::Effect
                                       : (policy == BypassPolicy::Flush
                                              ? AudioState::FlushClearing
                                              : AudioState::BypassTails);
        requested_state_.store(static_cast<std::uint32_t>(request),
                               std::memory_order_release);
    }

    // This operation clears the complete delay history and is intentionally
    // non-real-time. The caller must stop audio before invoking it.
    void ResetNonRealtime(ResetReason reason) noexcept {
        if(!prepared_) {
            return;
        }

        graph_.Reset(reason);
        flush_remaining_frames_ = 0U;
        flush_remaining_status_.store(0U, std::memory_order_release);

        const AudioState requested = RequestedAudioState();
        audio_state_ = requested == AudioState::FlushClearing
                           ? AudioState::BypassDry
                           : requested;
        PublishAudioState();
    }

    [[nodiscard]] bool ProcessAudio(const AudioBlock& block,
                                    DiagnosticsCounters& diagnostics) noexcept {
        if(!prepared_ || block.frames == 0U
           || block.frames > spec_.max_block_frames || block.in[0] == nullptr
           || block.in[1] == nullptr || block.out[0] == nullptr
           || block.out[1] == nullptr) {
            return false;
        }

        ParameterArray candidate = current_parameters_;
        std::uint32_t candidate_generation = parameter_generation_;
        if(parameter_mailbox_.ReadBounded(candidate, candidate_generation)) {
            current_parameters_ = candidate;
            parameter_generation_ = candidate_generation;
        } else {
            snapshot_misses_.fetch_add(1U, std::memory_order_relaxed);
        }

        UpdateAudioStateFromRequest();

        if(audio_state_ == AudioState::BypassDry) {
            CopyDryAndAccount(block, diagnostics);
            return true;
        }

        ParameterArray block_parameters = current_parameters_;
        if(audio_state_ == AudioState::BypassTails) {
            // Keep processing the feedback path so the stored tail decays, but
            // stop new input injection. Dry signal remains unity while the
            // currently requested wet level governs the audible tail.
            block_parameters[DigitalDelayNode::InputSend] = 0.0F;
            block_parameters[DigitalDelayNode::DryMix] = 1.0F;
        } else if(audio_state_ == AudioState::FlushClearing) {
            // Zero-inject with feedback disabled while producing dry-only
            // output. After one complete history capacity has been overwritten,
            // the runtime can enter true dry bypass without an O(N) reset in
            // the callback.
            block_parameters[DigitalDelayNode::InputSend] = 0.0F;
            block_parameters[DigitalDelayNode::Feedback] = 0.0F;
            block_parameters[DigitalDelayNode::DryMix] = 1.0F;
            block_parameters[DigitalDelayNode::WetMix] = 0.0F;
        }

        const ParameterSnapshot snapshot{block_parameters.data(),
                                         block_parameters.size(),
                                         parameter_generation_};
        graph_.Process(block, &snapshot, diagnostics);

        if(audio_state_ == AudioState::FlushClearing) {
            CompleteFlushProgress(block.frames);
        }
        return true;
    }

    [[nodiscard]] std::uint32_t SnapshotMisses() const noexcept {
        return snapshot_misses_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] std::uint32_t ActiveParameterGeneration() const noexcept {
        return parameter_generation_status_.load(std::memory_order_relaxed);
    }

    [[nodiscard]] AudioState PublishedAudioState() const noexcept {
        return static_cast<AudioState>(
            published_audio_state_.load(std::memory_order_acquire));
    }

    [[nodiscard]] std::uint32_t FlushFramesRemaining() const noexcept {
        return flush_remaining_status_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t MaxDelaySamples() const noexcept {
        return delay_.MaxDelaySamples();
    }

  private:
    [[nodiscard]] AudioState RequestedAudioState() const noexcept {
        const auto raw = requested_state_.load(std::memory_order_acquire);
        if(raw == static_cast<std::uint32_t>(AudioState::BypassTails)) {
            return AudioState::BypassTails;
        }
        if(raw == static_cast<std::uint32_t>(AudioState::FlushClearing)) {
            return AudioState::FlushClearing;
        }
        return AudioState::Effect;
    }

    void UpdateAudioStateFromRequest() noexcept {
        parameter_generation_status_.store(parameter_generation_,
                                           std::memory_order_relaxed);

        // A flush already in progress has priority. This prevents stale history
        // from becoming audible if a rapid re-engage request arrives before the
        // complete circular buffer has been overwritten.
        if(audio_state_ == AudioState::FlushClearing
           && flush_remaining_frames_ > 0U) {
            PublishAudioState();
            return;
        }

        const AudioState requested = RequestedAudioState();
        if(requested == AudioState::FlushClearing) {
            if(audio_state_ != AudioState::BypassDry) {
                audio_state_ = AudioState::FlushClearing;
                flush_remaining_frames_ = flush_required_frames_;
                flush_remaining_status_.store(
                    static_cast<std::uint32_t>(flush_remaining_frames_),
                    std::memory_order_relaxed);
            }
        } else {
            audio_state_ = requested;
            flush_remaining_frames_ = 0U;
            flush_remaining_status_.store(0U, std::memory_order_relaxed);
        }
        PublishAudioState();
    }

    void CompleteFlushProgress(std::size_t processed_frames) noexcept {
        if(processed_frames >= flush_remaining_frames_) {
            flush_remaining_frames_ = 0U;
            flush_remaining_status_.store(0U, std::memory_order_relaxed);

            const AudioState requested = RequestedAudioState();
            if(requested == AudioState::Effect) {
                audio_state_ = AudioState::Effect;
            } else if(requested == AudioState::BypassTails) {
                audio_state_ = AudioState::BypassTails;
            } else {
                audio_state_ = AudioState::BypassDry;
            }
            PublishAudioState();
            return;
        }

        flush_remaining_frames_ -= processed_frames;
        flush_remaining_status_.store(
            static_cast<std::uint32_t>(flush_remaining_frames_),
            std::memory_order_relaxed);
    }

    void PublishAudioState() noexcept {
        published_audio_state_.store(static_cast<std::uint32_t>(audio_state_),
                                     std::memory_order_release);
    }

    static void CopyDryAndAccount(const AudioBlock& block,
                                  DiagnosticsCounters& diagnostics) noexcept {
        diagnostics.processed_blocks += 1U;
        diagnostics.processed_samples += block.frames;

        for(std::size_t channel = 0U; channel < 2U; ++channel) {
            for(std::size_t frame = 0U; frame < block.frames; ++frame) {
                float sample = block.in[channel][frame];
                if(!std::isfinite(sample)) {
                    sample = 0.0F;
                    diagnostics.non_finite_samples += 1U;
                }
                block.out[channel][frame] = sample;

                const float magnitude = std::fabs(sample);
                if(magnitude > diagnostics.max_abs_sample) {
                    diagnostics.max_abs_sample = magnitude;
                }
                if(magnitude > 1.0F) {
                    diagnostics.clipped_samples += 1U;
                }
            }
        }
    }

    DigitalDelayNode delay_;
    StaticSerialGraph<1U> graph_;
    PrepareSpec spec_{};
    RealtimeFloatSnapshotMailbox<DigitalDelayNode::ParameterCount>
        parameter_mailbox_{};
    ParameterArray current_parameters_{};
    std::uint32_t parameter_generation_ = 0U;
    std::size_t flush_required_frames_ = 0U;
    std::size_t flush_remaining_frames_ = 0U;
    AudioState audio_state_ = AudioState::Effect;
    std::atomic<std::uint32_t> requested_state_{
        static_cast<std::uint32_t>(AudioState::Effect)};
    std::atomic<std::uint32_t> published_audio_state_{
        static_cast<std::uint32_t>(AudioState::Effect)};
    std::atomic<std::uint32_t> snapshot_misses_{0U};
    std::atomic<std::uint32_t> parameter_generation_status_{0U};
    std::atomic<std::uint32_t> flush_remaining_status_{0U};
    bool prepared_ = false;
};

} // namespace phh
