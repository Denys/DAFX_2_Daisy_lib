// # Compressor/Expander
// Dynamic range processing with compression and expansion
//
// Ported from DAFX book compexp.m by M. Holters
// Implements a combined compressor/expander with RMS detection,
// lookahead delay, and smooth attack/release curves.
//
// ## Parameters
// - comp_threshold: Compression threshold in dB (default -20 dB)
// - comp_slope: Compression ratio as slope (e.g., 0.5 = 2:1, default 0.5)
// - exp_threshold: Expansion threshold in dB (default -40 dB)
// - exp_slope: Expansion ratio as slope (e.g., 2.0 = 1:2, default 2.0)
// - attack_time: Attack time in seconds (default 0.03 s)
// - release_time: Release time in seconds (default 0.003 s)
// - lookahead: Lookahead delay in samples (default 150)
//
// ## Example
// ~~~~
// CompressorExpander<256> compexp;
// compexp.Init(48000.0f);
// compexp.SetCompThreshold(-20.0f);
// compexp.SetCompSlope(0.5f);  // 2:1 compression
// compexp.SetExpThreshold(-40.0f);
// compexp.SetExpSlope(2.0f);   // 1:2 expansion
// float out = compexp.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 4
// - Original MATLAB: compexp.m
#pragma once
#ifndef DSY_COMPRESSOR_EXPANDER_H
#define DSY_COMPRESSOR_EXPANDER_H

#include <cmath>
#include <cstring>

