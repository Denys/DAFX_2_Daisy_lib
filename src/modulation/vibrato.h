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
  /** Largest width the documented parameter range admits, in seconds.
   *  Init() allocates for this so that SetWidth() never has to reallocate:
   *  the setter is callable from the audio thread, and the delay line must
   *  already be large enough for any width the header promises to accept.
   *  Cost at 48 kHz is 2 + 4800 + 2*4800 = 14402 floats (~57.6 kB); at
   *  96 kHz, ~115 kB. */
  static constexpr float kMaxWidthSeconds = 0.1f;

  /** Highest sample rate Init() will honour. Above this the delay-line length
   *  computed from kMaxWidthSeconds overflows the int it is held in - the
   *  float-to-int conversion itself is undefined - and the allocation is
   *  absurd well before that. 768 kHz is the top of the audio range; the
   *  buffer there is 230402 floats (~922 kB). */
  static constexpr float kMaxSampleRate = 768000.0f;

  Vibrato()
      : sample_rate_(48000.0f), freq_(5.0f), width_(0.005f), delay_samples_(0),
        width_samples_(0), mod_freq_samples_(0.0f), delay_line_size_(2),
        delay_line_capacity_(0), delay_line_(nullptr), write_ptr_(0) {}

  ~Vibrato() {
    delete[] delay_line_;
    delay_line_ = nullptr;
  }

  // The delay line is a raw owning allocation. Compiler-generated copies would
  // give two owners of one buffer and double-free on the second destruction.
  Vibrato(const Vibrato &) = delete;
  Vibrato &operator=(const Vibrato &) = delete;

  Vibrato(Vibrato &&other) noexcept
      : sample_rate_(other.sample_rate_), freq_(other.freq_),
        width_(other.width_), delay_samples_(other.delay_samples_),
        width_samples_(other.width_samples_),
        mod_freq_samples_(other.mod_freq_samples_),
        delay_line_size_(other.delay_line_size_),
        delay_line_capacity_(other.delay_line_capacity_),
        delay_line_(other.delay_line_), write_ptr_(other.write_ptr_) {
    other.delay_line_ = nullptr;
    other.delay_line_capacity_ = 0;
  }

  Vibrato &operator=(Vibrato &&other) noexcept {
    if (this != &other) {
      delete[] delay_line_;
      sample_rate_ = other.sample_rate_;
      freq_ = other.freq_;
      width_ = other.width_;
      delay_samples_ = other.delay_samples_;
      width_samples_ = other.width_samples_;
      mod_freq_samples_ = other.mod_freq_samples_;
      delay_line_size_ = other.delay_line_size_;
      delay_line_capacity_ = other.delay_line_capacity_;
      delay_line_ = other.delay_line_;
      write_ptr_ = other.write_ptr_;
      other.delay_line_ = nullptr;
      other.delay_line_capacity_ = 0;
    }
    return *this;
  }

  void Init(float sample_rate);

  float Process(const float &in);

  inline void SetFrequency(const float &freq) {
    freq_ = freq;
    RecalculateCoefficients();
  }

  /** Width in seconds. Values outside [0, kMaxWidthSeconds] are clamped, so
   *  the delay line allocated by Init() always covers the resulting size.
   *  GetWidth() reports the clamped value. */
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
  int delay_line_size_;     // logical length in use, <= delay_line_capacity_
  int delay_line_capacity_; // allocated length, sized for kMaxWidthSeconds
  float *delay_line_;
  int write_ptr_;

  void RecalculateCoefficients();
};

} // namespace daisysp

#endif // DSY_VIBRATO_H
