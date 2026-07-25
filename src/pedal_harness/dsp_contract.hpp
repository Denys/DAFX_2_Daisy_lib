#pragma once

#include <cstddef>
#include <cstdint>
#include <limits>

namespace phh {

enum class ChannelLayout : std::uint8_t {
    Mono = 1,
    Stereo = 2,
    DualMono = 3,
};

enum class ResetReason : std::uint8_t {
    PowerOn,
    PresetRecall,
    ModeChange,
    FaultRecovery,
    UserRequest,
};

enum class TailPolicy : std::uint8_t {
    Flush,
    Preserve,
    Fade,
};

struct PrepareSpec {
    float sample_rate_hz = 48000.0F;
    std::size_t max_block_frames = 48;
    ChannelLayout layout = ChannelLayout::Stereo;
};

struct ResourceEnvelope {
    std::size_t persistent_bytes = 0;
    std::size_t scratch_bytes = 0;
    std::uint32_t algorithmic_latency_samples = 0;
    std::uint32_t nominal_tail_samples = 0;
    bool requires_external_memory = false;
};

struct NodeDescriptor {
    const char* stable_id = "UNASSIGNED";
    std::uint32_t schema_version = 1;
    ChannelLayout supported_layout = ChannelLayout::Stereo;
    TailPolicy default_tail_policy = TailPolicy::Preserve;
    ResourceEnvelope resources{};
};

struct ParameterSnapshot {
    const float* values = nullptr;
    std::size_t count = 0;
    std::uint32_t generation = 0;

    [[nodiscard]] float GetOr(std::size_t index, float fallback) const noexcept {
        return (values != nullptr && index < count) ? values[index] : fallback;
    }
};

struct AudioBlock {
    const float* in[2] = {nullptr, nullptr};
    float* out[2] = {nullptr, nullptr};
    std::size_t frames = 0;
};

struct DiagnosticsCounters {
    std::uint64_t processed_blocks = 0;
    std::uint64_t processed_samples = 0;
    std::uint64_t non_finite_samples = 0;
    std::uint64_t clipped_samples = 0;
    float max_abs_sample = 0.0F;

    void Reset() noexcept {
        processed_blocks = 0;
        processed_samples = 0;
        non_finite_samples = 0;
        clipped_samples = 0;
        max_abs_sample = 0.0F;
    }
};

class StaticArena {
  public:
    StaticArena(void* storage, std::size_t bytes) noexcept
        : base_(static_cast<std::uint8_t*>(storage)), capacity_(bytes) {}

    [[nodiscard]] void* Allocate(std::size_t bytes,
                                 std::size_t alignment) noexcept {
        if(base_ == nullptr || bytes == 0 || alignment == 0
           || (alignment & (alignment - 1U)) != 0U) {
            return nullptr;
        }

        const auto current = reinterpret_cast<std::uintptr_t>(base_ + used_);
        const auto aligned = (current + alignment - 1U) & ~(alignment - 1U);
        const auto padding = static_cast<std::size_t>(aligned - current);

        if(padding > capacity_ - used_
           || bytes > capacity_ - used_ - padding) {
            return nullptr;
        }

        used_ += padding + bytes;
        return reinterpret_cast<void*>(aligned);
    }

    template <typename T>
    [[nodiscard]] T* AllocateArray(std::size_t count) noexcept {
        if(count > (std::numeric_limits<std::size_t>::max() / sizeof(T))) {
            return nullptr;
        }
        return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
    }

    [[nodiscard]] std::size_t Used() const noexcept { return used_; }
    [[nodiscard]] std::size_t Remaining() const noexcept {
        return capacity_ - used_;
    }

  private:
    std::uint8_t* base_ = nullptr;
    std::size_t capacity_ = 0;
    std::size_t used_ = 0;
};

class DspNode {
  public:
    virtual ~DspNode() = default;

    [[nodiscard]] virtual NodeDescriptor Describe() const noexcept = 0;

    // Called outside the audio callback. All state and scratch allocations
    // must come from the supplied arena or compile-time/static storage.
    [[nodiscard]] virtual bool Prepare(const PrepareSpec& spec,
                                       StaticArena& arena) noexcept = 0;

    virtual void Reset(ResetReason reason) noexcept = 0;

    // Audio-thread contract:
    // - no heap allocation
    // - no locks, filesystem, logging, display or storage calls
    // - bounded runtime for every valid parameter snapshot
    virtual void Process(const AudioBlock& block,
                         const ParameterSnapshot& parameters,
                         DiagnosticsCounters& diagnostics) noexcept = 0;
};

} // namespace phh
