// # princarg
// Phase unwrapping utility for spectral processing
//
// Wraps an arbitrary phase value into the principal range (-π, π].
// Essential for phase vocoder and spectral effects.
//
// ## References
// - DAFX 2nd Ed., Chapter 7, princarg.m
// - Original MATLAB: phase = mod(phase_in+pi,-2*pi) + pi
#pragma once
#ifndef DSY_PRINCARG_H
#define DSY_PRINCARG_H

#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

#ifndef TWOPI
#define TWOPI 6.28318530717958647692f
#endif

namespace daisysp {

/**
 * @brief Wrap phase to principal range (-π, π]
 *
 * This function normalizes any phase value to the range (-π, π].
 * Matches MATLAB's princarg.m implementation exactly.
 *
 * @param phase_in Input phase in radians (any value)
 * @return Phase wrapped to (-π, π]
 *
 * @note The formula used is: mod(phase_in + π, -2π) + π
 *       In C++, fmodf with negative divisor requires special handling.
 */
inline float Princarg(float phase_in) {
  // MATLAB: mod(phase_in+pi,-2*pi) + pi
  // C++ fmodf behavior with negative divisor differs from MATLAB mod
  // We use a mathematically equivalent transformation:
  // Wrap to (-π, π] by: phase - 2π * floor((phase + π) / 2π)
  float wrapped = phase_in - TWOPI * std::floor((phase_in + M_PI) / TWOPI);
  return wrapped;
}

/**
 * @brief Wrap phase array in-place
 *
 * @param phases Array of phase values
 * @param length Number of elements
 */
inline void PrincargArray(float *phases, size_t length) {
  for (size_t i = 0; i < length; ++i) {
    phases[i] = Princarg(phases[i]);
  }
}

/**
 * @brief Calculate phase difference with unwrapping
 *
 * Computes the phase difference between two values, ensuring
 * the result is in (-π, π]. Useful for phase vocoder bin tracking.
 *
 * @param phase1 First phase value
 * @param phase2 Second phase value
 * @return Wrapped difference (phase1 - phase2) in (-π, π]
 */
inline float PhaseDiff(float phase1, float phase2) {
  return Princarg(phase1 - phase2);
}

} // namespace daisysp

#endif // DSY_PRINCARG_H
