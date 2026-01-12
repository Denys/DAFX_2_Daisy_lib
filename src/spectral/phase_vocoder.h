// # Phase Vocoder Pitch Shifter
// FFT-based pitch shifting using phase accumulation and grain resampling
//
// Implements the phase vocoder algorithm for real-time pitch shifting.
// Uses phase unwrapping (princarg) for accurate phase tracking.
//
// ## Example
// ~~~~
// PhaseVocoder<2048> pv;
// pv.Init(48000.0f);
// pv.SetPitchRatio(1.2f);  // Shift up 20%
// float out = pv.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 7, VX_pitch_pv.m
// - Phase Vocoder Tutorial (Dolson, 1986)
#pragma once
#ifndef DSY_PHASE_VOCODER_H
#define DSY_PHASE_VOCODER_H

#include "../utility/fft_handler.h"
#include "../utility/princarg.h"
#include "../utility/windows.h"
#include <cmath>
#include <cstddef>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef TWOPI
#define TWOPI 6.28318530717958647692f
#endif

namespace daisysp {

/**
 * @brief Phase vocoder pitch shifter
 *
 * @tparam FFT_SIZE FFT window size (default 2048)
 *
 * The synthesis hop size varies based on pitch ratio while
 * analysis hop is fixed at FFT_SIZE/4.
 */
template <size_t FFT_SIZE = 2048> class PhaseVocoder {
public:
  static constexpr size_t HOP_SIZE = FFT_SIZE / 4; // 75% overlap
  static constexpr size_t MAX_GRAIN = FFT_SIZE;

  PhaseVocoder()
      : sample_rate_(48000.0f), pitch_ratio_(1.0f), input_pos_(0),
        output_pos_(0), grain_pos_(0) {}

  /**
   * @brief Initialize the phase vocoder
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    pitch_ratio_ = 1.0f;
    input_pos_ = 0;
    output_pos_ = 0;
    grain_pos_ = 0;

    // Initialize FFT
    fft_.Init();

    // Generate windows
    Windows::Hanning(analysis_window_, FFT_SIZE);
    Windows::Hanning(synthesis_window_, FFT_SIZE);

    // Clear all buffers
    std::memset(input_buffer_, 0, sizeof(input_buffer_));
    std::memset(output_buffer_, 0, sizeof(output_buffer_));
    std::memset(prev_phase_, 0, sizeof(prev_phase_));
    std::memset(accum_phase_, 0, sizeof(accum_phase_));
    std::memset(grain_buffer_, 0, sizeof(grain_buffer_));

    // Pre-compute expected phase increment per bin
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      omega_[k] = TWOPI * static_cast<float>(k) * static_cast<float>(HOP_SIZE) /
                  static_cast<float>(FFT_SIZE);
    }

    UpdateInterpolationParams();
  }

  /**
   * @brief Set pitch ratio
   *
   * @param ratio Pitch multiplier (0.5 = octave down, 2.0 = octave up)
   */
  void SetPitchRatio(float ratio) {
    // Clamp to safe range
    if (ratio < 0.5f)
      ratio = 0.5f;
    if (ratio > 2.0f)
      ratio = 2.0f;

    if (ratio != pitch_ratio_) {
      pitch_ratio_ = ratio;
      UpdateInterpolationParams();
    }
  }

  /**
   * @brief Get current pitch ratio
   */
  float GetPitchRatio() const { return pitch_ratio_; }

  /**
   * @brief Process a single sample
   *
   * @param in Input sample
   * @return Pitch-shifted output sample
   */
  float Process(float in) {
    // Add input to circular buffer
    input_buffer_[input_pos_] = in;
    input_pos_ = (input_pos_ + 1) % (FFT_SIZE * 2);

    // Get output from grain buffer
    float out = 0.0f;
    if (grain_pos_ < grain_length_) {
      // Linear interpolation within grain
      float frac_pos = static_cast<float>(grain_pos_);
      size_t idx0 = static_cast<size_t>(frac_pos);
      size_t idx1 = idx0 + 1;
      if (idx1 >= FFT_SIZE)
        idx1 = FFT_SIZE - 1;
      float frac = frac_pos - static_cast<float>(idx0);

      out = grain_buffer_[idx0] * (1.0f - frac) + grain_buffer_[idx1] * frac;

      grain_pos_++;
    }

    // Add overlapped output
    out += output_buffer_[output_pos_];
    output_buffer_[output_pos_] = 0.0f;
    output_pos_ = (output_pos_ + 1) % FFT_SIZE;

    // Check if it's time to process a new frame
    frame_counter_++;
    if (frame_counter_ >= HOP_SIZE) {
      ProcessFrame();
      frame_counter_ = 0;
    }

    return out;
  }

  /**
   * @brief Get FFT size
   */
  constexpr size_t GetFFTSize() const { return FFT_SIZE; }

  /**
   * @brief Get hop size
   */
  constexpr size_t GetHopSize() const { return HOP_SIZE; }

private:
  float sample_rate_;
  float pitch_ratio_;
  size_t input_pos_;
  size_t output_pos_;
  size_t grain_pos_;
  size_t grain_length_;
  size_t frame_counter_ = 0;

  // FFT handler
  FFTHandler<FFT_SIZE> fft_;

  // Windows
  float analysis_window_[FFT_SIZE];
  float synthesis_window_[FFT_SIZE];

  // Phase tracking
  float prev_phase_[FFT_SIZE];
  float accum_phase_[FFT_SIZE];
  float omega_[FFT_SIZE]; // Expected phase increment per bin

  // Buffers
  float input_buffer_[FFT_SIZE * 2];
  float output_buffer_[FFT_SIZE];
  float grain_buffer_[FFT_SIZE];

  // Working buffers
  float freq_real_[FFT_SIZE];
  float freq_imag_[FFT_SIZE];
  float magnitude_[FFT_SIZE];
  float phase_[FFT_SIZE];
  float time_buffer_[FFT_SIZE];

  // Interpolation
  float interp_x_[FFT_SIZE];
  size_t interp_idx0_[FFT_SIZE];
  size_t interp_idx1_[FFT_SIZE];
  float interp_frac_[FFT_SIZE];

  /**
   * @brief Update interpolation parameters after pitch ratio change
   */
  void UpdateInterpolationParams() {
    // Calculate grain length after resampling
    grain_length_ =
        static_cast<size_t>(static_cast<float>(FFT_SIZE) / pitch_ratio_);
    if (grain_length_ > FFT_SIZE)
      grain_length_ = FFT_SIZE;

    // Pre-compute interpolation indices and fractions
    // Resample grain_length samples from FFT_SIZE
    for (size_t i = 0; i < grain_length_; ++i) {
      float x = static_cast<float>(i) * static_cast<float>(FFT_SIZE) /
                static_cast<float>(grain_length_);
      interp_x_[i] = x;
      interp_idx0_[i] = static_cast<size_t>(x);
      interp_idx1_[i] = interp_idx0_[i] + 1;
      if (interp_idx1_[i] >= FFT_SIZE)
        interp_idx1_[i] = FFT_SIZE - 1;
      interp_frac_[i] = x - static_cast<float>(interp_idx0_[i]);
    }
  }

  /**
   * @brief Process one frame of audio
   */
  void ProcessFrame() {
    // Extract input frame with analysis window
    float windowed[FFT_SIZE];
    size_t read_start = (input_pos_ + FFT_SIZE * 2 - FFT_SIZE) % (FFT_SIZE * 2);

    for (size_t i = 0; i < FFT_SIZE; ++i) {
      size_t idx = (read_start + i) % (FFT_SIZE * 2);
      windowed[i] = input_buffer_[idx] * analysis_window_[i];
    }

    // FFT shift before transform (matches MATLAB fftshift on input)
    FFTShiftInPlace(windowed);

    // Forward FFT
    fft_.Forward(windowed, freq_real_, freq_imag_);

    // Convert to magnitude and phase
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      magnitude_[k] = std::sqrt(freq_real_[k] * freq_real_[k] +
                                freq_imag_[k] * freq_imag_[k]);
      phase_[k] = std::atan2(freq_imag_[k], freq_real_[k]);
    }

    // Time-stretch ratio (inverse of pitch ratio for synthesis)
    float tstretch = 1.0f / pitch_ratio_;

    // Phase accumulation
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      // Compute instantaneous frequency deviation
      float delta_phi =
          omega_[k] + Princarg(phase_[k] - prev_phase_[k] - omega_[k]);

      // Save current phase for next frame
      prev_phase_[k] = phase_[k];

      // Accumulate phase with time stretch
      accum_phase_[k] = Princarg(accum_phase_[k] + delta_phi * tstretch);
    }

