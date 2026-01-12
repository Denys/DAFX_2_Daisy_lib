// # Crosstalk Canceller
// Stereo loudspeaker crosstalk cancellation using HRIR-based filtering
//
// Reduces acoustic crosstalk between stereo loudspeakers by applying
// inverse HRIR filters. The result improves stereo image localization.
//
// ## Example
// ~~~~
// CrosstalkCanceller ctc;
// ctc.Init(48000.0f);
// ctc.SetSpeakerAngle(10.0f);  // ±10° speaker spacing
// float left_out, right_out;
// ctc.Process(left_in, right_in, &left_out, &right_out);
// ~~~~
//
// ## References
// - DAFX 2nd Ed., Chapter 5, crosstalkcanceler.m
// - Poletti, M. - Crosstalk Cancellation Systems
#pragma once
#ifndef DSY_CROSSTALK_CANCELLER_H
#define DSY_CROSSTALK_CANCELLER_H

#include "../utility/fft_handler.h"
#include "../utility/simple_hrir.h"
#include <cmath>
#include <cstddef>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {

/**
 * @brief Crosstalk cancellation for stereo loudspeakers
 *
 * Uses regularized inverse HRIR matrix to cancel acoustic crosstalk.
 * Requires stereo input (binaural signal) and outputs signals for
 * left and right loudspeakers.
 *
 * @tparam HRIR_LENGTH Length of HRIR filters (default 256)
 */
