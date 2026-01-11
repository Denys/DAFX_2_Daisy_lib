#include "vibrato.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {
void Vibrato::Init(float sample_rate) {
  sample_rate_ = sample_rate;
  freq_ = 5.0f;
  width_ = 0.005f;
  write_ptr_ = 0;

  RecalculateCoefficients();

  // Allocate delay line
  delay_line_ = new float[delay_line_size_];
  std::memset(delay_line_, 0, delay_line_size_ * sizeof(float));
}

void Vibrato::RecalculateCoefficients() {
  // Calculate delay in samples
  delay_samples_ = static_cast<int>(width_ * sample_rate_ + 0.5f);

  // Calculate modulation width in samples
  width_samples_ = static_cast<int>(width_ * sample_rate_ + 0.5f);

  // Calculate modulation frequency in samples
  mod_freq_samples_ = freq_ / sample_rate_;

  // Calculate delay line size: 2 + DELAY + WIDTH * 2
  delay_line_size_ = 2 + delay_samples_ + width_samples_ * 2;

  // Validate width is not greater than delay
  if (width_samples_ > delay_samples_) {
    width_samples_ = delay_samples_;
  }
}

float Vibrato::Process(const float &in) {
  // Write input to delay line
  delay_line_[write_ptr_] = in;

  // Calculate modulation
  float mod = std::sin(2.0f * M_PI * mod_freq_samples_ * write_ptr_);

  // Calculate tap position
  float tap = 1.0f + delay_samples_ + width_samples_ * mod;

  // Calculate integer and fractional parts
  int i = static_cast<int>(std::floor(tap));
  float frac = tap - i;

  // Handle circular buffer wrap
  int read_ptr = (write_ptr_ - i + delay_line_size_) % delay_line_size_;
  int read_ptr_prev = (read_ptr - 1 + delay_line_size_) % delay_line_size_;

  // Linear interpolation
  float out =
      delay_line_[read_ptr] * frac + delay_line_[read_ptr_prev] * (1.0f - frac);

  // Increment write pointer
  write_ptr_ = (write_ptr_ + 1) % delay_line_size_;

  return out;
}

} // namespace daisysp
