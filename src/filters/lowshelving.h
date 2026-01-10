// # LowShelving
// Low-frequency shelving filter with boost/cut capability
//
// Ported from DAFX book lowshelving.m by M. Holters
// Implements a first-order shelving filter using an allpass structure
// Suitable for bass tone control and equalization
//
// ## Parameters
// - frequency: Cutoff frequency in Hz (20-20000 Hz, default 100 Hz)
// - gain: Gain in dB (-20 to +20 dB, default 0 dB)
//
// ## Example
// ~~~~
// LowShelving bass_shelf;
// bass_shelf.Init(48000.0f);
// bass_shelf.SetFrequency(100.0f);
// bass_shelf.SetGain(6.0f);  // +6 dB bass boost
// float out = bass_shelf.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2, Section 2.3
// - Original MATLAB: lowshelving.m
#pragma once
#ifndef DSY_LOWSHELVING_H
#define DSY_LOWSHELVING_H

#include <cmath>

namespace daisysp
{
class LowShelving
{
public:
    LowShelving() {}
    ~LowShelving() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetFrequency(const float &freq)
    {
        freq_ = freq;
        RecalculateCoefficients();
    }

    inline void SetGain(const float &gain)
    {
        gain_ = gain;
        RecalculateCoefficients();
    }

    inline float GetFrequency() const { return freq_; }
    inline float GetGain() const { return gain_; }

private:
    float sample_rate_;
    float freq_;
    float gain_;

    float c_;
    float h0_;
    float xh_;

    void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_LOWSHELVING_H
