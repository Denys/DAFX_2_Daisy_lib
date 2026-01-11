// # EnvelopeFollower
// Envelope detector with configurable attack and release
//
// Provides peak and RMS envelope detection for dynamics processing.
// Used by noise gates, compressors, expanders, and other dynamics effects.
//
// ## Parameters
// - attack_time: Attack time in seconds (0.0001-1.0 s, default 0.01 s)
// - release_time: Release time in seconds (0.001-5.0 s, default 0.1 s)
// - mode: Detection mode (Peak or RMS)
//
// ## Example
// ~~~~
// EnvelopeFollower env;
// env.Init(48000.0f);
// env.SetAttackTime(0.01f);   // 10 ms attack
// env.SetReleaseTime(0.1f);   // 100 ms release
// float envelope = env.Process(input_sample);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 4, Section 4.2 (Dynamics Processing)
#pragma once
#ifndef DSY_ENVELOPEFOLLOWER_H
#define DSY_ENVELOPEFOLLOWER_H

#include <cmath>

namespace daisysp {

/**
 * @brief Envelope detection mode
 */
enum class EnvelopeMode {
  Peak, ///< Peak detection (fast response)
  RMS   ///< RMS detection (smoother, power-based)
};

/**
 * @brief Envelope follower with attack/release control
 *
 * Tracks the amplitude envelope of an audio signal using
 * configurable attack and release coefficients.
 */
class EnvelopeFollower {
public:
  EnvelopeFollower() {}
  ~EnvelopeFollower() {}

  /**
   * @brief Initialize the envelope follower
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    envelope_ = 0.0f;
    rms_accumulator_ = 0.0f;
    mode_ = EnvelopeMode::Peak;

    SetAttackTime(0.01f); // 10 ms default
    SetReleaseTime(0.1f); // 100 ms default
  }

  /**
   * @brief Process a sample and return envelope value
   * @param in Input sample
   * @return Envelope value (0.0 to 1.0+)
   */
  float Process(float in) {
    float input_level;

    if (mode_ == EnvelopeMode::Peak) {
      // Peak detection - absolute value
      input_level = std::fabs(in);
    } else {
      // RMS detection - square and smooth
      input_level = in * in;
    }

    // Apply attack or release coefficient
    if (input_level > envelope_) {
      // Attack phase - signal is rising
      envelope_ =
          attack_coeff_ * envelope_ + (1.0f - attack_coeff_) * input_level;
    } else {
      // Release phase - signal is falling
      envelope_ =
          release_coeff_ * envelope_ + (1.0f - release_coeff_) * input_level;
    }

    // For RMS mode, take square root of the result
    if (mode_ == EnvelopeMode::RMS) {
      return std::sqrt(envelope_);
    }

    return envelope_;
  }

  /**
   * @brief Process and return envelope in dB
   * @param in Input sample
   * @return Envelope value in dB
   */
  float ProcessDB(float in) {
    float env = Process(in);

    // Prevent log(0)
    if (env < 1e-10f)
      return -100.0f;

    return 20.0f * std::log10(env);
  }

  /**
   * @brief Set attack time
   * @param attack_time Attack time in seconds
   */
  void SetAttackTime(float attack_time) {
    attack_time_ = attack_time;
    // Time constant: time to reach ~63% of target
    attack_coeff_ = std::exp(-1.0f / (attack_time_ * sample_rate_));
  }

  /**
   * @brief Set release time
   * @param release_time Release time in seconds
   */
  void SetReleaseTime(float release_time) {
    release_time_ = release_time;
    release_coeff_ = std::exp(-1.0f / (release_time_ * sample_rate_));
  }

  /**
   * @brief Set detection mode
   * @param mode Peak or RMS
   */
  void SetMode(EnvelopeMode mode) { mode_ = mode; }

  /**
   * @brief Reset envelope to zero
   */
  void Reset() {
    envelope_ = 0.0f;
    rms_accumulator_ = 0.0f;
  }

  // Getters
  inline float GetAttackTime() const { return attack_time_; }
  inline float GetReleaseTime() const { return release_time_; }
  inline float GetEnvelope() const { return envelope_; }
  inline EnvelopeMode GetMode() const { return mode_; }

private:
  float sample_rate_;
  float attack_time_;
  float release_time_;
  float attack_coeff_;
  float release_coeff_;
  float envelope_;
  float rms_accumulator_;
  EnvelopeMode mode_;
};

/**
 * @brief Simple one-pole envelope follower
 *
 * Simplified version with single smoothing coefficient.
 * Useful when separate attack/release is not needed.
 */
class SimpleEnvelopeFollower {
public:
  SimpleEnvelopeFollower() : coeff_(0.99f), envelope_(0.0f) {}

  void Init(float sample_rate, float smoothing_time = 0.01f) {
    coeff_ = std::exp(-1.0f / (smoothing_time * sample_rate));
    envelope_ = 0.0f;
  }

  inline float Process(float in) {
    float abs_in = std::fabs(in);
    envelope_ = coeff_ * envelope_ + (1.0f - coeff_) * abs_in;
    return envelope_;
  }

  void SetCoefficient(float coeff) { coeff_ = coeff; }

  inline float GetEnvelope() const { return envelope_; }

private:
  float coeff_;
  float envelope_;
};

} // namespace daisysp

#endif // DSY_ENVELOPEFOLLOWER_H
