// # YIN Pitch Detector
// Monophonic pitch detection using the YIN algorithm
//
// Implements the YIN pitch detection algorithm for real-time
// fundamental frequency estimation of monophonic signals.
//
// ## Algorithm Overview
// 1. Compute difference function d(τ)
// 2. Compute cumulative mean normalized difference d'(τ)
// 3. Apply absolute threshold to find candidates
// 4. Search for first minimum below threshold
// 5. Apply parabolic interpolation for sub-sample accuracy
//
// ## Parameters
// - tolerance: Detection threshold (0.1-0.5, default 0.15)
// - f0_min: Minimum detectable frequency (20-200 Hz)
// - f0_max: Maximum detectable frequency (500-2000 Hz)
//
// ## Performance
// - CPU: ~10-15% at 48kHz (optimized)
// - Memory: ~16KB (yinLen=1024, τ_max≈1000)
// - Latency: ~21ms (frame size)
//
// ## Example
// ~~~~
// YinPitchDetector<1024> yin;
// yin.Init(48000.0f);
// yin.SetTolerance(0.15f);
// yin.SetFrequencyRange(80.0f, 800.0f);
// float pitch = yin.Process(audio_buffer);  // Returns Hz or 0 if unvoiced
// ~~~~
//
// ## References
// - de Cheveigné, A., & Kawahara, H. (2002). YIN, a fundamental frequency
//   estimator for speech and music. JASA, 111(4), 1917-1930.
// - DAFX 2nd Ed., Chapter 9, yinDAFX.m
#pragma once
#ifndef DSY_YIN_H
#define DSY_YIN_H

#include <cmath>
#include <cstddef>
#include <cstring>

namespace daisysp {

/**
 * @brief YIN pitch detector result
 */
struct YinResult {
  float frequency;  ///< Detected pitch in Hz (0 if unvoiced)
  float period;     ///< Detected period in samples
  float confidence; ///< Detection confidence (1 - d'(τ))
  bool voiced;      ///< Whether signal is considered voiced

