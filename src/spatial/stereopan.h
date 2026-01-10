// # StereoPan
// Stereo panning with tangent law
//
// Ported from DAFX book stereopan.m by V. Pulkki, T. Lokki
// Implements stereo panning using tangent law for natural sound distribution
// Suitable for positioning mono signals in stereo field
//
// ## Parameters
// - pan: Panning position (-1 to +1, default 0)
//   -1 = full left, 0 = center, +1 = full right
// - speaker_angle: Loudspeaker base angle in degrees (0-60, default 30)
//
// ## Example
// ~~~~
// StereoPan pan;
// pan.Init();
// pan.SetPan(0.5f);  // Slightly right
// pan.SetSpeakerAngle(30.0f);  // 30-degree speaker spread
// float left, right;
// pan.Process(in, &left, &right);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 5, Section 5.1
// - Original MATLAB: stereopan.m
#pragma once
#ifndef DSY_STEREOPAN_H
#define DSY_STEREOPAN_H

#include <cmath>

namespace daisysp
{
class StereoPan
{
public:
    StereoPan() {}
    ~StereoPan() {}

    void Init();

    void Process(const float &in, float *out_left, float *out_right);

    inline void SetPan(const float &pan)
    {
        pan_ = pan;
        RecalculateGains();
    }

    inline void SetSpeakerAngle(const float &angle)
    {
        speaker_angle_ = angle;
        RecalculateGains();
    }

    inline float GetPan() const { return pan_; }
    inline float GetSpeakerAngle() const { return speaker_angle_; }

private:
    float pan_;
    float speaker_angle_;
    float gain_left_;
    float gain_right_;

    void RecalculateGains();
};

} // namespace daisysp

#endif // DSY_STEREOPAN_H
