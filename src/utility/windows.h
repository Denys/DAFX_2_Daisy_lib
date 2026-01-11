// # Windows
// Window function library for spectral processing
//
// Provides common window functions used in FFT-based effects,
// overlap-add processing, and spectral analysis.
//
// ## Available Windows
// - Hanning (Hann): Smooth, good frequency resolution
// - Blackman-Harris: Excellent sidelobe suppression
// - Triangular: Simple linear taper
// - Hamming: Similar to Hanning with less sidelobe leakage
// - Rectangular: No windowing (for reference)
//
// ## Example
// ~~~~
// float window[1024];
// Windows::Hanning(window, 1024);
// for (int i = 0; i < 1024; i++) {
//     fft_buffer[i] *= window[i];
// }
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 6, Section 6.2 (Granular Synthesis)
// - DAFX 2nd Ed., Chapter 7, Section 7.1 (Phase Vocoder)
#pragma once
#ifndef DSY_WINDOWS_H
#define DSY_WINDOWS_H

#include <cmath>
#include <cstddef>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef TWO_PI
#define TWO_PI (2.0 * M_PI)
#endif

namespace daisysp {

/**
 * @brief Window function utilities for spectral processing
 *
 * Static methods to generate common window functions.
 * All methods fill a provided buffer with window coefficients.
 */
class Windows {
public:
  /**
   * @brief Generate a Hanning (Hann) window
   *
   * w[n] = 0.5 * (1 - cos(2π * n / N))
   *
   * This is the "symmetric" Hanning window, matching MATLAB's hanningz.
   * Good general-purpose window with smooth taper.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void Hanning(float *buffer, size_t size) {
    const float scale = static_cast<float>(TWO_PI) / static_cast<float>(size);
    for (size_t i = 0; i < size; i++) {
      buffer[i] = 0.5f * (1.0f - std::cos(scale * static_cast<float>(i)));
    }
  }

  /**
   * @brief Generate a Hamming window
   *
   * w[n] = 0.54 - 0.46 * cos(2π * n / (N-1))
   *
   * Similar to Hanning but with slightly less sidelobe leakage.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void Hamming(float *buffer, size_t size) {
    const float scale =
        static_cast<float>(TWO_PI) / static_cast<float>(size - 1);
    for (size_t i = 0; i < size; i++) {
      buffer[i] = 0.54f - 0.46f * std::cos(scale * static_cast<float>(i));
    }
  }

  /**
   * @brief Generate a Blackman-Harris window (4-term)
   *
   * Excellent sidelobe suppression (-92 dB first sidelobe).
   * Wider main lobe than Hanning, but much better stopband.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void BlackmanHarris(float *buffer, size_t size) {
    const float a0 = 0.35875f;
    const float a1 = 0.48829f;
    const float a2 = 0.14128f;
    const float a3 = 0.01168f;

    const float n_minus_1 = static_cast<float>(size - 1);

    for (size_t i = 0; i < size; i++) {
      const float n = static_cast<float>(i);
      buffer[i] =
          a0 - a1 * std::cos(static_cast<float>(TWO_PI) * n / n_minus_1) +
          a2 * std::cos(2.0f * static_cast<float>(TWO_PI) * n / n_minus_1) -
          a3 * std::cos(3.0f * static_cast<float>(TWO_PI) * n / n_minus_1);
    }
  }

  /**
   * @brief Generate a Blackman window (3-term)
   *
   * Good compromise between Hanning and Blackman-Harris.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void Blackman(float *buffer, size_t size) {
    const float a0 = 0.42f;
    const float a1 = 0.5f;
    const float a2 = 0.08f;

    const float n_minus_1 = static_cast<float>(size - 1);

    for (size_t i = 0; i < size; i++) {
      const float n = static_cast<float>(i);
      buffer[i] =
          a0 - a1 * std::cos(static_cast<float>(TWO_PI) * n / n_minus_1) +
          a2 * std::cos(2.0f * static_cast<float>(TWO_PI) * n / n_minus_1);
    }
  }

  /**
   * @brief Generate a triangular (Bartlett) window
   *
   * w[n] = 1 - |2n/(N-1) - 1|
   *
   * Simple linear taper. Useful for overlap-add with 50% overlap.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void Triangular(float *buffer, size_t size) {
    const float n_minus_1 = static_cast<float>(size - 1);
    const float half = n_minus_1 / 2.0f;

    for (size_t i = 0; i < size; i++) {
      const float n = static_cast<float>(i);
      buffer[i] = 1.0f - std::fabs((n - half) / half);
    }
  }

  /**
   * @brief Generate a rectangular window (no windowing)
   *
   * All coefficients are 1.0. Provided for completeness.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   */
  static void Rectangular(float *buffer, size_t size) {
    for (size_t i = 0; i < size; i++) {
      buffer[i] = 1.0f;
    }
  }

  /**
   * @brief Generate a Kaiser window
   *
   * Adjustable trade-off between main lobe width and sidelobe level.
   * Higher beta = narrower main lobe but higher sidelobes.
   *
   * @param buffer Output buffer to fill
   * @param size Window size in samples
   * @param beta Shape parameter (typical: 4-9)
   */
  static void Kaiser(float *buffer, size_t size, float beta = 8.0f) {
    const float n_minus_1 = static_cast<float>(size - 1);
    const float i0_beta = BesselI0(beta);

    for (size_t i = 0; i < size; i++) {
      const float n = static_cast<float>(i);
      const float ratio = (2.0f * n / n_minus_1) - 1.0f;
      buffer[i] = BesselI0(beta * std::sqrt(1.0f - ratio * ratio)) / i0_beta;
    }
  }

  /**
   * @brief Apply a window to a buffer (in-place multiplication)
   *
   * @param signal Signal buffer to window
   * @param window Window coefficients
   * @param size Buffer size
   */
  static void Apply(float *signal, const float *window, size_t size) {
    for (size_t i = 0; i < size; i++) {
      signal[i] *= window[i];
    }
  }

  /**
   * @brief Calculate the sum of a window (for normalization)
   *
   * @param window Window coefficients
   * @param size Window size
   * @return Sum of all coefficients
   */
  static float Sum(const float *window, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; i++) {
      sum += window[i];
    }
    return sum;
  }

  /**
   * @brief Calculate the sum of squared window values
   *
   * Useful for overlap-add normalization.
   *
   * @param window Window coefficients
   * @param size Window size
   * @return Sum of squared coefficients
   */
  static float SumSquared(const float *window, size_t size) {
    float sum = 0.0f;
    for (size_t i = 0; i < size; i++) {
      sum += window[i] * window[i];
    }
    return sum;
  }

private:
  /**
   * @brief Modified Bessel function of order 0 (for Kaiser window)
   *
   * Approximation using polynomial series expansion.
   */
  static float BesselI0(float x) {
    float sum = 1.0f;
    float term = 1.0f;
    const float x_half = x / 2.0f;
    const float x_half_squared = x_half * x_half;

    for (int k = 1; k <= 20; k++) {
      term *= x_half_squared / static_cast<float>(k * k);
      sum += term;
      if (term < 1e-10f)
        break;
    }

    return sum;
  }
};

} // namespace daisysp

#endif // DSY_WINDOWS_H
