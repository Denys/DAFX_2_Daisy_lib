// # FFT Handler
// FFT/IFFT wrapper for spectral processing effects
//
// Provides a hardware-agnostic interface for FFT operations.
// Uses a simple radix-2 DIT implementation for portability.
// On Daisy Seed, can be replaced with ARM CMSIS-DSP for optimization.
//
// ## Supported FFT Sizes
// - 256, 512, 1024, 2048, 4096 (power of 2)
//
// ## Example
// ~~~~
// FFTHandler<1024> fft;
// fft.Init();
// fft.Forward(time_domain, freq_real, freq_imag);
// // ... modify spectrum ...
// fft.Inverse(freq_real, freq_imag, time_domain);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 7 (FFT-based effects)
// - Cooley-Tukey FFT algorithm
#pragma once
#ifndef DSY_FFT_HANDLER_H
#define DSY_FFT_HANDLER_H

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
 * @brief Complex number for FFT operations
 */
struct Complex {
  float real;
  float imag;

  Complex() : real(0.0f), imag(0.0f) {}
  Complex(float r, float i) : real(r), imag(i) {}

  Complex operator+(const Complex &other) const {
    return Complex(real + other.real, imag + other.imag);
  }

  Complex operator-(const Complex &other) const {
    return Complex(real - other.real, imag - other.imag);
  }

  Complex operator*(const Complex &other) const {
    return Complex(real * other.real - imag * other.imag,
                   real * other.imag + imag * other.real);
  }

  float Magnitude() const { return std::sqrt(real * real + imag * imag); }

  float Phase() const { return std::atan2(imag, real); }

  static Complex FromPolar(float mag, float phase) {
    return Complex(mag * std::cos(phase), mag * std::sin(phase));
  }
};

/**
 * @brief Template-based FFT handler with static allocation
 *
 * @tparam N FFT size (must be power of 2)
 */