  YinResult()
      : frequency(0.0f), period(0.0f), confidence(0.0f), voiced(false) {}
};

/**
 * @brief YIN pitch detection algorithm
 *
 * Real-time monophonic pitch detector based on the YIN algorithm.
 * Optimized for embedded use with pre-allocated static buffers.
 *
 * @tparam YinLen Analysis window length (typically 1024 for speech,
 *                2048 for music)
 */
template <size_t YinLen = 1024> class YinPitchDetector {
public:
  YinPitchDetector()
      : sample_rate_(48000.0f), tolerance_(0.15f), f0_min_(80.0f),
        f0_max_(800.0f), tau_max_(600), tau_min_(60), hop_size_(YinLen / 2),
        input_pos_(0), frames_processed_(0) {}

  /**
   * @brief Initialize the YIN detector
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    input_pos_ = 0;
    frames_processed_ = 0;

    // Initialize buffers
    std::memset(input_buffer_, 0, sizeof(input_buffer_));
    std::memset(diff_function_, 0, sizeof(diff_function_));
    std::memset(cmnd_function_, 0, sizeof(cmnd_function_));

    // Recalculate tau limits
    UpdateTauLimits();

    // Initialize result
    last_result_ = YinResult();
  }

  /**
   * @brief Set detection tolerance
   *
   * Lower values = stricter pitch detection, fewer false positives.
   * Higher values = more sensitive, may detect noise as pitch.
   *
   * @param tolerance Threshold (0.1 to 0.5, default 0.15)
   */
  void SetTolerance(float tolerance) {
    tolerance_ = (tolerance < 0.05f)  ? 0.05f
                 : (tolerance > 0.5f) ? 0.5f
                                      : tolerance;
  }

  /**
   * @brief Set detectable frequency range
   *
   * @param f0_min Minimum frequency in Hz (e.g., 80 for voice)
   * @param f0_max Maximum frequency in Hz (e.g., 800 for voice)
   */
  void SetFrequencyRange(float f0_min, float f0_max) {
    f0_min_ = f0_min;
    f0_max_ = f0_max;
    UpdateTauLimits();
  }

  /**
   * @brief Set hop size for streaming processing
   *
   * @param hop_size Samples between analyses (default: YinLen/2)
   */
  void SetHopSize(size_t hop_size) {
    hop_size_ = (hop_size > 0 && hop_size <= YinLen) ? hop_size : YinLen / 2;
  }

  /**
   * @brief Process a single sample (streaming mode)
   *
   * Accumulates samples and performs pitch detection when
   * a full frame is ready.
   *
   * @param sample Input sample
   * @return true if a new pitch estimate is available
   */
  bool ProcessSample(float sample) {
    input_buffer_[input_pos_] = sample;
    input_pos_ = (input_pos_ + 1) % (YinLen + MaxTau);

    frames_processed_++;

    if (frames_processed_ >= hop_size_) {
      frames_processed_ = 0;
      AnalyzeFrame();
      return true;
    }

    return false;
  }

  /**
   * @brief Process a complete buffer
   *
   * Analyzes the provided buffer and returns pitch.
   *
   * @param input Audio buffer (at least YinLen + tau_max samples)
   * @return Detected pitch in Hz (0 if unvoiced)
   */
  float Process(const float *input) {
    // Copy to internal buffer
    std::memcpy(input_buffer_, input, (YinLen + tau_max_) * sizeof(float));
    AnalyzeFrame();
    return last_result_.frequency;
  }

  /**
   * @brief Get the last detection result
   *
   * @return Full YinResult structure with frequency, confidence, etc.
   */
  const YinResult &GetResult() const { return last_result_; }

  /**
   * @brief Get detected frequency in Hz
   *
   * @return Frequency in Hz, or 0 if unvoiced
   */
  float GetFrequency() const { return last_result_.frequency; }

  /**
   * @brief Get detection confidence
   *
   * @return Confidence value (0 to 1, higher = more confident)
   */
  float GetConfidence() const { return last_result_.confidence; }

  /**
   * @brief Check if signal is currently voiced
   *
   * @return true if pitch was detected
   */
  bool IsVoiced() const { return last_result_.voiced; }

  /**
   * @brief Get MIDI note number from detected pitch
   *
   * @return MIDI note number (fractional) or -1 if unvoiced
   */
  float GetMidiNote() const {
    if (!last_result_.voiced || last_result_.frequency < 10.0f) {
      return -1.0f;
    }
    // MIDI note = 69 + 12 * log2(f / 440)
    return 69.0f + 12.0f * std::log2(last_result_.frequency / 440.0f);
  }

  /**
   * @brief Get pitch deviation in cents from nearest MIDI note
   *
   * @return Deviation in cents (-50 to +50)
   */
  float GetCentsDeviation() const {
    float midi = GetMidiNote();
    if (midi < 0.0f)
      return 0.0f;

    float nearest = std::round(midi);
    return (midi - nearest) * 100.0f;
  }

  // Getters
  inline float GetSampleRate() const { return sample_rate_; }
  inline float GetTolerance() const { return tolerance_; }
  inline size_t GetTauMax() const { return tau_max_; }
  inline size_t GetTauMin() const { return tau_min_; }
  static constexpr size_t GetYinLen() { return YinLen; }

private:
  static constexpr size_t MaxTau = YinLen; // Maximum possible tau

  float sample_rate_;
  float tolerance_;
  float f0_min_;
  float f0_max_;
  size_t tau_max_;
  size_t tau_min_;
  size_t hop_size_;
  size_t input_pos_;
  size_t frames_processed_;

  float input_buffer_[YinLen + MaxTau];
  float diff_function_[MaxTau];
  float cmnd_function_[MaxTau];
  YinResult last_result_;

  /**
   * @brief Update tau limits based on frequency range
   */
  void UpdateTauLimits() {
    // tau = sample_rate / f0
    tau_min_ = static_cast<size_t>(sample_rate_ / f0_max_);
    tau_max_ = static_cast<size_t>(sample_rate_ / f0_min_);

    // Clamp to buffer limits
    if (tau_max_ > MaxTau)
      tau_max_ = MaxTau;
    if (tau_min_ < 2)
      tau_min_ = 2;
    if (tau_min_ >= tau_max_)
      tau_min_ = tau_max_ - 1;
  }

  /**
   * @brief Analyze current frame for pitch
   */
  void AnalyzeFrame() {
    // Step 1: Compute difference function
    // d(τ) = Σ(x[j] - x[j+τ])²
    ComputeDifferenceFunction();

    // Step 2: Compute cumulative mean normalized difference
    // d'(τ) = d(τ) / ((1/τ) * Σ d(j)), with d'(0) = 1
    ComputeCMND();

    // Step 3: Find first minimum below threshold
    size_t tau = FindPitchPeriod();

    // Step 4: Apply parabolic interpolation
    float refined_tau = (tau > 0) ? ParabolicInterpolation(tau) : 0.0f;

    // Build result
    if (refined_tau > 0.0f) {
      last_result_.period = refined_tau;
      last_result_.frequency = sample_rate_ / refined_tau;
      last_result_.confidence = 1.0f - cmnd_function_[tau];
      last_result_.voiced = true;
    } else {
      last_result_.period = 0.0f;
      last_result_.frequency = 0.0f;
      last_result_.confidence = 0.0f;
      last_result_.voiced = false;
    }
  }

  /**
   * @brief Compute the difference function (YIN step 2)
   *
   * d(τ) = Σⱼ (x[j] - x[j+τ])²
   *
   * This is the core autocorrelation-like measure.
   */
  void ComputeDifferenceFunction() {
    diff_function_[0] = 0.0f;

    for (size_t tau = 1; tau < tau_max_; tau++) {
      float sum = 0.0f;

      for (size_t j = 0; j < YinLen; j++) {
        float diff = input_buffer_[j] - input_buffer_[j + tau];
        sum += diff * diff;
      }

      diff_function_[tau] = sum;
    }
  }

  /**
   * @brief Compute cumulative mean normalized difference (YIN step 3)
   *
   * d'(τ) = d(τ) / ((1/τ) * Σⱼ₌₁ᵗᵃᵘ d(j))
   *
   * This normalization helps distinguish true periods from subharmonics.
   */
  void ComputeCMND() {
    cmnd_function_[0] = 1.0f;

    float running_sum = 0.0f;

    for (size_t tau = 1; tau < tau_max_; tau++) {
      running_sum += diff_function_[tau];

      if (running_sum > 1e-10f) {
        cmnd_function_[tau] =
            diff_function_[tau] * static_cast<float>(tau) / running_sum;
      } else {
        cmnd_function_[tau] = 1.0f;
      }
    }
  }

  /**
   * @brief Find pitch period using absolute threshold (YIN step 4)
   *
   * Search for first dip below threshold, then find its minimum.
   *
   * @return Period in samples (0 if no pitch found)
   */
  size_t FindPitchPeriod() {
    size_t tau = tau_min_;

    // Find first value below threshold
    while (tau < tau_max_) {
      if (cmnd_function_[tau] < tolerance_) {
        // Found a candidate - now find the minimum in this dip
        while (tau + 1 < tau_max_ &&
               cmnd_function_[tau + 1] < cmnd_function_[tau]) {
          tau++;
        }
        return tau;
      }
      tau++;
    }

    // No pitch found - try to find global minimum as fallback
    // (only if there's a clear minimum)
    float min_val = cmnd_function_[tau_min_];
    size_t min_tau = tau_min_;

    for (tau = tau_min_ + 1; tau < tau_max_; tau++) {
      if (cmnd_function_[tau] < min_val) {
        min_val = cmnd_function_[tau];
        min_tau = tau;
      }
    }

    // Only use fallback if minimum is reasonably low
    if (min_val < tolerance_ * 2.0f) {
      return min_tau;
    }

    return 0; // Unvoiced
  }

  /**
   * @brief Apply parabolic interpolation for sub-sample accuracy (YIN step 5)
   *
   * Fits a parabola through three points around the minimum for
   * improved frequency resolution.
   *
   * @param tau Integer period estimate
   * @return Refined period (fractional samples)
   */
  float ParabolicInterpolation(size_t tau) {
    if (tau < 1 || tau >= tau_max_ - 1) {
      return static_cast<float>(tau);
    }

    float y0 = cmnd_function_[tau - 1];
    float y1 = cmnd_function_[tau];
    float y2 = cmnd_function_[tau + 1];

    // Parabolic fit: offset = (y0 - y2) / (2 * (y0 - 2*y1 + y2))
    float denom = 2.0f * (y0 - 2.0f * y1 + y2);

    if (std::fabs(denom) < 1e-10f) {
      return static_cast<float>(tau);
    }

    float offset = (y0 - y2) / denom;

    // Limit offset to reasonable range
    if (offset > 1.0f)
      offset = 1.0f;
    if (offset < -1.0f)
      offset = -1.0f;

    return static_cast<float>(tau) + offset;
  }
};

// Common type aliases
using Yin1024 = YinPitchDetector<1024>;
using Yin2048 = YinPitchDetector<2048>;

} // namespace daisysp

#endif // DSY_YIN_H
