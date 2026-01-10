// # WahWah
// Wah-wah effect based on variable bandpass filter
//
// Based on DAFX book Chapter 12 on Virtual Analog
// Implements a wah-wah effect using a modulated bandpass filter
// Suitable for creating expressive vocal and guitar effects
//
// ## Parameters
// - frequency: Wah frequency in Hz (200-2000 Hz, default 500 Hz)
// - q: Filter Q factor (1-20, default 5)
// - depth: Modulation depth (0-1, default 1.0)
//
// ## Example
// ~~~~
// WahWah wah;
// wah.Init(48000.0f);
// wah.SetFrequency(500.0f);  // 500 Hz wah
// wah.SetQ(5.0f);  // Q factor
// wah.SetDepth(1.0f);  // Full modulation
// float out = wah.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 12, Section 12.3
#pragma once
#ifndef DSY_WAHWAH_H
#define DSY_WAHWAH_H

#include <cmath>

namespace daisysp
{
class WahWah
{
public:
    WahWah() {}
    ~WahWah() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetFrequency(const float &freq)
    {
        freq_ = freq;
        RecalculateCoefficients();
    }

    inline void SetQ(const float &q)
    {
        q_ = q;
        RecalculateCoefficients();
    }

    inline void SetDepth(const float &depth)
    {
        depth_ = depth;
    }

    inline float GetFrequency() const { return freq_; }
    inline float GetQ() const { return q_; }
    inline float GetDepth() const { return depth_; }

private:
    float sample_rate_;
    float freq_;
    float q_;
    float depth_;

    float b0_;
    float b1_;
    float b2_;
    float a0_;
    float a1_;
    float a2_;

    float xnm1_;
    float xnm2_;
    float ynm1_;
    float ynm2_;

    void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_WAHWAH_H
