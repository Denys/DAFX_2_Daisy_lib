// # SOLA Time Stretch
// Synchronized Overlap-Add time stretching algorithm
//
// Ported from DAFX book TimeScaleSOLA.m by U. Zölzer, G. De Poli, P. Dutilleux
// Implements time-domain time stretching using cross-correlation
// for optimal overlap alignment.
//
// ## Algorithm Overview
// 1. Segment input into overlapping grains of length N
// 2. Use cross-correlation to find optimal alignment
// 3. Crossfade overlapping regions for seamless output
//
// ## Parameters
// - time_stretch: Time stretch factor (0.5 = half speed, 2.0 = double speed)
// - grain_size: Analysis grain size in samples (default 2048)
// - analysis_hop: Analysis hop size (default 256)
// - overlap_length: Cross-correlation search length (default 128)
//
// ## Example
// ~~~~
// SOLATimeStretch<4096, 2048> sola;
// sola.Init(48000.0f);
// sola.SetTimeStretch(0.75f);  // Slow down by 25%
// // Feed input, pull output at stretched rate
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 6
// - Original MATLAB: TimeScaleSOLA.m
#pragma once
#ifndef DSY_SOLA_TIME_STRETCH_H
#define DSY_SOLA_TIME_STRETCH_H

#include <cmath>
#include <cstring>

namespace daisysp {

/**
 * @brief SOLA Time Stretch (Synchronized Overlap-Add)
 *
 * Real-time time stretching using cross-correlation for
 * optimal grain alignment and seamless playback.
 *
 * @tparam MaxGrain Maximum grain size in samples
 * @tparam GrainSize Default grain size (N)
 */
template <size_t MaxGrain = 4096, size_t GrainSize = 2048>
class SOLATimeStretch {
public:
  SOLATimeStretch()
      : sample_rate_(48000.0f), time_stretch_(1.0f), analysis_hop_(256),
        synthesis_hop_(256), overlap_len_(128), input_pos_(0), output_pos_(0),
        grain_ready_(false) {}

  /**
   * @brief Initialize the SOLA processor
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;

    // Clear buffers
    std::memset(input_buffer_, 0, sizeof(input_buffer_));
    std::memset(output_buffer_, 0, sizeof(output_buffer_));
    std::memset(current_grain_, 0, sizeof(current_grain_));
    std::memset(overlap_buffer_, 0, sizeof(overlap_buffer_));

    input_pos_ = 0;
    output_pos_ = 0;
    grain_ready_ = false;

    RecalculateHops();
  }

  /**
   * @brief Set time stretch factor
   *
   * @param stretch Time stretch factor (0.25 to 2.0)
   *        < 1.0 = slower playback (stretch)
   *        > 1.0 = faster playback (compress)
   */
  void SetTimeStretch(float stretch) {
    time_stretch_ = (stretch < 0.25f)  ? 0.25f
                    : (stretch > 2.0f) ? 2.0f
                                       : stretch;
    RecalculateHops();
  }

  /**
   * @brief Set grain size
   *
   * @param size Grain size in samples (must be <= MaxGrain)
   */
  void SetGrainSize(size_t size) {
    grain_size_ = (size > MaxGrain) ? MaxGrain : size;
    RecalculateHops();
  }

  /**
   * @brief Set analysis hop size
   *
   * @param hop Analysis hop in samples
   */
  void SetAnalysisHop(size_t hop) {
    analysis_hop_ = (hop > grain_size_ / 2) ? grain_size_ / 2 : hop;
    RecalculateHops();
  }

  /**
   * @brief Process input samples (streaming mode)
   *
   * Feed samples in, retrieve time-stretched samples out.
   * Input and output rates differ based on stretch factor.
   *
   * @param in Input sample
   * @return true if new output samples are available
   */
  bool FeedInput(float in) {
    // Store input sample
    input_buffer_[input_pos_ % (MaxGrain * 2)] = in;
    input_pos_++;

    // Check if we have enough samples for a new grain
    if (input_pos_ >= grain_size_ && !grain_ready_) {
      ExtractGrain();
      grain_ready_ = true;
      return true;
    }

    return false;
  }

  /**
   * @brief Get output sample (streaming mode)
   *
   * Call after FeedInput returns true.
   *
   * @return Output sample
   */
  float GetOutput() {
    if (output_pos_ >= synthesis_hop_) {
      output_pos_ = 0;
      grain_ready_ = false;
    }

    float out = output_buffer_[output_pos_];
    output_pos_++;
    return out;
  }

  /**
   * @brief Check if output is available
   *
   * @return true if more output samples can be retrieved
   */
  bool OutputAvailable() const {
    return grain_ready_ && (output_pos_ < synthesis_hop_);
  }

