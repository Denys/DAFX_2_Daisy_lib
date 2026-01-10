// # RingModulator
// Ring modulation effect using sine wave multiplication
//
// Simple ring modulator that multiplies input signal by a sine wave
// Creates metallic, bell-like tones and classic ring modulation effects
//
// ## Parameters
// - frequency: Modulation frequency in Hz (1-10000 Hz, default 440 Hz)
// - depth: Modulation depth (0-1, default 1.0)
//
// ## Example
// ~~~~
// RingModulator ring_mod;
// ring_mod.Init(48000.0f);
// ring_mod.SetFrequency(440.0f);  // A4 note
// ring_mod.SetDepth(1.0f);  // Full modulation
// float out = ring_mod.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 3, Section 3.2
#pragma once
#ifndef DSY_RINGMOD_H
#define DSY_RINGMOD_H

#include <cmath>

namespace daisysp
{
class RingModulator
{
public:
    RingModulator() {}
    ~RingModulator() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetFrequency(const float &freq)
    {
        freq_ = freq;
        RecalculatePhaseIncrement();
    }

    inline void SetDepth(const float &depth)
    {
        depth_ = depth;
    }

    inline float GetFrequency() const { return freq_; }
    inline float GetDepth() const { return depth_; }

private:
    float sample_rate_;
    float freq_;
    float depth_;
    float phase_;
    float phase_increment_;

    void RecalculatePhaseIncrement();
};

} // namespace daisysp

#endif // DSY_RINGMOD_H