    // Reconstruct complex spectrum with accumulated phase
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      freq_real_[k] = magnitude_[k] * std::cos(accum_phase_[k]);
      freq_imag_[k] = magnitude_[k] * std::sin(accum_phase_[k]);
    }

    // Inverse FFT
    fft_.Inverse(freq_real_, freq_imag_, time_buffer_);

    // FFT shift after transform
    FFTShiftInPlace(time_buffer_);

    // Apply synthesis window
    for (size_t i = 0; i < FFT_SIZE; ++i) {
      time_buffer_[i] *= synthesis_window_[i];
    }

    // Resample grain by pitch ratio (linear interpolation)
    for (size_t i = 0; i < grain_length_; ++i) {
      grain_buffer_[i] =
          time_buffer_[interp_idx0_[i]] * (1.0f - interp_frac_[i]) +
          time_buffer_[interp_idx1_[i]] * interp_frac_[i];
    }

    // Overlap-add to output buffer
    for (size_t i = 0; i < grain_length_; ++i) {
      size_t out_idx = (output_pos_ + i) % FFT_SIZE;
      output_buffer_[out_idx] += grain_buffer_[i];
    }

    // Reset grain position
    grain_pos_ = 0;
  }

  /**
   * @brief FFT shift in place (swap halves)
   */
  void FFTShiftInPlace(float *data) {
    const size_t half = FFT_SIZE / 2;
    for (size_t i = 0; i < half; ++i) {
      float temp = data[i];
      data[i] = data[i + half];
      data[i + half] = temp;
    }
  }
};

} // namespace daisysp

#endif // DSY_PHASE_VOCODER_H
