#include "stereopan.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {
void StereoPan::Init() {
  pan_ = 0.0f;
  speaker_angle_ = 30.0f;
  gain_left_ = 0.7071f; // 1/sqrt(2) for center
  gain_right_ = 0.7071f;
}

void StereoPan::RecalculateGains() {
  // Use equal-power cosine pan law
  // pan_ ranges from -1 (full left) to +1 (full right)
  // Map to angle: -1 -> 0, 0 -> π/4, +1 -> π/2
  float angle = (pan_ + 1.0f) * 0.25f * M_PI; // 0 to π/2

  // Equal-power: left = cos(angle), right = sin(angle)
  gain_left_ = std::cos(angle);
  gain_right_ = std::sin(angle);
}

void StereoPan::Process(const float &in, float *out_left, float *out_right) {
  *out_left = in * gain_left_;
  *out_right = in * gain_right_;
}

} // namespace daisysp
