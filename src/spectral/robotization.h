// # Robotization
// FFT-based robotization effect
//
// Creates a metallic, robotic voice effect by discarding phase information
// and reconstructing the signal using magnitude only.
//
// ## Algorithm
// 1. Window input grain
// 2. FFT to frequency domain
// 3. Extract magnitude, discard phase
// 4. IFFT with zero phase
// 5. Window and overlap-add
//
// ## Example
// ~~~~
// Robotization<1024> robot;
// robot.Init(48000.0f);
// robot.Process(input, output, block_size);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 7, VX_robot.m
#pragma once
#ifndef DSY_ROBOTIZATION_H
#define DSY_ROBOTIZATION_H

#include "../utility/fft_handler.h"
#include "../utility/windows.h"
#include <cmath>
#include <cstddef>
#include <cstring>

namespace daisysp {

/**
 * @brief FFT-based robotization effect
 *
 * Creates a metallic robotic voice by discarding phase information
 * and reconstructing using magnitude-only spectrum.
 *
 * @tparam N FFT size (must be power of 2, typical: 1024)
 */
template <size_t N = 1024> class Robotization {
public:
  static_assert((N & (N - 1)) == 0, "FFT size must be power of 2");

  Robotization() : initialized_(false), hop_size_(N / 4), mix_(1.0f) {}

  /**
   * @brief Initialize the robotization effect
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
   * @param hop_size Hop size in samples (typically N/4 for 75% overlap)
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
  bool initialized_;

  /**
   * @brief Process one grain (FFT frame)
   */
  void ProcessGrain() {
    // Build grain from input buffer (use last N samples with wraparound)
    float grain[N];
    for (size_t i = 0; i < N; ++i) {
      // Circular access to get the last N samples
      size_t idx = (input_pos_ + i) % N;
      grain[i] = input_buffer_[idx] * window_[i];
    }

    // Forward FFT
    fft_.Forward(grain, freq_real_, freq_imag_);

    // Extract magnitude (discard phase - key to robotization)
    fft_.GetMagnitude(freq_real_, freq_imag_, magnitude_);

    // Reconstruct with zero phase (magnitude only)
    for (size_t i = 0; i < N; ++i) {
      freq_real_[i] = magnitude_[i];
      freq_imag_[i] = 0.0f;
    }

    // Inverse FFT
    fft_.Inverse(freq_real_, freq_imag_, grain);

    // Apply FFT shift (center the result) and synthesis window
    const size_t half = N / 2;
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

#endif // DSY_ROBOTIZATION_H
