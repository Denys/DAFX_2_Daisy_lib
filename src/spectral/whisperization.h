// # Whisperization
// FFT-based whisperization effect
//
// Creates a breathy whisper effect by replacing the phase spectrum
// with random values while preserving magnitude.
//
// ## Algorithm
// 1. Window input grain
// 2. FFT to frequency domain
// 3. Extract magnitude, generate random phase
// 4. Reconstruct spectrum with random phase
// 5. IFFT, window, and overlap-add
//
// ## Example
// ~~~~
// Whisperization<512> whisper;
// whisper.Init(48000.0f);
// whisper.Process(input, output, block_size);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 7, VX_whisper.m
#pragma once
#ifndef DSY_WHISPERIZATION_H
#define DSY_WHISPERIZATION_H

#include "../utility/fft_handler.h"
#include "../utility/windows.h"
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#ifndef TWOPI
#define TWOPI 6.28318530717958647692f
#endif

namespace daisysp {

/**
 * @brief FFT-based whisperization effect
 *
 * Creates a breathy whisper effect by randomizing the phase spectrum
 * while preserving the magnitude (spectral envelope).
 *
 * @tparam N FFT size (must be power of 2, typical: 512)
 */
template <size_t N = 512> class Whisperization {
public:
  static_assert((N & (N - 1)) == 0, "FFT size must be power of 2");

  Whisperization()
      : initialized_(false), hop_size_(N / 8), mix_(1.0f), rand_seed_(12345) {}

  /**
   * @brief Initialize the whisperization effect
   *
   * @param sample_rate Audio sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Initialize FFT
    fft_.Init();

    // Generate Hanning window
    Windows::Hanning(window_, N);

    // Clear buffers
    std::memset(input_buffer_, 0, sizeof(input_buffer_));
    std::memset(output_buffer_, 0, sizeof(output_buffer_));
    std::memset(overlap_buffer_, 0, sizeof(overlap_buffer_));

    input_pos_ = 0;
    output_pos_ = 0;

    initialized_ = true;
  }

  /**
   * @brief Process a block of audio samples
   *
   * @param input Input audio buffer
   * @param output Output audio buffer
   * @param size Number of samples to process
   */
  void Process(const float *input, float *output, size_t size) {
    for (size_t i = 0; i < size; ++i) {
      // Store input sample
      input_buffer_[input_pos_] = input[i];

      // Output from overlap buffer with dry/wet mix
      float wet = overlap_buffer_[output_pos_];
      float dry = input[i];
      output[i] = dry * (1.0f - mix_) + wet * mix_;

      // Consume from overlap buffer
      overlap_buffer_[output_pos_] = 0.0f;

      input_pos_++;
      output_pos_ = (output_pos_ + 1) % N;

      // Process grain when we have enough samples
      if (input_pos_ >= hop_size_) {
        ProcessGrain();
        input_pos_ = 0;
      }
    }
  }

  /**
   * @brief Process a single sample (convenience method)
   *
   * @param in Input sample
   * @return Processed output sample
   */
  float Process(float in) {
    float out;
    Process(&in, &out, 1);
    return out;
  }

  /**
   * @brief Set the hop size (overlap amount)
   *
   * @param hop_size Hop size in samples (typically N/8 for 87.5% overlap)
   */
  void SetHopSize(size_t hop_size) {
    if (hop_size > 0 && hop_size <= N) {
      hop_size_ = hop_size;
    }
  }

  /**
   * @brief Get current hop size
   */
  size_t GetHopSize() const { return hop_size_; }

  /**
   * @brief Set dry/wet mix
   *
   * @param mix Mix ratio (0 = dry, 1 = wet)
   */
  void SetMix(float mix) { mix_ = std::fmax(0.0f, std::fmin(1.0f, mix)); }

  /**
   * @brief Get current mix value
   */
  float GetMix() const { return mix_; }

  /**
   * @brief Set random seed for reproducible results (useful for testing)
   *
   * @param seed Random seed value
   */
  void SetSeed(uint32_t seed) { rand_seed_ = seed; }

  /**
   * @brief Get FFT size
   */
  constexpr size_t GetFFTSize() const { return N; }

  /**
   * @brief Check if initialized
   */
  bool IsInitialized() const { return initialized_; }

private:
  FFTHandler<N> fft_;
  float window_[N];
  float input_buffer_[N];
  float output_buffer_[N];
  float overlap_buffer_[N * 2]; // Double size for overlap-add
  float freq_real_[N];
  float freq_imag_[N];
  float magnitude_[N];

  float sample_rate_;
  size_t hop_size_;
  size_t input_pos_;
  size_t output_pos_;
  float mix_;
  uint32_t rand_seed_;
  bool initialized_;

  /**
   * @brief Simple linear congruential generator for random phase
   *
   * Fast, deterministic random number generation.
   */
  float RandomPhase() {
    // LCG parameters (Numerical Recipes)
    rand_seed_ = rand_seed_ * 1664525u + 1013904223u;
    // Convert to [0, 2π)
    return static_cast<float>(rand_seed_) / 4294967296.0f * TWOPI;
  }

  /**
   * @brief Process one grain (FFT frame)
   */
  void ProcessGrain() {
    // Build grain from input buffer with FFT shift and window
    float grain[N];
    const size_t half = N / 2;
    for (size_t i = 0; i < N; ++i) {
      // Circular access to get the last N samples with FFT shift
      size_t idx = (input_pos_ + i) % N;
      size_t shifted_idx = (i + half) % N;
      grain[shifted_idx] = input_buffer_[idx] * window_[i];
    }

    // Forward FFT
    fft_.Forward(grain, freq_real_, freq_imag_);

    // Extract magnitude
    fft_.GetMagnitude(freq_real_, freq_imag_, magnitude_);

    // Generate random phase for each bin and reconstruct
    for (size_t i = 0; i < N; ++i) {
      float phase = RandomPhase();
      freq_real_[i] = magnitude_[i] * std::cos(phase);
      freq_imag_[i] = magnitude_[i] * std::sin(phase);
    }

    // Inverse FFT
    fft_.Inverse(freq_real_, freq_imag_, grain);

    // Apply FFT shift and synthesis window
    for (size_t i = 0; i < N; ++i) {
      size_t shifted_idx = (i + half) % N;
      output_buffer_[i] = grain[shifted_idx] * window_[i];
    }

    // Overlap-add to output buffer
    for (size_t i = 0; i < N; ++i) {
      size_t out_idx = (output_pos_ + i) % (N * 2);
      overlap_buffer_[out_idx] += output_buffer_[i];
    }
  }
};

} // namespace daisysp

#endif // DSY_WHISPERIZATION_H
