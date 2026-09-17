// # Universal Comb Filter
// Configurable comb filter with feed-forward, feed-back, and blend controls
//
// Ported from DAFX book unicomb.m by P. Dutilleux, U Zölzer
// Implements a universal comb filter structure that can create
// flanger, chorus, and other delay-based effects.
//
// ## Filter Structure
// y[n] = FF * x[n-M] + BL * x[n] + FB * y[n-M]
//
// Where:
// - FF: Feed-forward coefficient (delay line to output)
// - BL: Blend coefficient (dry signal to output)
// - FB: Feedback coefficient (output to delay line)
// - M: Delay length in samples
//
// ## Parameters
// - delay_samples: Delay length M in samples
// - feedback: FB coefficient (-1 to 1)
// - feedforward: FF coefficient (-1 to 1)
// - blend: BL coefficient (0 to 1)
//
// ## Example
// ~~~~
// UniversalComb<2048> comb;
// comb.Init(48000.0f);
// comb.SetDelay(480);  // 10ms at 48kHz
// comb.SetFeedback(-0.5f);
// comb.SetFeedforward(1.0f);
// comb.SetBlend(0.5f);
// float out = comb.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 2
// - Original MATLAB: unicomb.m
#pragma once
#ifndef DSY_UNIVERSAL_COMB_H
#define DSY_UNIVERSAL_COMB_H

#include <cmath>
#include <cstring>

namespace daisysp {

/**
 * @brief Universal Comb Filter
 *
 * Flexible comb filter structure supporting various delay-based effects.
 * Can be configured as FIR comb, IIR comb, allpass, or combinations.
 *
 * @tparam MaxDelay Maximum delay length in samples
 */
template <size_t MaxDelay = 2048> class UniversalComb {
public:
  static_assert(MaxDelay >= 2, "MaxDelay must leave room for a delay tap");

  // The nominal default is 10 samples, but a small instantiation cannot hold
  // it. An out-of-range default made ProcessFractional() compute a negative
  // read position and cast it to size_t, which is undefined.
  static constexpr size_t kDefaultDelay = (MaxDelay > 10) ? 10 : MaxDelay - 1;

  UniversalComb()
      : sample_rate_(48000.0f), delay_samples_(kDefaultDelay),
        delay_frac_(static_cast<float>(kDefaultDelay)), feedback_(0.0f),
        feedforward_(1.0f), blend_(0.5f), write_ptr_(0) {}

  /**
   * @brief Initialize the comb filter
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // delay_frac_ is deliberately not touched here. The constructor
    // initialises it, so Init() does not need to, and assigning
    // static_cast<float>(delay_samples_) would silently truncate a delay
    // configured through SetDelayFractional() or SetDelayMs().

    // Clear delay buffer
    std::memset(delay_buffer_, 0, sizeof(delay_buffer_));

    // Reset state
    write_ptr_ = 0;
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

    // Calculate input to delay line (with feedback)
    float xh = in + feedback_ * delayed;

    // Calculate output (feedforward + blend)
    float out = feedforward_ * delayed + blend_ * xh;

    // Write to delay line
    delay_buffer_[write_ptr_] = xh;
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

    // The cast below is undefined unless read_pos is in [0, MaxDelay). NaN
    // fails this test, and so does a negative value - which is finite, so a
    // finiteness check alone let it through to a cast of -6 on a small
    // MaxDelay. The setters reject the bad inputs that are known to reach
    // here; this covers the ones that are not.
    if (!(read_pos >= 0.0f && read_pos < static_cast<float>(MaxDelay))) {
      read_pos = 0.0f;
    }

    // Get integer and fractional parts
    size_t read_int = static_cast<size_t>(read_pos);
    float frac = read_pos - static_cast<float>(read_int);
    size_t read_next = (read_int + 1) % MaxDelay;

    // Linear interpolation
    float delayed = delay_buffer_[read_int] * (1.0f - frac) +
                    delay_buffer_[read_next] * frac;

    // Calculate input to delay line
    float xh = in + feedback_ * delayed;

    // Calculate output
    float out = feedforward_ * delayed + blend_ * xh;

    // Write to delay line
    delay_buffer_[write_ptr_] = xh;
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
    // Clamp before the cast, not after: converting NaN, an infinity or a
    // negative to size_t is undefined, and no later clamp can undo that.
    if (!std::isfinite(samples) || samples < 0.0f) {
      samples = 0.0f;
    }
    if (samples > static_cast<float>(MaxDelay - 1)) {
      samples = static_cast<float>(MaxDelay - 1);
    }
    delay_frac_ = samples;
    delay_samples_ = static_cast<size_t>(samples);
  }

  inline void SetDelayFractional(float samples) {
    // NaN fails every comparison, so the upper clamp below would let it
    // through to a cast that is undefined. Reject it first.
    if (!std::isfinite(samples) || samples < 0.0f) {
      samples = 0.0f;
    }
    delay_frac_ = (samples > static_cast<float>(MaxDelay - 1))
                      ? static_cast<float>(MaxDelay - 1)
                      : samples;
    delay_samples_ = static_cast<size_t>(delay_frac_);
  }

  inline void SetFeedback(float fb) {
    // Clamp to stable range
    feedback_ = (fb < -0.999f) ? -0.999f : (fb > 0.999f) ? 0.999f : fb;
  }

  inline void SetFeedforward(float ff) { feedforward_ = ff; }

  inline void SetBlend(float bl) { blend_ = bl; }

  // Preset configurations
  inline void SetFIRComb() {
    // Pure feedforward comb (FIR)
    feedback_ = 0.0f;
    feedforward_ = 1.0f;
    blend_ = 1.0f;
  }

  inline void SetIIRComb(float fb = 0.7f) {
    // Pure feedback comb (IIR)
    feedback_ = fb;
    feedforward_ = 0.0f;
    blend_ = 1.0f;
  }

  inline void SetAllpass(float g = 0.7f) {
    // Allpass configuration
    feedback_ = g;
    feedforward_ = -g;
    blend_ = 1.0f;
  }

  inline void SetFlanger(float depth = 0.7f) {
    // Typical flanger settings
    feedback_ = depth;
    feedforward_ = 1.0f;
    blend_ = 0.5f;
  }

  // Parameter getters
  inline size_t GetDelay() const { return delay_samples_; }
  inline float GetDelayFractional() const { return delay_frac_; }
  inline float GetFeedback() const { return feedback_; }
  inline float GetFeedforward() const { return feedforward_; }
  inline float GetBlend() const { return blend_; }
  inline float GetSampleRate() const { return sample_rate_; }

  // Clear the delay buffer
  inline void Clear() { std::memset(delay_buffer_, 0, sizeof(delay_buffer_)); }

private:
  float sample_rate_;
  size_t delay_samples_;
  float delay_frac_;
  float feedback_;
  float feedforward_;
  float blend_;

  // Zero-initialised so that a default-constructed instance can be
  // processed without first calling Init(); reading it otherwise is
  // undefined, whatever the index arithmetic does.
  float delay_buffer_[MaxDelay] = {};
  size_t write_ptr_;
};

// Common type aliases
using Comb1K = UniversalComb<1024>;
using Comb2K = UniversalComb<2048>;
using Comb4K = UniversalComb<4096>;

} // namespace daisysp

#endif // DSY_UNIVERSAL_COMB_H
