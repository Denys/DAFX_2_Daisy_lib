#include "peakfilter.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace daisysp
{
void PeakFilter::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    freq_ = 1000.0f;
    bandwidth_ = 100.0f;
    gain_ = 0.0f;
    xh1_ = 0.0f;
    xh2_ = 0.0f;

    RecalculateCoefficients();
}

void PeakFilter::RecalculateCoefficients()
{
    // Convert frequency to normalized frequency (0 < Wc < 1)
    // Wc = 2 * fc / fs
    float wc = 2.0f * freq_ / sample_rate_;

    // Convert bandwidth to normalized bandwidth (0 < Wb < 1)
    // Wb = 2 * fb / fs
    float wb = 2.0f * bandwidth_ / sample_rate_;

    // Clamp wc and wb to valid range
    if (wc >= 1.0f)
    {
        wc = 0.999f;
    }
    if (wc <= 0.0f)
    {
        wc = 0.001f;
    }
    if (wb >= 1.0f)
    {
        wb = 0.999f;
    }
    if (wb <= 0.0f)
    {
        wb = 0.001f;
    }

    // Convert gain from dB to linear
    float v0 = std::pow(10.0f, gain_ / 20.0f);
    h0_ = v0 - 1.0f;

    // Calculate allpass coefficient
    float tan_half_wb = std::tan(M_PI * wb / 2.0f);

    if (gain_ >= 0.0f)
    {
        // Boost
        c_ = (tan_half_wb - 1.0f) / (tan_half_wb + 1.0f);
    }
    else
    {
        // Cut
        c_ = (tan_half_wb - v0) / (tan_half_wb + v0);
    }

    // Calculate frequency coefficient
    d_ = -std::cos(M_PI * wc);
}

float PeakFilter::Process(const float &in)
{
    // Allpass filter (second-order)
    float xh_new = in - d_ * (1.0f - c_) * xh1_ + c_ * xh2_;
    float ap_y = -c_ * xh_new + d_ * (1.0f - c_) * xh1_ + xh2_;

    // Update state
    xh2_ = xh1_;
    xh1_ = xh_new;

    // Peak filter output
    float out = 0.5f * h0_ * (in - ap_y) + in;

    return out;
}

} // namespace daisysp
