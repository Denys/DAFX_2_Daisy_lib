// # Simple HRIR Generator
// Simplified Head-Related Impulse Response for crosstalk cancellation
//
// Implements a basic HRIR model with ITD (Interaural Time Difference)
// and ILD (Interaural Level Difference) approximations. Not a full HRTF
// database, but sufficient for basic crosstalk cancellation.
//
// ## References
// - DAFX 2nd Ed., Chapter 5 (Spatial Effects)
// - simpleHRIR.m by F. Fontana, D. Rocchesso, V. Pulkki
#pragma once
#ifndef DSY_SIMPLE_HRIR_H
#define DSY_SIMPLE_HRIR_H

#include <cmath>
#include <cstddef>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {

/**
 * @brief Simple HRIR generator for crosstalk cancellation
 *
 * Generates simplified Head-Related Impulse Responses using only
 * ITD and ILD approximations. Based on a spherical head model.
 *
 * @tparam MaxLength Maximum HRIR length in samples (3ms @ 48kHz ≈ 144)
 */
template <size_t MaxLength = 256> class SimpleHRIR {
public:
  SimpleHRIR() : sample_rate_(48000.0f), length_(0) {}

  /**
   * @brief Initialize the HRIR generator
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    length_ = static_cast<size_t>(0.003f * sample_rate_); // 3ms impulse
    if (length_ > MaxLength)
      length_ = MaxLength;
  }

  /**
   * @brief Generate HRIR for a given azimuth angle
   *
   * The HRIR models the acoustic path from a sound source at angle theta
   * to one ear. For stereo crosstalk cancellation, you need HRIRs for
   * both the ipsilateral (same side) and contralateral (opposite side) ears.
   *
   * @param theta Azimuth angle in degrees (0 = front, 90 = right, -90 = left)
   * @param output Output buffer for HRIR (must have at least GetLength()
   * samples)
   */
  void Generate(float theta, float *output) {
    // Clear output buffer
    std::memset(output, 0, length_ * sizeof(float));

    // Constants from DAFX simpleHRIR.m
    const float theta0 = 150.0f;  // Reference angle
    const float alfa_min = 0.05f; // Minimum attenuation
    const float c = 334.0f;       // Speed of sound (m/s)
    const float a = 0.08f;        // Head radius (m)
    const float w0 = c / a;       // Angular frequency

    // Shift theta to match MATLAB convention
    float theta_shifted = theta + 90.0f;

    // Calculate ILD (level difference) coefficient
    float alfa = 1.0f + alfa_min / 2.0f +
                 (1.0f - alfa_min / 2.0f) * std::cos(theta_shifted / theta0 *
                                                     static_cast<float>(M_PI));

    // First-order IIR filter coefficients for frequency-dependent ILD
    float w0_fs = w0 / sample_rate_;
    float denom = 1.0f + w0_fs;
    b0_ = (alfa + w0_fs) / denom;
    b1_ = (-alfa + w0_fs) / denom;
    a1_ = -(1.0f - w0_fs) / denom;

    // Calculate ITD (time delay) in samples
    size_t gdelay;
    float theta_rad = theta * static_cast<float>(M_PI) / 180.0f;

    if (std::fabs(theta) < 90.0f) {
      // Front hemisphere: delay based on cosine
      gdelay = static_cast<size_t>(
          std::round(-sample_rate_ / w0 * (std::cos(theta_rad) - 1.0f)));
    } else {
      // Rear hemisphere: delay based on angle
      gdelay = static_cast<size_t>(std::round(
          sample_rate_ / w0 *
          ((std::fabs(theta) - 90.0f) * static_cast<float>(M_PI) / 180.0f +
           1.0f)));
    }

    // Clamp delay to buffer size
    if (gdelay >= length_)
      gdelay = length_ - 1;

    // Generate impulse response
    // Apply first-order IIR filter: y[n] = b0*x[n] + b1*x[n-1] - a1*y[n-1]
    float x_prev = 0.0f;
    float y_prev = 0.0f;

    for (size_t i = 0; i < length_; ++i) {
      // Input is delta function at sample 0
      float x = (i == 0) ? 1.0f : 0.0f;

      // IIR filter
      float y = b0_ * x + b1_ * x_prev - a1_ * y_prev;

      // Apply delay (circular shift)
      size_t out_idx = i + gdelay;
      if (out_idx < length_) {
        output[out_idx] = y;
      }

      x_prev = x;
      y_prev = y;
    }
  }

  /**
   * @brief Get HRIR length in samples
   */
  size_t GetLength() const { return length_; }

  /**
   * @brief Get sample rate
   */
  float GetSampleRate() const { return sample_rate_; }

private:
  float sample_rate_;
  size_t length_;

  // Filter coefficients (stored for potential reuse)
  float b0_, b1_, a1_;
};

} // namespace daisysp

#endif // DSY_SIMPLE_HRIR_H
