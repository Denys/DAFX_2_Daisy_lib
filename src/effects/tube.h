// # Tube
// Tube distortion simulation with asymmetrical waveshaping
//
// Ported from DAFX book tube.m by Bendiksen, Dutilleux, Zölzer
// Implements non-linear waveshaping characteristic of tube amplifiers
//
// ~~~~
// void Init(float sample_rate);
// float Process(float in);
// void SetDrive(float drive);
// void SetBias(float bias);
// void SetDistortion(float dist);
// void SetHighPassPole(float rh);
// void SetLowPassPole(float rl);
// void SetMix(float mix);
// ~~~~
#pragma once
#ifndef DSY_TUBE_H
#define DSY_TUBE_H

namespace daisysp
{
class Tube
{
public:
    Tube() {}
    ~Tube() {}

    void Init(float sample_rate);

    float Process(float in);

    inline void SetDrive(float drive) { drive_ = drive; }
    inline void SetBias(float bias) { bias_ = bias; }
    inline void SetDistortion(float dist) { dist_ = dist; }
    inline void SetHighPassPole(float rh) { rh_ = rh; }
    inline void SetLowPassPole(float rl) { rl_ = rl; }
    inline void SetMix(float mix) { mix_ = mix; }

private:
    float drive_;
    float bias_;
    float dist_;
    float rh_;
    float rl_;
    float mix_;

    float hp_xnm1_;
    float hp_xnm2_;
    float hp_ynm1_;
    float hp_ynm2_;
    float lp_xnm1_;
    float lp_ynm1_;

    float ProcessWaveshaper(float in);
    float ProcessHighPass(float in);
    float ProcessLowPass(float in);
};

} // namespace daisysp

#endif // DSY_TUBE_H