namespace daisysp {

/**
 * @brief Combined Compressor/Expander dynamics processor
 *
 * Implements RMS-based level detection with configurable
 * compression and expansion thresholds and ratios.
 *
 * @tparam MaxDelay Maximum lookahead delay in samples (default 256)
 */
template <size_t MaxDelay = 256> class CompressorExpander {
public:
  CompressorExpander()
      : sample_rate_(48000.0f), comp_threshold_(-20.0f), comp_slope_(0.5f),
        exp_threshold_(-40.0f), exp_slope_(2.0f), attack_time_(0.03f),
        release_time_(0.003f), lookahead_(150), tav_(0.01f), xrms_(0.0f),
        gain_(1.0f), write_ptr_(0) {}

  /**
   * @brief Initialize the compressor/expander
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Clear delay buffer
    std::memset(delay_buffer_, 0, sizeof(delay_buffer_));

    // Reset state
    xrms_ = 0.0f;
    gain_ = 1.0f;
    write_ptr_ = 0;

    // Recalculate coefficients
    RecalculateCoefficients();
  }

  /**
   * @brief Process a single sample
   *
   * @param in Input sample
   * @return Processed output sample
   */
  float Process(float in) {
    // RMS level detection (single-pole IIR)
    xrms_ = (1.0f - tav_) * xrms_ + tav_ * (in * in);

    // Convert to dB (with floor to avoid log(0))
    float x_db = 10.0f * std::log10(xrms_ + 1e-20f);

    // Gain computer: G = min(0, CS*(CT-X), ES*(ET-X))
    float comp_gain = comp_slope_ * (comp_threshold_ - x_db);
    float exp_gain = exp_slope_ * (exp_threshold_ - x_db);
    float target_gain_db = std::min(0.0f, std::min(comp_gain, exp_gain));

    // Convert target gain to linear
    float target_gain = std::pow(10.0f, target_gain_db / 20.0f);

    // Smooth gain with attack/release
    float coeff;
    if (target_gain < gain_) {
      coeff = attack_coeff_; // Attacking (reducing gain)
    } else {
      coeff = release_coeff_; // Releasing (increasing gain)
    }
    gain_ = (1.0f - coeff) * gain_ + coeff * target_gain;

    // Read from lookahead delay buffer
    size_t read_ptr = (write_ptr_ + MaxDelay - lookahead_) % MaxDelay;
    float delayed = delay_buffer_[read_ptr];

    // Write to delay buffer
    delay_buffer_[write_ptr_] = in;
    write_ptr_ = (write_ptr_ + 1) % MaxDelay;

    // Apply gain
    return gain_ * delayed;
  }

  // Parameter setters
  inline void SetCompThreshold(float threshold_db) {
    comp_threshold_ = threshold_db;
  }

  inline void SetCompSlope(float slope) {
    // Clamp slope to valid range (0 = ∞:1, 1 = 1:1)
    comp_slope_ = (slope < 0.0f) ? 0.0f : (slope > 1.0f) ? 1.0f : slope;
  }

  inline void SetCompRatio(float ratio) {
    // Convert ratio (e.g., 4:1) to slope (e.g., 0.75)
    if (ratio <= 1.0f) {
      comp_slope_ = 1.0f; // No compression
    } else {
      comp_slope_ = 1.0f - (1.0f / ratio);
    }
  }

  inline void SetExpThreshold(float threshold_db) {
    exp_threshold_ = threshold_db;
  }

  inline void SetExpSlope(float slope) {
    // Clamp slope to valid range (1 = 1:1, higher = more expansion)
    exp_slope_ = (slope < 1.0f) ? 1.0f : slope;
  }

  inline void SetExpRatio(float ratio) {
    // Convert ratio (e.g., 1:2) to slope (e.g., 2.0)
    exp_slope_ = (ratio < 1.0f) ? 1.0f : ratio;
  }

  inline void SetAttackTime(float time_sec) {
    attack_time_ = time_sec;
    RecalculateCoefficients();
  }

  inline void SetReleaseTime(float time_sec) {
    release_time_ = time_sec;
    RecalculateCoefficients();
  }

  inline void SetLookahead(size_t samples) {
    lookahead_ = (samples > MaxDelay) ? MaxDelay : samples;
  }

  inline void SetRmsTime(float time_sec) {
    tav_ = 1.0f / (sample_rate_ * time_sec);
    if (tav_ > 1.0f)
      tav_ = 1.0f;
  }

  // Parameter getters
  inline float GetCompThreshold() const { return comp_threshold_; }
  inline float GetCompSlope() const { return comp_slope_; }
  inline float GetExpThreshold() const { return exp_threshold_; }
  inline float GetExpSlope() const { return exp_slope_; }
  inline float GetAttackTime() const { return attack_time_; }
  inline float GetReleaseTime() const { return release_time_; }
  inline float GetCurrentGain() const { return gain_; }
  inline float GetCurrentGainDb() const {
    return 20.0f * std::log10(gain_ + 1e-20f);
  }
  inline float GetCurrentLevelDb() const {
    return 10.0f * std::log10(xrms_ + 1e-20f);
  }

private:
  float sample_rate_;
  float comp_threshold_;
  float comp_slope_;
  float exp_threshold_;
  float exp_slope_;
  float attack_time_;
  float release_time_;
  size_t lookahead_;

  float tav_;           // RMS averaging coefficient
  float attack_coeff_;  // Attack smoothing coefficient
  float release_coeff_; // Release smoothing coefficient

  float xrms_;                   // Running RMS estimate
  float gain_;                   // Current smoothed gain
  float delay_buffer_[MaxDelay]; // Lookahead delay buffer
  size_t write_ptr_;             // Delay buffer write position

  void RecalculateCoefficients() {
    // Convert times to one-pole coefficients
    attack_coeff_ = 1.0f - std::exp(-1.0f / (attack_time_ * sample_rate_));
    release_coeff_ = 1.0f - std::exp(-1.0f / (release_time_ * sample_rate_));

    // Default RMS time constant (~10ms)
    tav_ = 1.0f / (sample_rate_ * 0.01f);
  }
};

// Common type aliases
using CompExp = CompressorExpander<256>;
using CompExpLong = CompressorExpander<512>;

} // namespace daisysp

#endif // DSY_COMPRESSOR_EXPANDER_H