  /**
   * @brief Process a block of samples (offline mode)
   *
   * @param input Input buffer
   * @param input_length Input length in samples
   * @param output Output buffer (must be large enough)
   * @param max_output Maximum output length
   * @return Actual output length
   */
  size_t ProcessBlock(const float *input, size_t input_length, float *output,
                      size_t max_output) {
    size_t in_pos = 0;
    size_t out_pos = 0;

    // Initialize overlap buffer with first grain
    if (input_length >= grain_size_) {
      std::memcpy(overlap_buffer_, input, grain_size_ * sizeof(float));
      in_pos = analysis_hop_;
    }

    // Main SOLA loop
    while (in_pos + grain_size_ <= input_length &&
           out_pos + synthesis_hop_ <= max_output) {
      // Extract current grain
      std::memcpy(current_grain_, &input[in_pos], grain_size_ * sizeof(float));

      // Find optimal alignment using cross-correlation
      size_t optimal_offset = FindOptimalOffset();

      // Crossfade and overlap-add
      size_t fade_len = grain_size_ - optimal_offset;
      if (fade_len > synthesis_hop_)
        fade_len = synthesis_hop_;

      // Output the non-overlapping portion
      for (size_t i = 0; i < synthesis_hop_ - fade_len; i++) {
        if (out_pos < max_output) {
          output[out_pos++] = overlap_buffer_[i];
        }
      }

      // Crossfade region
      for (size_t i = 0; i < fade_len; i++) {
        float fade_out =
            1.0f - static_cast<float>(i) / static_cast<float>(fade_len);
        float fade_in = static_cast<float>(i) / static_cast<float>(fade_len);

        size_t overlap_idx = synthesis_hop_ - fade_len + i;
        float crossfade = overlap_buffer_[overlap_idx] * fade_out +
                          current_grain_[i] * fade_in;

        if (out_pos < max_output) {
          output[out_pos++] = crossfade;
        }
      }

      // Update overlap buffer
      std::memcpy(overlap_buffer_, &current_grain_[fade_len],
                  (grain_size_ - fade_len) * sizeof(float));

      in_pos += analysis_hop_;
    }

    return out_pos;
  }

  // Getters
  inline float GetTimeStretch() const { return time_stretch_; }
  inline size_t GetGrainSize() const { return grain_size_; }
  inline size_t GetAnalysisHop() const { return analysis_hop_; }
  inline size_t GetSynthesisHop() const { return synthesis_hop_; }
  inline float GetSampleRate() const { return sample_rate_; }

private:
  float sample_rate_;
  float time_stretch_;
  size_t grain_size_ = GrainSize;
  size_t analysis_hop_;
  size_t synthesis_hop_;
  size_t overlap_len_;

  float input_buffer_[MaxGrain * 2];
  float output_buffer_[MaxGrain];
  float current_grain_[MaxGrain];
  float overlap_buffer_[MaxGrain];

  size_t input_pos_;
  size_t output_pos_;
  bool grain_ready_;

  void RecalculateHops() {
    synthesis_hop_ =
        static_cast<size_t>(static_cast<float>(analysis_hop_) * time_stretch_);
    if (synthesis_hop_ < 1)
      synthesis_hop_ = 1;
    if (synthesis_hop_ > grain_size_ / 2)
      synthesis_hop_ = grain_size_ / 2;

    // Overlap length for cross-correlation
    overlap_len_ = analysis_hop_ / 2;
    if (overlap_len_ < 32)
      overlap_len_ = 32;
    if (overlap_len_ > grain_size_ / 4)
      overlap_len_ = grain_size_ / 4;
  }

  void ExtractGrain() {
    size_t start = (input_pos_ >= grain_size_) ? input_pos_ - grain_size_ : 0;
    for (size_t i = 0; i < grain_size_; i++) {
      current_grain_[i] = input_buffer_[(start + i) % (MaxGrain * 2)];
    }

    // Perform overlap-add into output buffer
    size_t optimal_offset = FindOptimalOffset();
    OverlapAdd(optimal_offset);
  }

  size_t FindOptimalOffset() {
    // Simple cross-correlation to find best alignment
    float max_xcorr = -1e10f;
    size_t best_offset = 0;

    for (size_t k = 0; k < overlap_len_; k++) {
      float xcorr = 0.0f;
      for (size_t i = 0; i < overlap_len_; i++) {
        if (k + i < grain_size_ && i < grain_size_) {
          xcorr += current_grain_[i] * overlap_buffer_[synthesis_hop_ + k + i];
        }
      }

      if (xcorr > max_xcorr) {
        max_xcorr = xcorr;
        best_offset = k;
      }
    }

    return best_offset;
  }

  void OverlapAdd(size_t offset) {
    // Clear output buffer
    std::memset(output_buffer_, 0, sizeof(output_buffer_));

    // Apply crossfade
    size_t fade_len = overlap_len_;

    for (size_t i = 0; i < synthesis_hop_; i++) {
      if (i < fade_len) {
        float fade_out =
            1.0f - static_cast<float>(i) / static_cast<float>(fade_len);
        float fade_in = static_cast<float>(i) / static_cast<float>(fade_len);
        output_buffer_[i] = overlap_buffer_[i] * fade_out +
                            current_grain_[offset + i] * fade_in;
      } else {
        output_buffer_[i] = current_grain_[offset + i];
      }
    }

    // Update overlap buffer
    std::memcpy(overlap_buffer_, current_grain_, grain_size_ * sizeof(float));
  }
};

// Common type aliases
using SOLA = SOLATimeStretch<4096, 2048>;
using SOLASmall = SOLATimeStretch<2048, 1024>;

} // namespace daisysp

#endif // DSY_SOLA_TIME_STRETCH_H
