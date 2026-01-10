// # NoiseGate
// Noise gate with hysteresis
//
// Ported from DAFX book noisegt.m by R. Bendiksen
// Implements a noise gate with envelope detection and hysteresis
// Suitable for removing noise and unwanted low-level signals
//
// ## Parameters
// - threshold: Gate threshold in dB (-60 to 0 dB, default -40 dB)
// - hold_time: Time signal must be below threshold in seconds (0.001-1.0 s, default 0.1 s)
// - attack_time: Attack time in seconds (0.0001-0.1 s, default 0.001 s)
// - release_time: Release time in seconds (0.001-1.0 s, default 0.1 s)
// - alpha: Envelope detection filter coefficient (0-1, default 0.99)
//
// ## Example
// ~~~~
// NoiseGate gate;
// gate.Init(48000.0f);
// gate.SetThreshold(-40.0f);  // -40 dB threshold
// gate.SetHoldTime(0.1f);  // 100 ms hold
// gate.SetAttackTime(0.001f);  // 1 ms attack
// gate.SetReleaseTime(0.1f);  // 100 ms release
// float out = gate.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 4, Section 4.2
// - Original MATLAB: noisegt.m
#pragma once
#ifndef DSY_NOISEGATE_H
#define DSY_NOISEGATE_H

#include <cmath>

namespace daisysp
{
class NoiseGate
{
public:
    NoiseGate() {}
    ~NoiseGate() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetThreshold(const float &thresh_db)
    {
        threshold_db_ = thresh_db;
        RecalculateThresholds();
    }

    inline void SetHoldTime(const float &hold_time)
    {
        hold_time_ = hold_time;
        RecalculateCoefficients();
    }

    inline void SetAttackTime(const float &attack_time)
    {
        attack_time_ = attack_time;
        RecalculateCoefficients();
    }

    inline void SetReleaseTime(const float &release_time)
    {
        release_time_ = release_time;
        RecalculateCoefficients();
    }

    inline void SetAlpha(const float &alpha)
    {
        alpha_ = alpha;
    }

    inline float GetThreshold() const { return threshold_db_; }
    inline float GetHoldTime() const { return hold_time_; }
    inline float GetAttackTime() const { return attack_time_; }
    inline float GetReleaseTime() const { return release_time_; }
    inline float GetAlpha() const { return alpha_; }

private:
    float sample_rate_;
    float threshold_db_;
    float hold_time_;
    float attack_time_;
    float release_time_;
    float alpha_;

    float threshold_linear_;
    float threshold_upper_linear_;
    int hold_samples_;
    int attack_samples_;
    int release_samples_;

    float envelope_;
    float gate_gain_;
    int low_threshold_count_;
    int upper_threshold_count_;

    void RecalculateThresholds();
    void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_NOISEGATE_H
