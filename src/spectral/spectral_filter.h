// # Spectral Filter
// FFT-based FIR filtering for time-frequency domain processing
//
// Performs convolution in the frequency domain using overlap-add.
// More efficient than time-domain convolution for long FIR filters.
//
// ## Example
// ~~~~
// SpectralFilter<1280> filter;
// filter.Init(48000.0f);
// filter.SetBandpass(1000.0f, 0.002f);  // 1kHz center, damping
// float out = filter.Process(in);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 7, VX_filter.m
// - Overlap-add method for FFT convolution
#pragma once
#ifndef DSY_SPECTRAL_FILTER_H
#define DSY_SPECTRAL_FILTER_H

#include "../utility/fft_handler.h"
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
 * @brief FFT-based spectral filter using overlap-add convolution
 *
 * @tparam FIR_LENGTH Length of the FIR filter (default 1280)
 *
 * The FFT size is 2 * FIR_LENGTH to accommodate zero-padding
 * for linear convolution via circular convolution.
 */
template <size_t FIR_LENGTH = 1280> class SpectralFilter {
public:
  static constexpr size_t FFT_SIZE = 2 * FIR_LENGTH;

  SpectralFilter() : sample_rate_(48000.0f), input_pos_(0) {}

  /**
   * @brief Initialize the spectral filter
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    input_pos_ = 0;

    // Initialize FFT handler
    fft_.Init();

    // Clear buffers
    std::memset(input_buffer_, 0, sizeof(input_buffer_));
    std::memset(output_buffer_, 0, sizeof(output_buffer_));
    std::memset(overlap_buffer_, 0, sizeof(overlap_buffer_));
    std::memset(fir_, 0, sizeof(fir_));
    std::memset(fir_freq_real_, 0, sizeof(fir_freq_real_));
    std::memset(fir_freq_imag_, 0, sizeof(fir_freq_imag_));

    // Default: unity pass-through
    fir_[0] = 1.0f;
    UpdateFIRSpectrum();
  }

  /**
   * @brief Set a bandpass filter response
   *
   * Creates a damped sinusoid impulse response centered at the
   * specified frequency. This is the default filter type from VX_filter.m.
   *
   * @param center_freq Center frequency in Hz
   * @param damping Damping coefficient (0.001 to 0.01 typical)
   */
  void SetBandpass(float center_freq, float damping = 0.002f) {
    float fr = center_freq / sample_rate_;

    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      float n = static_cast<float>(i);
      // Damped sinusoid: exp(alpha * n) * sin(2 * pi * fr * n)
      fir_[i] = std::exp(damping * n) * std::sin(TWOPI * fr * n);
    }

    // Normalize
    NormalizeFIR();
    UpdateFIRSpectrum();
  }

  /**
   * @brief Set a lowpass filter response
   *
   * Creates a sinc-based lowpass filter.
   *
   * @param cutoff_freq Cutoff frequency in Hz
   */
  void SetLowpass(float cutoff_freq) {
    float fc = cutoff_freq / sample_rate_;
    size_t center = FIR_LENGTH / 2;

    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      float n = static_cast<float>(i) - static_cast<float>(center);
      if (std::fabs(n) < 1e-6f) {
        fir_[i] = 2.0f * fc;
      } else {
        fir_[i] = std::sin(TWOPI * fc * n) / (static_cast<float>(M_PI) * n);
      }
    }

    // Apply Hanning window for smooth rolloff
    float window[FIR_LENGTH];
    Windows::Hanning(window, FIR_LENGTH);
    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      fir_[i] *= window[i];
    }

    NormalizeFIR();
    UpdateFIRSpectrum();
  }

  /**
   * @brief Set a highpass filter response
   *
   * @param cutoff_freq Cutoff frequency in Hz
   */
  void SetHighpass(float cutoff_freq) {
    // Start with lowpass
    SetLowpass(cutoff_freq);

    // Spectral inversion: negate all and add 1 at center
    size_t center = FIR_LENGTH / 2;
    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      fir_[i] = -fir_[i];
    }
    fir_[center] += 1.0f;

    UpdateFIRSpectrum();
  }

  /**
   * @brief Set custom FIR coefficients
   *
   * @param coeffs FIR coefficient array
   * @param length Number of coefficients (must be <= FIR_LENGTH)
   */
  void SetFIR(const float *coeffs, size_t length) {
    std::memset(fir_, 0, sizeof(fir_));
    size_t copy_len = (length < FIR_LENGTH) ? length : FIR_LENGTH;
    std::memcpy(fir_, coeffs, copy_len * sizeof(float));
    UpdateFIRSpectrum();
  }

  /**
   * @brief Process a single sample
   *
   * Uses overlap-add method for block-based FFT convolution.
   *
   * @param in Input sample
   * @return Filtered output sample
   */
  float Process(float in) {
    // Add sample to input buffer
    input_buffer_[input_pos_] = in;

    // Get output from current position (includes overlap from previous block)
    float out = output_buffer_[input_pos_];

    // Clear this output position for next block
    output_buffer_[input_pos_] = overlap_buffer_[input_pos_];
    overlap_buffer_[input_pos_] = 0.0f;

    input_pos_++;

    // When we have FIR_LENGTH samples, process a block
    if (input_pos_ >= FIR_LENGTH) {
      ProcessBlock();
      input_pos_ = 0;
    }

    return out;
  }

  /**
   * @brief Get FIR filter length
   */
  constexpr size_t GetFIRLength() const { return FIR_LENGTH; }

  /**
   * @brief Get FFT size
   */
  constexpr size_t GetFFTSize() const { return FFT_SIZE; }

