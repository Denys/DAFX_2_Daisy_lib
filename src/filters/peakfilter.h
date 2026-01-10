// # PeakFilter
// Peak/Parametric EQ filter with adjustable center frequency and bandwidth
//
// Ported from DAFX book peakfilt.m by M. Holters
// Implements a second-order peak filter using an allpass structure
// Suitable for parametric equalization and tone shaping
//
// ## Parameters
// - frequency: Center frequency in Hz (20-20000 Hz, default 1000 Hz)
// - bandwidth: Bandwidth in Hz (10-10000 Hz, default 100 Hz)
// - gain: Gain in dB (-20 to +20 dB, default 0 dB)
//
// ## Example
// ~~~~
// PeakFilter mid_eq;
// mid_eq.Init(48000.0f);
// mid_eq.SetFrequency(1000.0f);
// mid_eq.SetBandwidth(100.0f);
// mid_eq.SetGain(6.0f);  // +6 dB boost at 1 kHz
// float out = mid_eq.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2, Section 2.3
// - Original MATLAB: peakfilt.m
#pragma once
#ifndef DSY_PEAKFILTER_H
#define DSY_PEAKFILTER_H

#include <cmath>

namespace daisysp
{
class PeakFilter
{
public:
    PeakFilter() {}
    ~PeakFilter() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetFrequency(const float &freq)
    {
        freq_ = freq;
        RecalculateCoefficients();
    }

    inline void SetBandwidth(const float &bw)
    {
        bandwidth_ = bw;
        RecalculateCoefficients();
    }

    inline void SetGain(const float &gain)
    {
        gain_ = gain;
        RecalculateCoefficients();
    }

    inline float GetFrequency() const { return freq_; }
    inline float GetBandwidth() const { return bandwidth_; }
    inline float GetGain() const { return gain_; }

private:
    float sample_rate_;
    float freq_;
    float bandwidth_;
    float gain_;

    float c_;
    float d_;
    float h0_;
    float xh1_;
    float xh2_;

    void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_PEAKFILTER_H
