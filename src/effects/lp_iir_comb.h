// # Low-Pass IIR Comb Filter
// Damped comb filter with low-pass filter in the feedback loop
//
// Ported from DAFX book lpiircomb.m by P. Dutilleux, U Zölzer
// Implements a comb filter with a 1st-order low-pass filter in the
// feedback path for frequency-dependent decay (damping).
//
// ## Filter Structure
// y_h[n] = b0 * x[n-M] + b1 * x_hold - a1 * y_hold
// y[n] = x[n] + g * y_h[n]
//
// The LP filter in the feedback creates high-frequency damping,
// simulating the absorption characteristics of real acoustic spaces.
//
// ## Parameters
// - delay_samples: Delay length M in samples
// - feedback: Overall feedback gain g (0 to 0.999)
// - damping: Low-pass filter coefficient (0 = no damping, 1 = max damping)
//
// ## Example
// ~~~~
// LPIIRComb<4096> comb;
// comb.Init(48000.0f);
// comb.SetDelay(2400);   // 50ms at 48kHz
// comb.SetFeedback(0.8f);
// comb.SetDamping(0.3f);  // Slight high-frequency damping
// float out = comb.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2 and 5
// - Original MATLAB: lpiircomb.m
#pragma once
#ifndef DSY_LP_IIR_COMB_H
#define DSY_LP_IIR_COMB_H

#include <cmath>
#include <cstring>

namespace daisysp {

/**
 * @brief Low-Pass IIR Comb Filter
 *
 * Comb filter with damping for reverb and physical modeling applications.
 * The low-pass filter in the feedback loop creates natural high-frequency
 * decay similar to acoustic absorption.
 *
 * @tparam MaxDelay Maximum delay length in samples
 */
template <size_t MaxDelay = 4096> class LPIIRComb {
public:
  LPIIRComb()
      : sample_rate_(48000.0f), delay_samples_(100), feedback_(0.7f),
        damping_(0.3f), b0_(0.5f), b1_(0.5f), a1_(0.0f), x_hold_(0.0f),
        y_hold_(0.0f), write_ptr_(0) {}

  /**
   * @brief Initialize the LP-IIR comb filter
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Clear delay buffer
    std::memset(delay_buffer_, 0, sizeof(delay_buffer_));

    // Reset filter state
    x_hold_ = 0.0f;
    y_hold_ = 0.0f;
    write_ptr_ = 0;

    // Calculate coefficients
    RecalculateCoefficients();
  }

  /**
   * @brief Process a single sample
   *
   * @param in Input sample
   * @return Processed output sample
   */
  float Process(float in) {
    // Read from delay line
    size_t read_ptr = (write_ptr_ + MaxDelay - delay_samples_) % MaxDelay;
    float delayed = delay_buffer_[read_ptr];

    // 1st-order low-pass filter in feedback path
    // y_h[n] = b0 * x[n-M] + b1 * x_hold - a1 * y_hold
    float y_h = b0_ * delayed + b1_ * x_hold_ - a1_ * y_hold_;

    // Update LP filter state
    y_hold_ = y_h;
    x_hold_ = delayed;

    // Comb filter output
    float out = in + feedback_ * y_h;

    // Write to delay line
    delay_buffer_[write_ptr_] = out;
    write_ptr_ = (write_ptr_ + 1) % MaxDelay;

    return out;
  }

  /**
   * @brief Process with fractional delay (linear interpolation)
   *
   * @param in Input sample
   * @return Processed output sample
   */
  float ProcessFractional(float in) {
    // Calculate fractional read position
    float read_pos = static_cast<float>(write_ptr_) +
                     static_cast<float>(MaxDelay) - delay_frac_;
    while (read_pos >= static_cast<float>(MaxDelay)) {
      read_pos -= static_cast<float>(MaxDelay);
    }

    // Get integer and fractional parts
    size_t read_int = static_cast<size_t>(read_pos);
    float frac = read_pos - static_cast<float>(read_int);
    size_t read_next = (read_int + 1) % MaxDelay;

    // Linear interpolation
    float delayed = delay_buffer_[read_int] * (1.0f - frac) +
                    delay_buffer_[read_next] * frac;

    // 1st-order low-pass filter
    float y_h = b0_ * delayed + b1_ * x_hold_ - a1_ * y_hold_;
    y_hold_ = y_h;
    x_hold_ = delayed;

    // Comb filter output
    float out = in + feedback_ * y_h;

    // Write to delay line
    delay_buffer_[write_ptr_] = out;
    write_ptr_ = (write_ptr_ + 1) % MaxDelay;

    return out;
  }

  // Parameter setters
  inline void SetDelay(size_t samples) {
    delay_samples_ = (samples > MaxDelay - 1) ? MaxDelay - 1 : samples;
    delay_frac_ = static_cast<float>(delay_samples_);
  }

  inline void SetDelayMs(float ms) {
    float samples = ms * sample_rate_ / 1000.0f;
    delay_frac_ = samples;
    delay_samples_ = static_cast<size_t>(samples);
    if (delay_samples_ > MaxDelay - 1) {
      delay_samples_ = MaxDelay - 1;
      delay_frac_ = static_cast<float>(MaxDelay - 1);
    }
  }

  inline void SetFeedback(float fb) {
    feedback_ = (fb < -0.999f) ? -0.999f : (fb > 0.999f) ? 0.999f : fb;
  }

  inline void SetDamping(float damping) {
    damping_ = (damping < 0.0f) ? 0.0f : (damping > 0.999f) ? 0.999f : damping;
    RecalculateCoefficients();
  }

  /**
   * @brief Configure for reverb with specific RT60
   *
   * @param rt60 Desired reverb time in seconds
   * @param delay_ms Delay time in milliseconds
   */
  void SetReverbTime(float rt60, float delay_ms) {
    SetDelayMs(delay_ms);

    // Calculate feedback for desired RT60
    // RT60 = -3 * delay / log10(g)
    // g = 10^(-3 * delay / RT60)
    float delay_sec = delay_ms / 1000.0f;
    float g = std::pow(10.0f, -3.0f * delay_sec / rt60);
    SetFeedback(g);
  }

  // Parameter getters
  inline size_t GetDelay() const { return delay_samples_; }
  inline float GetFeedback() const { return feedback_; }
  inline float GetDamping() const { return damping_; }
  inline float GetSampleRate() const { return sample_rate_; }

  // Clear the delay buffer
  inline void Clear() {
    std::memset(delay_buffer_, 0, sizeof(delay_buffer_));
    x_hold_ = 0.0f;
    y_hold_ = 0.0f;
  }

private:
  float sample_rate_;
  size_t delay_samples_;
  float delay_frac_;
  float feedback_;
  float damping_;

  // LP filter coefficients
  float b0_;
  float b1_;
  float a1_;

  // LP filter state
  float x_hold_;
  float y_hold_;

  // Delay line
  float delay_buffer_[MaxDelay];
  size_t write_ptr_;

  void RecalculateCoefficients() {
    // Simple 1-pole LP: damping controls the mix
    // damping = 0: no filtering (b0=1, b1=0)
    // damping = 1: maximum filtering
    b0_ = 1.0f - damping_;
    b1_ = damping_;
    a1_ = 0.0f; // Can be extended for more complex responses
  }
};

// Common type aliases for reverb applications
using LPComb2K = LPIIRComb<2048>;
using LPComb4K = LPIIRComb<4096>;
using LPComb8K = LPIIRComb<8192>;

} // namespace daisysp

#endif // DSY_LP_IIR_COMB_H
