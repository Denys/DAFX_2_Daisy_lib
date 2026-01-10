#include "noisegate.h"
#include <cmath>
#include <algorithm>

namespace daisysp
{
void NoiseGate::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    threshold_db_ = -40.0f;
    hold_time_ = 0.1f;
    attack_time_ = 0.001f;
    release_time_ = 0.1f;
    alpha_ = 0.99f;

    envelope_ = 0.0f;
    gate_gain_ = 1.0f;
    low_threshold_count_ = 0;
    upper_threshold_count_ = 0;

    RecalculateThresholds();
    RecalculateCoefficients();
}

void NoiseGate::RecalculateThresholds()
{
    // Convert dB to linear
    threshold_linear_ = std::pow(10.0f, threshold_db_ / 20.0f);
    threshold_upper_linear_ = threshold_linear_ * 1.4142f;  // +3 dB hysteresis
}

void NoiseGate::RecalculateCoefficients()
{
    // Calculate sample counts
    hold_samples_ = static_cast<int>(hold_time_ * sample_rate_ + 0.5f);
    attack_samples_ = static_cast<int>(attack_time_ * sample_rate_ + 0.5f);
    release_samples_ = static_cast<int>(release_time_ * sample_rate_ + 0.5f);
}

float NoiseGate::Process(const float &in)
{
    float abs_in = std::abs(in);

    // Envelope detection filter (first-order lowpass)
    envelope_ = alpha_ * envelope_ + (1.0f - alpha_) * abs_in;

    // Gate logic with hysteresis
    if (envelope_ <= threshold_linear_ &&
        envelope_ < threshold_upper_linear_ &&
        low_threshold_count_ > 0)
    {
        // Below lower threshold
        low_threshold_count_++;
        upper_threshold_count_ = 0;

        if (low_threshold_count_ > hold_samples_)
        {
            // Time below lower threshold longer than hold time
            if (low_threshold_count_ > (release_samples_ + hold_samples_))
            {
                // Fade signal to zero
                gate_gain_ = 1.0f - static_cast<float>(low_threshold_count_ - hold_samples_) /
                                 static_cast<float>(release_samples_);
            }
            else
            {
                gate_gain_ = 0.0f;
            }
        }
        else
        {
            gate_gain_ = 0.0f;
        }
    }
    else if (envelope_ >= threshold_upper_linear_ &&
             envelope_ > threshold_linear_ &&
             upper_threshold_count_ > 0)
    {
        // Above upper threshold
        upper_threshold_count_++;
        low_threshold_count_ = 0;

        if (gate_gain_ < 1.0f)
        {
            // Attack: fade signal to full
            gate_gain_ = fminf(gate_gain_ + 1.0f / static_cast<float>(attack_samples_), 1.0f);
        }
        else
        {
            gate_gain_ = 1.0f;
        }
    }
    else
    {
        // In hysteresis region, maintain previous gain
        low_threshold_count_ = 0;
        upper_threshold_count_ = 0;
    }

    // Apply gate
    float out = in * gate_gain_;

    return out;
}

} // namespace daisysp
