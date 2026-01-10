#include "lowshelving.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace daisysp
{
void LowShelving::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    freq_ = 100.0f;
    gain_ = 0.0f;
    xh_ = 0.0f;

    RecalculateCoefficients();
}

void LowShelving::RecalculateCoefficients()
{
    // Convert frequency to normalized frequency (0 < Wc < 1)
    // Wc = 2 * fc / fs
    float wc = 2.0f * freq_ / sample_rate_;

    // Clamp wc to valid range
    if (wc >= 1.0f)
    {
        wc = 0.999f;
    }
    if (wc <= 0.0f)
    {
        wc = 0.001f;
    }

    // Convert gain from dB to linear
    float v0 = std::pow(10.0f, gain_ / 20.0f);
    h0_ = v0 - 1.0f;

    // Calculate allpass coefficient
    float tan_half_wc = std::tan(M_PI * wc / 2.0f);

    if (gain_ >= 0.0f)
    {
        // Boost
        c_ = (tan_half_wc - 1.0f) / (tan_half_wc + 1.0f);
    }
    else
    {
        // Cut
        c_ = (tan_half_wc - v0) / (tan_half_wc + v0);
    }
}

float LowShelving::Process(const float &in)
{
    // Allpass filter
    float xh_new = in - c_ * xh_;
    float ap_y = c_ * xh_new + xh_;
    xh_ = xh_new;

    // Shelving output
    float out = 0.5f * h0_ * (in + ap_y) + in;

    return out;
}

} // namespace daisysp
