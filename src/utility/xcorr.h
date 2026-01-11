// # CrossCorrelation
// Cross-correlation utility for time-domain alignment
//
// Provides cross-correlation computation for SOLA time stretching
// and pitch detection algorithms.
//
// ## Example
// ~~~~
// float result[256];
// CrossCorrelation::Compute(signal1, signal2, 256, result, 64);
// size_t lag = CrossCorrelation::FindPeakLag(result, 64);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 6, Section 6.3 (SOLA Time Stretching)
#pragma once
#ifndef DSY_XCORR_H
#define DSY_XCORR_H

#include <cmath>
#include <cstddef>

namespace daisysp {

/**
 * @brief Cross-correlation utilities for signal alignment
 *
 * Static methods for computing cross-correlation between signals,
 * finding correlation peaks, and related operations.
 */
class CrossCorrelation {
public:
  /**
   * @brief Compute cross-correlation between two signals
   *
   * Computes xcorr(x, y) for lags 0 to max_lag-1.
   * Output[lag] = sum(x[n] * y[n + lag]) for all valid n.
   *
   * @param x First signal (reference)
   * @param y Second signal (to be searched)
   * @param length Length of signals
   * @param output Output correlation values (size max_lag)
   * @param max_lag Maximum lag to compute
   */
  static void Compute(const float *x, const float *y, size_t length,
                      float *output, size_t max_lag) {
    for (size_t lag = 0; lag < max_lag; lag++) {
      float sum = 0.0f;
      size_t overlap = length - lag;

      for (size_t n = 0; n < overlap; n++) {
        sum += x[n] * y[n + lag];
      }

      output[lag] = sum;
    }
  }

  /**
   * @brief Compute normalized cross-correlation
   *
   * Normalizes correlation by signal energies for scale-invariant matching.
   * Result is in range [-1, 1].
   *
   * @param x First signal
   * @param y Second signal
   * @param length Length of signals
   * @param output Output correlation values (size max_lag)
   * @param max_lag Maximum lag to compute
   */
  static void ComputeNormalized(const float *x, const float *y, size_t length,
                                float *output, size_t max_lag) {
    // Compute energy of x
    float energy_x = 0.0f;
    for (size_t n = 0; n < length; n++) {
      energy_x += x[n] * x[n];
    }

    for (size_t lag = 0; lag < max_lag; lag++) {
      float sum = 0.0f;
      float energy_y = 0.0f;
      size_t overlap = length - lag;

      for (size_t n = 0; n < overlap; n++) {
        sum += x[n] * y[n + lag];
        energy_y += y[n + lag] * y[n + lag];
      }

      // Normalize by geometric mean of energies
      float norm = std::sqrt(energy_x * energy_y);
      output[lag] = (norm > 1e-10f) ? (sum / norm) : 0.0f;
    }
  }

  /**
   * @brief Find the lag corresponding to maximum correlation
   *
   * @param correlation Correlation values
   * @param length Number of lags
   * @return Lag with maximum correlation value
   */
  static size_t FindPeakLag(const float *correlation, size_t length) {
    size_t peak_lag = 0;
    float peak_value = correlation[0];

    for (size_t i = 1; i < length; i++) {
      if (correlation[i] > peak_value) {
        peak_value = correlation[i];
        peak_lag = i;
      }
    }

    return peak_lag;
  }

  /**
   * @brief Find peak lag with quadratic interpolation
   *
   * Uses parabolic interpolation around the peak for sub-sample accuracy.
   *
   * @param correlation Correlation values
   * @param length Number of lags
   * @return Interpolated lag (floating point)
   */
  static float FindPeakLagInterpolated(const float *correlation,
                                       size_t length) {
    size_t peak_lag = FindPeakLag(correlation, length);

    // Can't interpolate at boundaries
    if (peak_lag == 0 || peak_lag >= length - 1) {
      return static_cast<float>(peak_lag);
    }

    // Parabolic interpolation
    float y0 = correlation[peak_lag - 1];
    float y1 = correlation[peak_lag];
    float y2 = correlation[peak_lag + 1];

    // Peak offset: -0.5 * (y2 - y0) / (y2 - 2*y1 + y0)
    float denom = y2 - 2.0f * y1 + y0;
    if (std::fabs(denom) < 1e-10f) {
      return static_cast<float>(peak_lag);
    }

    float offset = -0.5f * (y2 - y0) / denom;
    return static_cast<float>(peak_lag) + offset;
  }

  /**
   * @brief Compute autocorrelation of a signal
   *
   * Special case of cross-correlation where x == y.
   *
   * @param x Input signal
   * @param length Signal length
   * @param output Output autocorrelation values
   * @param max_lag Maximum lag to compute
   */
  static void Autocorrelation(const float *x, size_t length, float *output,
                              size_t max_lag) {
    Compute(x, x, length, output, max_lag);
  }

  /**
   * @brief Compute autocorrelation with YIN-style difference function
   *
   * d(τ) = Σ(x[n] - x[n+τ])² = r(0) + r_shifted(0) - 2*r(τ)
   *
   * This is used in YIN pitch detection.
   *
   * @param x Input signal
   * @param length Signal length
   * @param output Output difference values
   * @param max_lag Maximum lag to compute
   */
  static void DifferenceFunction(const float *x, size_t length, float *output,
                                 size_t max_lag) {
    // First compute autocorrelation at lag 0 (energy)
    float energy = 0.0f;
    for (size_t n = 0; n < length; n++) {
      energy += x[n] * x[n];
    }

    for (size_t tau = 0; tau < max_lag; tau++) {
      float sum = 0.0f;
      float energy_shifted = 0.0f;

      for (size_t n = 0; n < length - tau; n++) {
        float diff = x[n] - x[n + tau];
        sum += diff * diff;
        energy_shifted += x[n + tau] * x[n + tau];
      }

      output[tau] = sum;
    }
  }

  /**
   * @brief Cumulative mean normalized difference function (for YIN)
   *
   * d'(τ) = d(τ) / ((1/τ) * Σ_{j=1}^{τ} d(j)), with d'(0) = 1
   *
   * @param diff_func Input difference function
   * @param output Output normalized difference
   * @param length Number of lags
   */
  static void CumulativeMeanNormalize(const float *diff_func, float *output,
                                      size_t length) {
    output[0] = 1.0f;

    float running_sum = 0.0f;

    for (size_t tau = 1; tau < length; tau++) {
      running_sum += diff_func[tau];
      output[tau] =
          (running_sum > 1e-10f)
              ? diff_func[tau] / (running_sum / static_cast<float>(tau))
              : 1.0f;
    }
  }
};

} // namespace daisysp

#endif // DSY_XCORR_H