private:
  float sample_rate_;
  size_t input_pos_;

  // FFT handler
  FFTHandler<FFT_SIZE> fft_;

  // Buffers
  float input_buffer_[FIR_LENGTH];
  float output_buffer_[FIR_LENGTH];
  float overlap_buffer_[FIR_LENGTH];

  // FIR filter
  float fir_[FIR_LENGTH];
  float fir_freq_real_[FFT_SIZE];
  float fir_freq_imag_[FFT_SIZE];

  // Working buffers
  float work_real_[FFT_SIZE];
  float work_imag_[FFT_SIZE];
  float time_buffer_[FFT_SIZE];

  /**
   * @brief Update FIR spectrum after coefficient change
   */
  void UpdateFIRSpectrum() {
    // Zero-pad FIR to FFT size
    float fir_padded[FFT_SIZE];
    std::memset(fir_padded, 0, sizeof(fir_padded));
    std::memcpy(fir_padded, fir_, FIR_LENGTH * sizeof(float));

    // Compute FFT of FIR
    fft_.Forward(fir_padded, fir_freq_real_, fir_freq_imag_);
  }

  /**
   * @brief Normalize FIR to unity DC gain
   */
  void NormalizeFIR() {
    float sum = 0.0f;
    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      sum += fir_[i];
    }
    if (std::fabs(sum) > 1e-6f) {
      float scale = 1.0f / std::fabs(sum);
      for (size_t i = 0; i < FIR_LENGTH; ++i) {
        fir_[i] *= scale;
      }
    }
  }

  /**
   * @brief Process one block using FFT convolution
   */
  void ProcessBlock() {
    // Zero-pad input to FFT size
    float input_padded[FFT_SIZE];
    std::memset(input_padded, 0, sizeof(input_padded));
    std::memcpy(input_padded, input_buffer_, FIR_LENGTH * sizeof(float));

    // Forward FFT of input
    fft_.Forward(input_padded, work_real_, work_imag_);

    // Complex multiplication in frequency domain
    for (size_t i = 0; i < FFT_SIZE; ++i) {
      float re =
          work_real_[i] * fir_freq_real_[i] - work_imag_[i] * fir_freq_imag_[i];
      float im =
          work_real_[i] * fir_freq_imag_[i] + work_imag_[i] * fir_freq_real_[i];
      work_real_[i] = re;
      work_imag_[i] = im;
    }

    // Inverse FFT
    fft_.Inverse(work_real_, work_imag_, time_buffer_);

    // Overlap-add: first half goes to output, second half to overlap
    for (size_t i = 0; i < FIR_LENGTH; ++i) {
      output_buffer_[i] = time_buffer_[i] + overlap_buffer_[i];
      overlap_buffer_[i] = time_buffer_[i + FIR_LENGTH];
    }
  }
};

} // namespace daisysp

#endif // DSY_SPECTRAL_FILTER_H
