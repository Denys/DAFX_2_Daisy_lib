// # ToneStack
// Tone stack effect based on virtual analog circuit
//
// Based on DAFX book Chapter 12 on Virtual Analog
// Implements a simplified tone stack with bass, middle, and treble controls
// Suitable for guitar and instrument tone shaping
//
// ## Parameters
// - bass: Bass control (-1 to +1, default 0)
// - middle: Mid control (-1 to +1, default 0)
// - treble: Treble control (-1 to +1, default 0)
//
// ## Example
// ~~~~
// ToneStack tone;
// tone.Init(48000.0f);
// tone.SetBass(0.5f);  // Boost bass
// tone.SetMiddle(-0.3f);  // Cut mids
// tone.SetTreble(0.2f);  // Boost treble
// float out = tone.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 12, Section 12.4
#pragma once
#ifndef DSY_TONESTACK_H
#define DSY_TONESTACK_H

#include <cmath>

namespace daisysp
{
class ToneStack
{
public:
    ToneStack() {}
    ~ToneStack() {}

    void Init(float sample_rate);

    float Process(const float &in);

    inline void SetBass(const float &bass)
    {
        bass_ = bass;
        RecalculateCoefficients();
    }

    inline void SetMiddle(const float &middle)
    {
        middle_ = middle;
        RecalculateCoefficients();
    }

    inline void SetTreble(const float &treble)
    {
        treble_ = treble;
        RecalculateCoefficients();
    }

    inline float GetBass() const { return bass_; }
    inline float GetMiddle() const { return middle_; }
    inline float GetTreble() const { return treble_; }

private:
    float sample_rate_;
    float bass_;
    float middle_;
    float treble_;

    float bass_gain_;
    float middle_gain_;
    float treble_gain_;

    void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_TONESTACK_H
