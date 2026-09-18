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
  /** Largest width this class will accept, in seconds - the top of the range
   *  the header documents. It is an upper bound on what Init() may be asked
   *  to reserve, not what it reserves by default. */
  static constexpr float kMaxWidthSeconds = 0.1f;

  /** Width reserved by Init() when the caller does not ask for more.
   *
   *  The delay line is sized once, at Init(), because SetWidth() is callable
   *  from the audio thread and must never reallocate. That makes the reserved
   *  width a memory decision rather than a tuning one, so the caller makes it.
   *
   *  The structure this port uses costs `2 + 3 * width * fs` floats, so
   *  reserving the full kMaxWidthSeconds would take 14402 floats (~57.6 kB)
   *  at 48 kHz and ~115 kB at 96 kHz - past the "< 50 KB" per-effect target
   *  in README.md, at both of the rates that README supports.
   *
   *  The default reserves 20 ms: 2882 floats (~11.3 kB) at 48 kHz and 5762
   *  floats (~22.5 kB) at 96 kHz, both inside that target. It covers the
   *  ordinary vibrato and chorus range, including the 10 ms that
   *  tests/test_vibrato.cpp exercises. Pass a larger value to Init() for
   *  wider settings; SetWidth() clamps to whatever was reserved, and
   *  GetWidth() reports the clamped value. */
  static constexpr float kDefaultReservedWidthSeconds = 0.02f;

  /** Highest sample rate Init() will honour. Above this the delay-line length
   *  computed from kMaxWidthSeconds overflows the int it is held in - the
   *  float-to-int conversion itself is undefined - and the allocation is
   *  absurd well before that. 768 kHz is the top of the audio range; the
   *  buffer there is 230402 floats (~922 kB). */
  static constexpr float kMaxSampleRate = 768000.0f;

  Vibrato()
      : sample_rate_(48000.0f), freq_(5.0f), width_(0.005f),
        reserved_width_(kDefaultReservedWidthSeconds), delay_samples_(0),
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
        width_(other.width_), reserved_width_(other.reserved_width_),
        delay_samples_(other.delay_samples_),
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
      reserved_width_ = other.reserved_width_;
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

  /** @param sample_rate  Sample rate in Hz, clamped to kMaxSampleRate.
   *  @param max_width_seconds  Width to reserve the delay line for, clamped
   *         to [0, kMaxWidthSeconds]. SetWidth() is limited to this value.
   *  Re-initialising is safe: the previous allocation is released, and a
   *  failed allocation leaves the object exactly as it was. */
  void Init(float sample_rate,
            float max_width_seconds = kDefaultReservedWidthSeconds);

  /** Width the delay line was sized for, in seconds. */
  inline float GetReservedWidth() const { return reserved_width_; }

  float Process(const float &in);

  inline void SetFrequency(const float &freq) {
    freq_ = freq;
    RecalculateCoefficients();
  }

  /** Width in seconds. Values outside [0, GetReservedWidth()] are clamped, so
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
  float reserved_width_; // what Init() sized the delay line for

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
