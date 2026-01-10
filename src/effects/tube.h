// # Tube
// Tube distortion simulation with asymmetrical waveshaping
//
// Ported from DAFX book tube.m by Bendiksen, Dutilleux, Zölzer
// Implements non-linear waveshaping characteristic of tube amplifiers
//
// ~~~~
// void Init(float sample_rate);
// float Process(const float &in);
// void SetDrive(const float &drive);
// void SetBias(const float &bias);
// void SetDistortion(const float &dist);
// void SetHighPassPole(const float &rh);
// void SetLowPassPole(const float &rl);
// void SetMix(const float &mix);
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

    float Process(const float &in);

    inline void SetDrive(const float &drive) { drive_ = drive; }
    inline void SetBias(const float &bias) { bias_ = bias; }
    inline void SetDistortion(const float &dist) { dist_ = dist; }
    inline void SetHighPassPole(const float &rh) { rh_ = rh; }
    inline void SetLowPassPole(const float &rl) { rl_ = rl; }
    inline void SetMix(const float &mix) { mix_ = mix; }

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

    float ProcessWaveshaper(const float &in);
    float ProcessHighPass(const float &in);
    float ProcessLowPass(const float &in);
};

} // namespace daisysp

#endif // DSY_TUBE_H
