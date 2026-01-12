// # FDN Reverb (Feedback Delay Network)
// Multi-tap reverberator with feedback delay network architecture
//
// Ported from DAFX book delaynetwork.m by V. Pulkki, T. Lokki
// Implements a 4-channel feedback delay network with Hadamard matrix,
// prime-length delay lines, and optional low-pass damping.
//
// ## Architecture
// - 4 parallel delay lines with prime lengths
// - Hadamard feedback matrix (orthogonal, energy-preserving)
// - Input distribution: b = [1, 1, 1, 1]
// - Output mixing: c = [0.8, 0.8, 0.8, 0.8]
//
// ## Parameters
// - decay: Overall decay factor (0.9-0.999)
// - mix: Wet/dry mix (0-1)
// - delay_scale: Scale delay lengths for different room sizes
// - damping: High-frequency damping per delay line
//
// ## Example
// ~~~~
// FDNReverb<8192> reverb;
// reverb.Init(48000.0f);
// reverb.SetDecay(0.97f);
// reverb.SetMix(0.5f);
// float out = reverb.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 5
// - Original MATLAB: delaynetwork.m
#pragma once
#ifndef DSY_FDN_REVERB_H
#define DSY_FDN_REVERB_H

#include <cmath>
#include <cstring>

namespace daisysp {

/**
 * @brief Feedback Delay Network Reverb
 *
 * 4-channel FDN with Hadamard feedback matrix and configurable
 * delay lengths for natural-sounding reverberation.
 *
 * @tparam MaxDelay Maximum delay length per line in samples
 */
template <size_t MaxDelay = 8192> class FDNReverb {
public:
  static constexpr size_t NumLines = 4;

  FDNReverb()
      : sample_rate_(48000.0f), decay_(0.97f), mix_(0.5f), delay_scale_(1.0f),
        damping_(0.3f) {
    // Default prime delay lengths (in samples at 44.1kHz)
    // Approximately 3.4ms, 4.8ms, 6.0ms, 6.6ms
    base_delays_[0] = 149;
    base_delays_[1] = 211;
    base_delays_[2] = 263;
    base_delays_[3] = 293;

    // Input gains (equal distribution)
    for (size_t i = 0; i < NumLines; i++) {
      input_gains_[i] = 1.0f;
      output_gains_[i] = 0.8f;
    }
  }

  /**
   * @brief Initialize the FDN reverb
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Clear all delay buffers
    for (size_t i = 0; i < NumLines; i++) {
      std::memset(delay_lines_[i], 0, sizeof(delay_lines_[i]));
      lp_state_[i] = 0.0f;
      write_ptrs_[i] = 0;
    }

    // Scale delays for sample rate (base is 44.1kHz)
    RecalculateDelays();

    // Build feedback matrix
    BuildFeedbackMatrix();
  }

  /**
   * @brief Process a single sample
   *
   * @param in Input sample
   * @return Processed output sample (wet + dry mix)
   */
  float Process(float in) {
    // Read from all delay lines
    float tap[NumLines];
    for (size_t i = 0; i < NumLines; i++) {
      size_t read_ptr = (write_ptrs_[i] + MaxDelay - delays_[i]) % MaxDelay;
      tap[i] = delay_lines_[i][read_ptr];
    }

    // Calculate output (sum of tapped delays)
    float wet = 0.0f;
    for (size_t i = 0; i < NumLines; i++) {
      wet += output_gains_[i] * tap[i];
    }

    // Apply feedback matrix and write to delay lines
    for (size_t i = 0; i < NumLines; i++) {
      // Matrix multiplication: feedback = A * tap
      float feedback = 0.0f;
      for (size_t j = 0; j < NumLines; j++) {
        feedback += feedback_matrix_[i][j] * tap[j];
      }

      // Add input
      float new_sample = input_gains_[i] * in + feedback;

      // Apply damping (1-pole LP filter)
      if (damping_ > 0.0f) {
        lp_state_[i] = (1.0f - damping_) * new_sample + damping_ * lp_state_[i];
        new_sample = lp_state_[i];
      }

      // Write to delay line
      delay_lines_[i][write_ptrs_[i]] = new_sample;
      write_ptrs_[i] = (write_ptrs_[i] + 1) % MaxDelay;
    }

    // Mix wet and dry
    return (1.0f - mix_) * in + mix_ * wet;
  }

  /**
   * @brief Process stereo input
   *
   * @param in_l Left input
   * @param in_r Right input
   * @param out_l Left output pointer
   * @param out_r Right output pointer
   */
  void ProcessStereo(float in_l, float in_r, float *out_l, float *out_r) {
    // Feed L to delays 0,1 and R to delays 2,3
    float tap[NumLines];
    for (size_t i = 0; i < NumLines; i++) {
      size_t read_ptr = (write_ptrs_[i] + MaxDelay - delays_[i]) % MaxDelay;
      tap[i] = delay_lines_[i][read_ptr];
    }

    // Stereo output mixing
    float wet_l = output_gains_[0] * tap[0] + output_gains_[1] * tap[1];
    float wet_r = output_gains_[2] * tap[2] + output_gains_[3] * tap[3];

    // Feedback with matrix
    for (size_t i = 0; i < NumLines; i++) {
      float feedback = 0.0f;
      for (size_t j = 0; j < NumLines; j++) {
        feedback += feedback_matrix_[i][j] * tap[j];
      }

      float input = (i < 2) ? in_l : in_r;
      float new_sample = input_gains_[i] * input + feedback;

      if (damping_ > 0.0f) {
        lp_state_[i] = (1.0f - damping_) * new_sample + damping_ * lp_state_[i];
        new_sample = lp_state_[i];
      }

      delay_lines_[i][write_ptrs_[i]] = new_sample;
      write_ptrs_[i] = (write_ptrs_[i] + 1) % MaxDelay;
    }

    *out_l = (1.0f - mix_) * in_l + mix_ * wet_l;
    *out_r = (1.0f - mix_) * in_r + mix_ * wet_r;
  }

  // Parameter setters
  inline void SetDecay(float decay) {
    decay_ = (decay < 0.0f) ? 0.0f : (decay > 0.999f) ? 0.999f : decay;
    BuildFeedbackMatrix();
  }

  inline void SetMix(float mix) {
    mix_ = (mix < 0.0f) ? 0.0f : (mix > 1.0f) ? 1.0f : mix;
  }

  inline void SetDelayScale(float scale) {
    delay_scale_ = (scale < 0.1f) ? 0.1f : (scale > 4.0f) ? 4.0f : scale;
    RecalculateDelays();
  }

  inline void SetDamping(float damping) {
    damping_ = (damping < 0.0f) ? 0.0f : (damping > 0.99f) ? 0.99f : damping;
  }

  /**
   * @brief Set reverb time (RT60)
   *
   * @param rt60 Reverb time in seconds
   */
  void SetReverbTime(float rt60) {
    // Calculate decay from RT60 using average delay
    float avg_delay_sec = 0.0f;
    for (size_t i = 0; i < NumLines; i++) {
      avg_delay_sec += static_cast<float>(delays_[i]) / sample_rate_;
    }
    avg_delay_sec /= static_cast<float>(NumLines);

    // RT60 = -3 * T / log10(g^N) where N is number of iterations
    // Simplified: g = 10^(-3 * avg_delay / RT60)
    decay_ = std::pow(10.0f, -3.0f * avg_delay_sec / rt60);
    if (decay_ > 0.999f)
      decay_ = 0.999f;
    BuildFeedbackMatrix();
  }

  /**
   * @brief Set custom delay lengths
   *
   * @param d0, d1, d2, d3 Delay lengths in samples
   */
  void SetDelays(size_t d0, size_t d1, size_t d2, size_t d3) {
    base_delays_[0] = d0;
    base_delays_[1] = d1;
    base_delays_[2] = d2;
    base_delays_[3] = d3;
    RecalculateDelays();
  }

  // Parameter getters
  inline float GetDecay() const { return decay_; }
  inline float GetMix() const { return mix_; }
  inline float GetDamping() const { return damping_; }
  inline float GetDelayScale() const { return delay_scale_; }

  // Clear all delay lines
  inline void Clear() {
    for (size_t i = 0; i < NumLines; i++) {
      std::memset(delay_lines_[i], 0, sizeof(delay_lines_[i]));
      lp_state_[i] = 0.0f;
    }
  }

private:
  float sample_rate_;
  float decay_;
  float mix_;
  float delay_scale_;
  float damping_;

  size_t base_delays_[NumLines];
  size_t delays_[NumLines];
  float input_gains_[NumLines];
  float output_gains_[NumLines];
  float feedback_matrix_[NumLines][NumLines];

  float delay_lines_[NumLines][MaxDelay];
  size_t write_ptrs_[NumLines];
  float lp_state_[NumLines];

  void RecalculateDelays() {
    float rate_scale = sample_rate_ / 44100.0f;
    for (size_t i = 0; i < NumLines; i++) {
      size_t scaled = static_cast<size_t>(static_cast<float>(base_delays_[i]) *
                                          rate_scale * delay_scale_);
      delays_[i] = (scaled > MaxDelay - 1) ? MaxDelay - 1 : scaled;
      if (delays_[i] < 1)
        delays_[i] = 1;
    }
  }

  void BuildFeedbackMatrix() {
    // Hadamard-style orthogonal matrix scaled by decay
    // [0  1  1  0]
    // [-1 0  0 -1]
    // [1  0  0 -1]
    // [0  1 -1  0]
    // Scaled by 1/sqrt(2) for energy preservation, then by decay

    float scale = decay_ / std::sqrt(2.0f);

    feedback_matrix_[0][0] = 0.0f;
    feedback_matrix_[0][1] = scale;
    feedback_matrix_[0][2] = scale;
    feedback_matrix_[0][3] = 0.0f;

    feedback_matrix_[1][0] = -scale;
    feedback_matrix_[1][1] = 0.0f;
    feedback_matrix_[1][2] = 0.0f;
    feedback_matrix_[1][3] = -scale;

    feedback_matrix_[2][0] = scale;
    feedback_matrix_[2][1] = 0.0f;
    feedback_matrix_[2][2] = 0.0f;
    feedback_matrix_[2][3] = -scale;

    feedback_matrix_[3][0] = 0.0f;
    feedback_matrix_[3][1] = scale;
    feedback_matrix_[3][2] = -scale;
    feedback_matrix_[3][3] = 0.0f;
  }
};

// Common type aliases
using FDNReverb4K = FDNReverb<4096>;
using FDNReverb8K = FDNReverb<8192>;
using FDNReverb16K = FDNReverb<16384>;

} // namespace daisysp

#endif // DSY_FDN_REVERB_H
