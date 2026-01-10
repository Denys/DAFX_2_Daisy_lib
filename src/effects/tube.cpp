#include <cmath>
#include "tube.h"

namespace daisysp
{
void Tube::Init(float sample_rate)
{
    drive_ = 1.0f;
    bias_ = 0.0f;
    dist_ = 1.0f;
    rh_ = 0.99f;
    rl_ = 0.5f;
    mix_ = 1.0f;

    hp_xnm1_ = 0.0f;
    hp_xnm2_ = 0.0f;
    hp_ynm1_ = 0.0f;
    hp_ynm2_ = 0.0f;
    lp_xnm1_ = 0.0f;
    lp_ynm1_ = 0.0f;
}

float Tube::ProcessWaveshaper(float in)
{
    float q = in * drive_;

    if (bias_ == 0.0f)
    {
        if (q == 0.0f)
        {
            return 1.0f / dist_;
        }
        return q / (1.0f - std::exp(-dist_ * q));
    }
    else
    {
        float q_minus_bias = q - bias_;
        if (q == bias_)
        {
            return 1.0f / dist_ + bias_ / (1.0f - std::exp(dist_ * bias_));
        }
        return (q_minus_bias) / (1.0f - std::exp(-dist_ * q_minus_bias))
               + bias_ / (1.0f - std::exp(dist_ * bias_));
    }
}

float Tube::ProcessHighPass(float in)
{
    float rh_sq = rh_ * rh_;
    float out = in - 2.0f * hp_xnm1_ + hp_xnm2_
                + 2.0f * rh_ * hp_ynm1_ - rh_sq * hp_ynm2_;
    
    hp_xnm2_ = hp_xnm1_;
    hp_xnm1_ = in;
    hp_ynm2_ = hp_ynm1_;
    hp_ynm1_ = out;
    
    return out;
}

float Tube::ProcessLowPass(float in)
{
    float out = (1.0f - rl_) * in + rl_ * lp_ynm1_;
    lp_xnm1_ = in;
    lp_ynm1_ = out;
    return out;
}

float Tube::Process(float in)
{
    float z = ProcessWaveshaper(in);
    float y = mix_ * z + (1.0f - mix_) * in;
    y = ProcessHighPass(y);
    y = ProcessLowPass(y);
    return y;
}

} // namespace daisysp
