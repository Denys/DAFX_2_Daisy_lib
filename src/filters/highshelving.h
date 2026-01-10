// # HighShelving
// High-frequency shelving filter with boost/cut capability
//
// Derived from DAFX book lowshelving.m by M. Holters
// Implements a first-order high-frequency shelving filter using an allpass structure
// Suitable for treble tone control and equalization
//
// ## Parameters
// - frequency: Cutoff frequency in Hz (20-20000 Hz, default 4000 Hz)
// - gain: Gain in dB (-20 to +20 dB, default 0 dB)
//
// ## Example
// ~~~~
// HighShelving treble_shelf;
// treble_shelf.Init(48000.0f);
// treble_shelf.SetFrequency(4000.0f);
// treble_shelf.SetGain(-2.0f);  // -2 dB treble cut
// float out = treble_shelf.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2, Section 2.3
// - Derived from: lowshelving.m (change sign in output equation)
#pragma once
#ifndef DSY_HIGHSHELVING_H
#define DSY_HIGHSHELVING_H

#include <cmath>

namespace daisysp
{
class HighShelving
{
public:
    HighShelving() {}
    ~HighShelving() {}

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

#endif // DSY_HIGHSHELVING_H
