// # Vibrato
// Vibrato effect using delay line modulation
//
// Ported from DAFX book vibrato.m by S. Disch
// Implements a vibrato effect using a modulated delay line with linear
// interpolation Suitable for pitch modulation and chorus-like effects
//
// ## Parameters
// - frequency: Modulation frequency in Hz (0.1-20 Hz, default 5 Hz)
// - width: Modulation depth in seconds (0.0001-0.1 s, default 0.005 s)
//
// ## Example
// ~~~~
// Vibrato vibrato;
// vibrato.Init(48000.0f);
// vibrato.SetFrequency(5.0f);  // 5 Hz modulation
// vibrato.SetWidth(0.005f);  // 5 ms depth
// float out = vibrato.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2, Section 2.4
// - Original MATLAB: vibrato.m
#pragma once
#ifndef DSY_VIBRATO_H
#define DSY_VIBRATO_H

#include <cmath>
#include <cstring>

namespace daisysp {
class Vibrato {
public:
  Vibrato() {}
  ~Vibrato() {
    if (delay_line_ != nullptr) {
      delete[] delay_line_;
      delay_line_ = nullptr;
    }
  }

  void Init(float sample_rate);

  float Process(const float &in);

  inline void SetFrequency(const float &freq) {
    freq_ = freq;
    RecalculateCoefficients();
  }

  inline void SetWidth(const float &width) {
    width_ = width;
    RecalculateCoefficients();
  }

  inline float GetFrequency() const { return freq_; }
  inline float GetWidth() const { return width_; }

private:
  float sample_rate_;
  float freq_;
  float width_;

  int delay_samples_;
  int width_samples_;
  float mod_freq_samples_;
  int delay_line_size_;
  float *delay_line_;
  int write_ptr_;

  void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_VIBRATO_H
