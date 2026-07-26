#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace phh {

enum class InterpolationPolicy : std::uint8_t {
    Linear,
    CubicLagrange,
};

[[nodiscard]] inline float InterpolateLinear(float x0,
                                             float x1,
                                             float fraction) noexcept {
    const float mu = std::clamp(fraction, 0.0F, 1.0F);
    return x0 + mu * (x1 - x0);
}

[[nodiscard]] inline float InterpolateCubicLagrange(float xm1,
                                                    float x0,
                                                    float x1,
                                                    float x2,
                                                    float fraction) noexcept {
    const float mu = std::clamp(fraction, 0.0F, 1.0F);

    // Third-order Lagrange polynomial through samples at -1, 0, 1 and 2.
    const float c_m1 = -mu * (mu - 1.0F) * (mu - 2.0F) / 6.0F;
    const float c_0 = (mu + 1.0F) * (mu - 1.0F) * (mu - 2.0F) / 2.0F;
    const float c_1 = -(mu + 1.0F) * mu * (mu - 2.0F) / 2.0F;
    const float c_2 = (mu + 1.0F) * mu * (mu - 1.0F) / 6.0F;

    return c_m1 * xm1 + c_0 * x0 + c_1 * x1 + c_2 * x2;
}

class CircularFractionalReader final {
  public:
    CircularFractionalReader(const float* buffer, std::size_t capacity) noexcept
        : buffer_(buffer), capacity_(capacity) {}

    [[nodiscard]] bool IsValid() const noexcept {
        return buffer_ != nullptr && capacity_ >= 4U;
    }

    // Target-facing API: keep the integer sample position separate from the
    // fractional phase. This prevents an accumulating binary32 position from
    // silently losing its fractional part after a few minutes of audio.
    [[nodiscard]] float Read(std::ptrdiff_t base,
                             float fraction,
                             InterpolationPolicy policy) const noexcept {
        if(!IsValid() || !Normalize(base, fraction)) {
            return 0.0F;
        }

        const float x0 = Sample(base);
        const float x1 = Sample(base + 1);

        if(policy == InterpolationPolicy::Linear) {
            return InterpolateLinear(x0, x1, fraction);
        }

        return InterpolateCubicLagrange(
            Sample(base - 1), x0, x1, Sample(base + 2), fraction);
    }

  private:
    [[nodiscard]] static bool AddChecked(std::ptrdiff_t& base,
                                         std::ptrdiff_t delta) noexcept {
        constexpr auto kMin = std::numeric_limits<std::ptrdiff_t>::min();
        constexpr auto kMax = std::numeric_limits<std::ptrdiff_t>::max();

        if((delta > 0 && base > kMax - delta)
           || (delta < 0 && base < kMin - delta)) {
            return false;
        }
        base += delta;
        return true;
    }

    [[nodiscard]] static bool Normalize(std::ptrdiff_t& base,
                                        float& fraction) noexcept {
        if(!std::isfinite(fraction)) {
            return false;
        }

        if(fraction >= 1.0F || fraction < 0.0F) {
            const float whole = std::floor(fraction);
            if(whole < static_cast<float>(
                           std::numeric_limits<std::ptrdiff_t>::min())
               || whole > static_cast<float>(
                              std::numeric_limits<std::ptrdiff_t>::max())) {
                return false;
            }

            const auto carry = static_cast<std::ptrdiff_t>(whole);
            if(!AddChecked(base, carry)) {
                return false;
            }
            fraction -= whole;
        }

        // Defend against the rare case where rounding produces exactly 1.0.
        if(fraction >= 1.0F) {
            if(!AddChecked(base, 1)) {
                return false;
            }
            fraction = 0.0F;
        }
        return fraction >= 0.0F && fraction < 1.0F;
    }

    [[nodiscard]] float Sample(std::ptrdiff_t index) const noexcept {
        const auto capacity = static_cast<std::ptrdiff_t>(capacity_);
        std::ptrdiff_t wrapped = index % capacity;
        if(wrapped < 0) {
            wrapped += capacity;
        }
        return buffer_[static_cast<std::size_t>(wrapped)];
    }

    const float* buffer_ = nullptr;
    std::size_t capacity_ = 0U;
};

} // namespace phh
