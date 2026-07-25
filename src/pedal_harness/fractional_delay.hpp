#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

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

    [[nodiscard]] float Read(float position,
                             InterpolationPolicy policy) const noexcept {
        if(!IsValid() || !std::isfinite(position)) {
            return 0.0F;
        }

        const float wrapped = WrapPosition(position);
        const auto base = static_cast<std::ptrdiff_t>(std::floor(wrapped));
        const float fraction = wrapped - static_cast<float>(base);

        const float x0 = Sample(base);
        const float x1 = Sample(base + 1);

        if(policy == InterpolationPolicy::Linear) {
            return InterpolateLinear(x0, x1, fraction);
        }

        return InterpolateCubicLagrange(
            Sample(base - 1), x0, x1, Sample(base + 2), fraction);
    }

  private:
    [[nodiscard]] float WrapPosition(float position) const noexcept {
        const float capacity = static_cast<float>(capacity_);
        float wrapped = std::fmod(position, capacity);
        if(wrapped < 0.0F) {
            wrapped += capacity;
        }
        return wrapped;
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
