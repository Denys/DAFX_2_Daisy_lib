#include "wahwah.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {
void WahWah::Init(float sample_rate) {
  sample_rate_ = sample_rate;
  freq_ = 500.0f;
  q_ = 5.0f;
  depth_ = 1.0f;

  xnm1_ = 0.0f;
  xnm2_ = 0.0f;
  ynm1_ = 0.0f;
  ynm2_ = 0.0f;

  RecalculateCoefficients();
}

void WahWah::RecalculateCoefficients() {
  // Calculate bandpass filter coefficients
  // Using second-order bandpass filter
  float wc = 2.0f * M_PI * freq_ / sample_rate_;
  (void)wc; // Used in cos/sin below
  float alpha = std::sin(wc) / (2.0f * q_);

  // Coefficients for modulated bandpass filter
  b0_ = alpha;
  b1_ = 0.0f;
  b2_ = -alpha;
  a0_ = 1.0f;
  a1_ = -2.0f * std::cos(wc);
  a2_ = 1.0f;
}

float WahWah::Process(const float &in) {
  // Modulate center frequency with LFO
  float mod =
      0.5f * (1.0f + depth_ * std::sin(2.0f * M_PI * freq_ / sample_rate_));

  // Apply modulated coefficients
  float wc_mod = 2.0f * M_PI * freq_ * mod / sample_rate_;
  float alpha_mod = std::sin(wc_mod) / (2.0f * q_);

  float b0_mod = alpha_mod;
  float b1_mod = 0.0f;
  float b2_mod = -alpha_mod;
  float a1_mod = -2.0f * std::cos(wc_mod);

  // Second-order bandpass filter
  float y0 = b0_mod * in + b1_mod * xnm1_ + b2_mod * xnm2_ - a1_mod * ynm1_ -
             a2_ * ynm2_;

  // Update state
  xnm2_ = xnm1_;
  xnm1_ = in;
  ynm2_ = ynm1_;
  ynm1_ = y0;

  return y0;
}

} // namespace daisysp
