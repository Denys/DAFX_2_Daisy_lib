#include "stereopan.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace daisysp
{
void StereoPan::Init()
{
    pan_ = 0.0f;
    speaker_angle_ = 30.0f;
    gain_left_ = 0.7071f;  // 1/sqrt(2) for center
    gain_right_ = 0.7071f;
}

void StereoPan::RecalculateGains()
{
    // Convert angles to radians
    float theta_rad = pan_ * 90.0f * M_PI / 180.0f;
    float lsbase_rad = speaker_angle_ * M_PI / 180.0f;

    // Compute gain factors with tangent law
    float g2 = 1.0f;
    float g1 = -(std::tan(theta_rad) - std::tan(lsbase_rad)) /
              (std::tan(theta_rad) + std::tan(lsbase_rad) + 1e-10f);

    // Normalize sum-of-squares
    float sum_sq = g1 * g1 + g2 * g2;
    float norm = std::sqrt(sum_sq);

    gain_left_ = g1 / norm;
    gain_right_ = g2 / norm;
}

void StereoPan::Process(const float &in, float *out_left, float *out_right)
{
    *out_left = in * gain_left_;
    *out_right = in * gain_right_;
}

} // namespace daisysp