template <size_t HRIR_LENGTH = 256> class CrosstalkCanceller {
public:
  static constexpr size_t FFT_SIZE = 512; // Next power of 2

  CrosstalkCanceller()
      : sample_rate_(48000.0f), speaker_angle_(10.0f), regularization_(1e-5f),
        input_pos_(0) {}

  /**
   * @brief Initialize the crosstalk canceller
   *
   * @param sample_rate Sample rate in Hz
   */
  void Init(float sample_rate) {
    sample_rate_ = sample_rate;
    input_pos_ = 0;

    // Initialize FFT
    fft_.Init();

    // Initialize HRIR generator
    hrir_gen_.Init(sample_rate);

    // Clear buffers
    std::memset(left_buffer_, 0, sizeof(left_buffer_));
    std::memset(right_buffer_, 0, sizeof(right_buffer_));
    std::memset(left_out_buffer_, 0, sizeof(left_out_buffer_));
    std::memset(right_out_buffer_, 0, sizeof(right_out_buffer_));
    std::memset(left_overlap_, 0, sizeof(left_overlap_));
    std::memset(right_overlap_, 0, sizeof(right_overlap_));

    // Compute inverse filter matrix
    ComputeInverseFilters();
  }

  /**
   * @brief Set speaker angle (half-angle from center)
   *
   * @param angle Speaker angle in degrees (typical: 5-30°)
   */
  void SetSpeakerAngle(float angle) {
    if (angle != speaker_angle_) {
      speaker_angle_ = angle;
      ComputeInverseFilters();
    }
  }

  /**
   * @brief Set regularization factor
   *
   * Higher values = more stable but less effective cancellation.
   *
   * @param beta Regularization factor (typical: 1e-5 to 1e-3)
   */
  void SetRegularization(float beta) {
    if (beta != regularization_) {
      regularization_ = beta;
      ComputeInverseFilters();
    }
  }

  /**
   * @brief Get current speaker angle
   */
  float GetSpeakerAngle() const { return speaker_angle_; }

  /**
   * @brief Process stereo samples
   *
   * @param left_in Left channel input (binaural left ear)
   * @param right_in Right channel input (binaural right ear)
   * @param left_out Left loudspeaker output
   * @param right_out Right loudspeaker output
   */
  void Process(float left_in, float right_in, float *left_out,
               float *right_out) {
    // Store inputs
    left_buffer_[input_pos_] = left_in;
    right_buffer_[input_pos_] = right_in;

    // Get outputs
    *left_out = left_out_buffer_[input_pos_] + left_overlap_[input_pos_];
    *right_out = right_out_buffer_[input_pos_] + right_overlap_[input_pos_];

    // Clear for next block
    left_out_buffer_[input_pos_] = 0.0f;
    right_out_buffer_[input_pos_] = 0.0f;
    left_overlap_[input_pos_] = 0.0f;
    right_overlap_[input_pos_] = 0.0f;

    input_pos_++;

    // Process block when buffer is full
    if (input_pos_ >= HRIR_LENGTH) {
      ProcessBlock();
      input_pos_ = 0;
    }
  }

private:
  float sample_rate_;
  float speaker_angle_;
  float regularization_;
  size_t input_pos_;

  // FFT and HRIR
  FFTHandler<FFT_SIZE> fft_;
  SimpleHRIR<HRIR_LENGTH> hrir_gen_;

  // Input buffers
  float left_buffer_[HRIR_LENGTH];
  float right_buffer_[HRIR_LENGTH];

  // Output buffers
  float left_out_buffer_[HRIR_LENGTH];
  float right_out_buffer_[HRIR_LENGTH];
  float left_overlap_[HRIR_LENGTH];
  float right_overlap_[HRIR_LENGTH];

  // Inverse filter matrix H (2x2 in frequency domain)
  // H_11, H_12, H_21, H_22 for each frequency bin
  float H_11_real_[FFT_SIZE];
  float H_11_imag_[FFT_SIZE];
  float H_12_real_[FFT_SIZE];
  float H_12_imag_[FFT_SIZE];
  float H_21_real_[FFT_SIZE];
  float H_21_imag_[FFT_SIZE];
  float H_22_real_[FFT_SIZE];
  float H_22_imag_[FFT_SIZE];

  // Working buffers
  float work1_real_[FFT_SIZE];
  float work1_imag_[FFT_SIZE];
  float work2_real_[FFT_SIZE];
  float work2_imag_[FFT_SIZE];
  float time_buffer_[FFT_SIZE];

  /**
   * @brief Compute inverse HRIR matrix in frequency domain
   */
  void ComputeInverseFilters() {
    // Generate HRIRs for both ears and both speakers
    // C[ear][speaker] where:
    //   C[0][0] = left ear from left speaker (ipsilateral)
    //   C[0][1] = left ear from right speaker (contralateral)
    //   C[1][0] = right ear from left speaker (contralateral)
    //   C[1][1] = right ear from right speaker (ipsilateral)

    float hrir_ll[HRIR_LENGTH]; // left ear, left speaker
    float hrir_lr[HRIR_LENGTH]; // left ear, right speaker
    float hrir_rl[HRIR_LENGTH]; // right ear, left speaker
    float hrir_rr[HRIR_LENGTH]; // right ear, right speaker

    // Left speaker at +angle, right speaker at -angle
    hrir_gen_.Generate(speaker_angle_ / 2.0f, hrir_ll);  // Left ear from left
    hrir_gen_.Generate(-speaker_angle_ / 2.0f, hrir_lr); // Left ear from right

    // Right ear is symmetric
    std::memcpy(hrir_rl, hrir_lr, HRIR_LENGTH * sizeof(float));
    std::memcpy(hrir_rr, hrir_ll, HRIR_LENGTH * sizeof(float));

    // Zero-pad to FFT size and compute FFT
    float padded[FFT_SIZE];
    float C_11_real[FFT_SIZE], C_11_imag[FFT_SIZE];
    float C_12_real[FFT_SIZE], C_12_imag[FFT_SIZE];
    float C_21_real[FFT_SIZE], C_21_imag[FFT_SIZE];
    float C_22_real[FFT_SIZE], C_22_imag[FFT_SIZE];

    auto PadAndFFT = [&](const float *hrir, float *real, float *imag) {
      std::memset(padded, 0, sizeof(padded));
      std::memcpy(padded, hrir, HRIR_LENGTH * sizeof(float));
      fft_.Forward(padded, real, imag);
    };

    PadAndFFT(hrir_ll, C_11_real, C_11_imag);
    PadAndFFT(hrir_lr, C_12_real, C_12_imag);
    PadAndFFT(hrir_rl, C_21_real, C_21_imag);
    PadAndFFT(hrir_rr, C_22_real, C_22_imag);

    // Regularized inversion: H = inv(C' * C + beta * I) * C'
    // For 2x2 matrix, we can compute directly
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      // C at this frequency bin
      float c11_r = C_11_real[k], c11_i = C_11_imag[k];
      float c12_r = C_12_real[k], c12_i = C_12_imag[k];
      float c21_r = C_21_real[k], c21_i = C_21_imag[k];
      float c22_r = C_22_real[k], c22_i = C_22_imag[k];

      // C' (conjugate transpose)
      float ct11_r = c11_r, ct11_i = -c11_i;
      float ct12_r = c21_r, ct12_i = -c21_i;
      float ct21_r = c12_r, ct21_i = -c12_i;
      float ct22_r = c22_r, ct22_i = -c22_i;

      // A = C' * C + beta * I
      float a11_r, a11_i, a12_r, a12_i, a21_r, a21_i, a22_r, a22_i;

      // A11 = ct11*c11 + ct12*c21 + beta
      ComplexMul(ct11_r, ct11_i, c11_r, c11_i, a11_r, a11_i);
      float temp_r, temp_i;
      ComplexMul(ct12_r, ct12_i, c21_r, c21_i, temp_r, temp_i);
      a11_r += temp_r + regularization_;
      a11_i += temp_i;

      // A12 = ct11*c12 + ct12*c22
      ComplexMul(ct11_r, ct11_i, c12_r, c12_i, a12_r, a12_i);
      ComplexMul(ct12_r, ct12_i, c22_r, c22_i, temp_r, temp_i);
      a12_r += temp_r;
      a12_i += temp_i;

      // A21 = ct21*c11 + ct22*c21
      ComplexMul(ct21_r, ct21_i, c11_r, c11_i, a21_r, a21_i);
      ComplexMul(ct22_r, ct22_i, c21_r, c21_i, temp_r, temp_i);
      a21_r += temp_r;
      a21_i += temp_i;

      // A22 = ct21*c12 + ct22*c22 + beta
      ComplexMul(ct21_r, ct21_i, c12_r, c12_i, a22_r, a22_i);
      ComplexMul(ct22_r, ct22_i, c22_r, c22_i, temp_r, temp_i);
      a22_r += temp_r + regularization_;
      a22_i += temp_i;

      // Invert 2x2 matrix A
      // det = a11*a22 - a12*a21
      float det_r, det_i;
      ComplexMul(a11_r, a11_i, a22_r, a22_i, det_r, det_i);
      ComplexMul(a12_r, a12_i, a21_r, a21_i, temp_r, temp_i);
      det_r -= temp_r;
      det_i -= temp_i;

      // Inverse of det
      float det_mag_sq = det_r * det_r + det_i * det_i;
      if (det_mag_sq < 1e-10f)
        det_mag_sq = 1e-10f;
      float inv_det_r = det_r / det_mag_sq;
      float inv_det_i = -det_i / det_mag_sq;

      // A_inv = [a22, -a12; -a21, a11] / det
      float ai11_r, ai11_i, ai12_r, ai12_i, ai21_r, ai21_i, ai22_r, ai22_i;
      ComplexMul(a22_r, a22_i, inv_det_r, inv_det_i, ai11_r, ai11_i);
      ComplexMul(-a12_r, -a12_i, inv_det_r, inv_det_i, ai12_r, ai12_i);
      ComplexMul(-a21_r, -a21_i, inv_det_r, inv_det_i, ai21_r, ai21_i);
      ComplexMul(a11_r, a11_i, inv_det_r, inv_det_i, ai22_r, ai22_i);

      // H = A_inv * C'
      // H11 = ai11*ct11 + ai12*ct21
      ComplexMul(ai11_r, ai11_i, ct11_r, ct11_i, H_11_real_[k], H_11_imag_[k]);
      ComplexMul(ai12_r, ai12_i, ct21_r, ct21_i, temp_r, temp_i);
      H_11_real_[k] += temp_r;
      H_11_imag_[k] += temp_i;

      // H12 = ai11*ct12 + ai12*ct22
      ComplexMul(ai11_r, ai11_i, ct12_r, ct12_i, H_12_real_[k], H_12_imag_[k]);
      ComplexMul(ai12_r, ai12_i, ct22_r, ct22_i, temp_r, temp_i);
      H_12_real_[k] += temp_r;
      H_12_imag_[k] += temp_i;

      // H21 = ai21*ct11 + ai22*ct21
      ComplexMul(ai21_r, ai21_i, ct11_r, ct11_i, H_21_real_[k], H_21_imag_[k]);
      ComplexMul(ai22_r, ai22_i, ct21_r, ct21_i, temp_r, temp_i);
      H_21_real_[k] += temp_r;
      H_21_imag_[k] += temp_i;

      // H22 = ai21*ct12 + ai22*ct22
      ComplexMul(ai21_r, ai21_i, ct12_r, ct12_i, H_22_real_[k], H_22_imag_[k]);
      ComplexMul(ai22_r, ai22_i, ct22_r, ct22_i, temp_r, temp_i);
      H_22_real_[k] += temp_r;
      H_22_imag_[k] += temp_i;
    }
  }

  /**
   * @brief Complex multiplication helper
   */
  static void ComplexMul(float a_r, float a_i, float b_r, float b_i,
                         float &out_r, float &out_i) {
    out_r = a_r * b_r - a_i * b_i;
    out_i = a_r * b_i + a_i * b_r;
  }

  /**
   * @brief Process one block
   */
  void ProcessBlock() {
    // Zero-pad inputs
    float left_padded[FFT_SIZE];
    float right_padded[FFT_SIZE];
    std::memset(left_padded, 0, sizeof(left_padded));
    std::memset(right_padded, 0, sizeof(right_padded));
    std::memcpy(left_padded, left_buffer_, HRIR_LENGTH * sizeof(float));
    std::memcpy(right_padded, right_buffer_, HRIR_LENGTH * sizeof(float));

    // Forward FFT
    float left_real[FFT_SIZE], left_imag[FFT_SIZE];
    float right_real[FFT_SIZE], right_imag[FFT_SIZE];
    fft_.Forward(left_padded, left_real, left_imag);
    fft_.Forward(right_padded, right_real, right_imag);

    // Apply inverse matrix: [out_L; out_R] = H * [in_L; in_R]
    for (size_t k = 0; k < FFT_SIZE; ++k) {
      float in_l_r = left_real[k], in_l_i = left_imag[k];
      float in_r_r = right_real[k], in_r_i = right_imag[k];

      // out_L = H11 * in_L + H12 * in_R
      float temp1_r, temp1_i, temp2_r, temp2_i;
      ComplexMul(H_11_real_[k], H_11_imag_[k], in_l_r, in_l_i, temp1_r,
                 temp1_i);
      ComplexMul(H_12_real_[k], H_12_imag_[k], in_r_r, in_r_i, temp2_r,
                 temp2_i);
      work1_real_[k] = temp1_r + temp2_r;
      work1_imag_[k] = temp1_i + temp2_i;

      // out_R = H21 * in_L + H22 * in_R
      ComplexMul(H_21_real_[k], H_21_imag_[k], in_l_r, in_l_i, temp1_r,
                 temp1_i);
      ComplexMul(H_22_real_[k], H_22_imag_[k], in_r_r, in_r_i, temp2_r,
                 temp2_i);
      work2_real_[k] = temp1_r + temp2_r;
      work2_imag_[k] = temp1_i + temp2_i;
    }

    // Inverse FFT
    float left_time[FFT_SIZE], right_time[FFT_SIZE];
    fft_.Inverse(work1_real_, work1_imag_, left_time);
    fft_.Inverse(work2_real_, work2_imag_, right_time);

    // Overlap-add
    for (size_t i = 0; i < HRIR_LENGTH; ++i) {
      left_out_buffer_[i] = left_time[i] + left_overlap_[i];
      right_out_buffer_[i] = right_time[i] + right_overlap_[i];
      left_overlap_[i] = left_time[i + HRIR_LENGTH];
      right_overlap_[i] = right_time[i + HRIR_LENGTH];
    }
  }
};

} // namespace daisysp

#endif // DSY_CROSSTALK_CANCELLER_H
