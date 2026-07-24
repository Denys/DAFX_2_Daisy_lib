#pragma once

#include "pedal_harness/dsp_contract.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace phh {

template <std::size_t MaxNodes>
class StaticSerialGraph final {
  public:
    [[nodiscard]] bool Add(DspNode& node) noexcept {
        if(node_count_ >= MaxNodes) {
            return false;
        }
        nodes_[node_count_++] = &node;
        return true;
    }

    [[nodiscard]] bool Prepare(const PrepareSpec& spec,
                               StaticArena& arena) noexcept {
        if(spec.max_block_frames == 0) {
            return false;
        }

        spec_ = spec;

        for(std::size_t i = 0; i < node_count_; ++i) {
            if(nodes_[i] == nullptr || !nodes_[i]->Prepare(spec, arena)) {
                return false;
            }
        }

        const auto samples_per_buffer = spec.max_block_frames * 2U;
        scratch_a_ = arena.AllocateArray<float>(samples_per_buffer);
        scratch_b_ = arena.AllocateArray<float>(samples_per_buffer);
        return scratch_a_ != nullptr && scratch_b_ != nullptr;
    }

    void Reset(ResetReason reason) noexcept {
        for(std::size_t i = 0; i < node_count_; ++i) {
            nodes_[i]->Reset(reason);
        }
    }

    void Process(const AudioBlock& input_output,
                 const ParameterSnapshot* node_parameters,
                 DiagnosticsCounters& diagnostics) noexcept {
        if(input_output.frames == 0
           || input_output.frames > spec_.max_block_frames) {
            return;
        }

        if(node_count_ == 0) {
            CopyBlock(input_output.in, input_output.out, input_output.frames);
            AccountOutput(input_output, diagnostics);
            return;
        }

        const float* current_in[2] = {input_output.in[0], input_output.in[1]};
        float* current_out[2] = {nullptr, nullptr};

        for(std::size_t node_index = 0; node_index < node_count_;
            ++node_index) {
            const bool is_last = (node_index + 1U == node_count_);

            if(is_last) {
                current_out[0] = input_output.out[0];
                current_out[1] = input_output.out[1];
            } else {
                float* target =
                    (node_index % 2U == 0U) ? scratch_a_ : scratch_b_;
                current_out[0] = target;
                current_out[1] = target + spec_.max_block_frames;
            }

            AudioBlock block{{current_in[0], current_in[1]},
                             {current_out[0], current_out[1]},
                             input_output.frames};

            const ParameterSnapshot parameters =
                (node_parameters != nullptr) ? node_parameters[node_index]
                                             : ParameterSnapshot{};

            nodes_[node_index]->Process(block, parameters, diagnostics);

            current_in[0] = current_out[0];
            current_in[1] = current_out[1];
        }

        AccountOutput(input_output, diagnostics);
    }

    [[nodiscard]] std::size_t NodeCount() const noexcept {
        return node_count_;
    }

  private:
    static void CopyBlock(const float* const in[2],
                          float* const out[2],
                          std::size_t frames) noexcept {
        for(std::size_t channel = 0; channel < 2U; ++channel) {
            if(in[channel] == nullptr || out[channel] == nullptr) {
                continue;
            }
            for(std::size_t frame = 0; frame < frames; ++frame) {
                out[channel][frame] = in[channel][frame];
            }
        }
    }

    static void AccountOutput(const AudioBlock& block,
                              DiagnosticsCounters& diagnostics) noexcept {
        diagnostics.processed_blocks += 1U;
        diagnostics.processed_samples += block.frames;

        for(std::size_t channel = 0; channel < 2U; ++channel) {
            if(block.out[channel] == nullptr) {
                continue;
            }
            for(std::size_t frame = 0; frame < block.frames; ++frame) {
                const float sample = block.out[channel][frame];
                if(!std::isfinite(sample)) {
                    diagnostics.non_finite_samples += 1U;
                    continue;
                }
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

    std::array<DspNode*, MaxNodes> nodes_{};
    std::size_t node_count_ = 0;
    PrepareSpec spec_{};
    float* scratch_a_ = nullptr;
    float* scratch_b_ = nullptr;
};

} // namespace phh
