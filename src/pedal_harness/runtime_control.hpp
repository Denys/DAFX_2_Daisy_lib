#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace phh {

template <std::size_t ParameterCount>
struct RealtimeControlFrame {
    std::array<float, ParameterCount> parameters{};
    bool bypass_with_trails = false;
    std::uint32_t generation = 0U;
};

template <std::size_t ParameterCount>
class RealtimeControlMailbox final {
  public:
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free,
                  "32-bit atomics must be lock-free for realtime control transfer");

    void Publish(const std::array<float, ParameterCount>& parameters,
                 bool bypass_with_trails) noexcept {
        sequence_.fetch_add(1U, std::memory_order_acq_rel); // odd: writer active
        for(std::size_t i = 0; i < ParameterCount; ++i) {
            parameter_bits_[i].store(FloatToBits(parameters[i]),
                                     std::memory_order_relaxed);
        }
        bypass_bits_.store(bypass_with_trails ? 1U : 0U,
                           std::memory_order_relaxed);
        sequence_.fetch_add(1U, std::memory_order_release); // even: publish
    }

    [[nodiscard]] bool ReadBounded(
        RealtimeControlFrame<ParameterCount>& frame) const noexcept {
        // The audio thread never spins. Two bounded attempts are sufficient to
        // accept a coherent frame or retain the previous complete snapshot.
        for(std::size_t attempt = 0; attempt < 2U; ++attempt) {
            const std::uint32_t before =
                sequence_.load(std::memory_order_acquire);
            if((before & 1U) != 0U) {
                continue;
            }

            RealtimeControlFrame<ParameterCount> candidate{};
            for(std::size_t i = 0; i < ParameterCount; ++i) {
                candidate.parameters[i] = BitsToFloat(
                    parameter_bits_[i].load(std::memory_order_relaxed));
            }
            candidate.bypass_with_trails =
                bypass_bits_.load(std::memory_order_relaxed) != 0U;

            const std::uint32_t after =
                sequence_.load(std::memory_order_acquire);
            if(before == after && (after & 1U) == 0U) {
                candidate.generation = after / 2U;
                frame = candidate;
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
    std::array<std::atomic<std::uint32_t>, ParameterCount> parameter_bits_{};
    std::atomic<std::uint32_t> bypass_bits_{0U};
};

struct ToggleGestureState {
    bool cubic_interpolation = false;
    bool bypass_with_trails = false;

    void Update(bool interpolation_toggle_edge,
                bool bypass_toggle_edge) noexcept {
        if(interpolation_toggle_edge) {
            cubic_interpolation = !cubic_interpolation;
        }
        if(bypass_toggle_edge) {
            bypass_with_trails = !bypass_with_trails;
        }
    }
};

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

} // namespace phh