template <size_t N> class FFTHandler {
public:
  static_assert((N & (N - 1)) == 0, "FFT size must be power of 2");

  FFTHandler() : initialized_(false) {}

  /**
   * @brief Initialize the FFT handler
   *
   * Pre-computes twiddle factors for efficiency.
   */
  void Init() {
    // Pre-compute twiddle factors
    for (size_t k = 0; k < N / 2; ++k) {
      float angle = -TWOPI * static_cast<float>(k) / static_cast<float>(N);
      twiddle_[k].real = std::cos(angle);
      twiddle_[k].imag = std::sin(angle);
    }

    // Pre-compute bit-reversal indices
    for (size_t i = 0; i < N; ++i) {
      bit_reverse_[i] = BitReverse(i, Log2(N));
    }

    initialized_ = true;
  }

  /**
   * @brief Perform forward FFT (time → frequency)
   *
   * @param input Time-domain input buffer (N samples)
   * @param output_real Real part of frequency domain output
   * @param output_imag Imaginary part of frequency domain output
   */
  void Forward(const float *input, float *output_real, float *output_imag) {
    // Copy input to complex buffer with bit-reversal
    for (size_t i = 0; i < N; ++i) {
      size_t j = bit_reverse_[i];
      buffer_[j].real = input[i];
      buffer_[j].imag = 0.0f;
    }

    // Cooley-Tukey DIT FFT
    FFTCore(false);

    // Copy to output
    for (size_t i = 0; i < N; ++i) {
      output_real[i] = buffer_[i].real;
      output_imag[i] = buffer_[i].imag;
    }
  }

  /**
   * @brief Perform forward FFT with complex buffer output
   *
   * @param input Time-domain input buffer (N samples)
   * @param output Complex frequency domain output
   */
  void Forward(const float *input, Complex *output) {
    for (size_t i = 0; i < N; ++i) {
      size_t j = bit_reverse_[i];
      buffer_[j].real = input[i];
      buffer_[j].imag = 0.0f;
    }

    FFTCore(false);

    std::memcpy(output, buffer_, N * sizeof(Complex));
  }

  /**
   * @brief Perform inverse FFT (frequency → time)
   *
   * @param input_real Real part of frequency domain input
   * @param input_imag Imaginary part of frequency domain input
   * @param output Time-domain output buffer (N samples)
   */
  void Inverse(const float *input_real, const float *input_imag,
               float *output) {
    // Copy input to complex buffer with bit-reversal
    for (size_t i = 0; i < N; ++i) {
      size_t j = bit_reverse_[i];
      buffer_[j].real = input_real[i];
      buffer_[j].imag = input_imag[i];
    }

    // Inverse FFT (conjugate twiddle, then normalize)
    FFTCore(true);

    // Copy real part to output with normalization
    const float scale = 1.0f / static_cast<float>(N);
    for (size_t i = 0; i < N; ++i) {
      output[i] = buffer_[i].real * scale;
    }
  }

  /**
   * @brief Perform inverse FFT with complex buffer input
   *
   * @param input Complex frequency domain input
   * @param output Time-domain output buffer (N samples)
   */
  void Inverse(const Complex *input, float *output) {
    for (size_t i = 0; i < N; ++i) {
      size_t j = bit_reverse_[i];
      buffer_[j] = input[i];
    }

    FFTCore(true);

    const float scale = 1.0f / static_cast<float>(N);
    for (size_t i = 0; i < N; ++i) {
      output[i] = buffer_[i].real * scale;
    }
  }

  /**
   * @brief Perform FFT shift (swap left and right halves)
   *
   * Useful for centering zero-frequency in spectrum display
   * or for certain spectral effects.
   *
   * @param data Buffer to shift (N samples)
   */
  void FFTShift(float *data) {
    const size_t half = N / 2;
    for (size_t i = 0; i < half; ++i) {
      float temp = data[i];
      data[i] = data[i + half];
      data[i + half] = temp;
    }
  }

  /**
   * @brief Get magnitude spectrum from complex frequency data
   *
   * @param freq_real Real part of frequency data
   * @param freq_imag Imaginary part of frequency data
   * @param magnitude Output magnitude buffer
   */
  void GetMagnitude(const float *freq_real, const float *freq_imag,
                    float *magnitude) {
    for (size_t i = 0; i < N; ++i) {
      magnitude[i] =
          std::sqrt(freq_real[i] * freq_real[i] + freq_imag[i] * freq_imag[i]);
    }
  }

  /**
   * @brief Get phase spectrum from complex frequency data
   *
   * @param freq_real Real part of frequency data
   * @param freq_imag Imaginary part of frequency data
   * @param phase Output phase buffer (in radians)
   */
  void GetPhase(const float *freq_real, const float *freq_imag, float *phase) {
    for (size_t i = 0; i < N; ++i) {
      phase[i] = std::atan2(freq_imag[i], freq_real[i]);
    }
  }

  /**
   * @brief Reconstruct complex frequency data from magnitude and phase
   *
   * @param magnitude Magnitude spectrum
   * @param phase Phase spectrum (in radians)
   * @param freq_real Output real part
   * @param freq_imag Output imaginary part
   */
  void FromPolar(const float *magnitude, const float *phase, float *freq_real,
                 float *freq_imag) {
    for (size_t i = 0; i < N; ++i) {
      freq_real[i] = magnitude[i] * std::cos(phase[i]);
      freq_imag[i] = magnitude[i] * std::sin(phase[i]);
    }
  }

  /**
   * @brief Get FFT size
   */
  constexpr size_t Size() const { return N; }

  /**
   * @brief Check if initialized
   */
  bool IsInitialized() const { return initialized_; }

private:
  Complex buffer_[N];
  Complex twiddle_[N / 2];
  size_t bit_reverse_[N];
  bool initialized_;

  /**
   * @brief Core FFT computation (Cooley-Tukey radix-2 DIT)
   */
  void FFTCore(bool inverse) {
    size_t stages = Log2(N);

    for (size_t stage = 0; stage < stages; ++stage) {
      size_t block_size = static_cast<size_t>(1) << (stage + 1);
      size_t half_block = block_size / 2;
      size_t twiddle_step = N / block_size;

      for (size_t block_start = 0; block_start < N; block_start += block_size) {
        for (size_t k = 0; k < half_block; ++k) {
          size_t even_idx = block_start + k;
          size_t odd_idx = block_start + k + half_block;

          Complex twiddle = twiddle_[k * twiddle_step];
          if (inverse) {
            twiddle.imag = -twiddle.imag; // Conjugate for IFFT
          }

          Complex even = buffer_[even_idx];
          Complex odd = buffer_[odd_idx] * twiddle;

          buffer_[even_idx] = even + odd;
          buffer_[odd_idx] = even - odd;
        }
      }
    }
  }

  /**
   * @brief Compute log base 2 (for power-of-2 sizes)
   */
  static constexpr size_t Log2(size_t n) {
    size_t log = 0;
    while ((static_cast<size_t>(1) << log) < n)
      ++log;
    return log;
  }

  /**
   * @brief Bit-reverse an index
   */
  static size_t BitReverse(size_t x, size_t bits) {
    size_t result = 0;
    for (size_t i = 0; i < bits; ++i) {
      result = (result << 1) | (x & 1);
      x >>= 1;
    }
    return result;
  }
};

/**
 * @brief Convenience typedefs for common FFT sizes
 */
using FFT256 = FFTHandler<256>;
using FFT512 = FFTHandler<512>;
using FFT1024 = FFTHandler<1024>;
using FFT2048 = FFTHandler<2048>;
using FFT4096 = FFTHandler<4096>;

} // namespace daisysp

#endif // DSY_FFT_HANDLER_H
