#include "vibrato.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {
void Vibrato::Init(float sample_rate, float max_width_seconds) {
  // Nothing is committed to the object until the allocation has succeeded.
  // Assigning sample_rate_ first and then throwing would leave the new rate
  // installed against the old, smaller buffer; RecalculateCoefficients()
  // would then derive tap offsets from the new rate while clamping the
  // length to the old capacity, and Process() would index outside it.
  float rate = sample_rate_;
  if (std::isfinite(sample_rate) && sample_rate > 0.0f) {
    // An absurdly high rate is undefined before it is merely absurd: the
    // delay length is held in an int, and converting 0.1 * 1e12 to one is
    // outside its range.
    rate = (sample_rate > kMaxSampleRate) ? kMaxSampleRate : sample_rate;
  }

  float reserve = max_width_seconds;
  if (!(reserve >= 0.0f)) { // also catches NaN
    reserve = 0.0f;
  }
  if (reserve > kMaxWidthSeconds) {
    reserve = kMaxWidthSeconds;
  }

  // Size the allocation once, for the width the caller reserved. SetWidth()
  // may be called at any time, including from the audio thread, and must not
  // have to reallocate - so the reserved width, not the current one, decides
  // the footprint.
  const int max_delay = static_cast<int>(reserve * rate + 0.5f);
  const int capacity = 2 + max_delay + max_delay * 2;

  // Allocate before freeing. Deleting first would leave delay_line_ dangling
  // if the new[] threw, and the destructor would then free it a second time.
  if (delay_line_capacity_ != capacity) {
    float *replacement = new float[capacity];
    delete[] delay_line_;
    delay_line_ = replacement;
    delay_line_capacity_ = capacity;
  }

  // Past this point nothing can throw, so the state can be committed.
  sample_rate_ = rate;
  reserved_width_ = reserve;
  freq_ = 5.0f;
  width_ = (0.005f > reserve) ? reserve : 0.005f;
  write_ptr_ = 0;
  std::memset(delay_line_, 0, delay_line_capacity_ * sizeof(float));

  RecalculateCoefficients();
}

void Vibrato::RecalculateCoefficients() {
  // Clamp to the range Init() allocated for. Without this, a width inside the
  // documented 0.0001-0.1 s range but larger than the one Init() saw used to
  // grow delay_line_size_ past the allocation and Process() ran off the end
  // of the heap block.
  if (!(width_ >= 0.0f)) { // also catches NaN
    width_ = 0.0f;
  }
  if (width_ > reserved_width_) {
    width_ = reserved_width_;
  }

  // Calculate delay in samples
  delay_samples_ = static_cast<int>(width_ * sample_rate_ + 0.5f);

  // Calculate modulation width in samples
  width_samples_ = static_cast<int>(width_ * sample_rate_ + 0.5f);

  // Calculate modulation frequency in samples
  if (!std::isfinite(freq_)) {
    freq_ = 0.0f;
  }
  mod_freq_samples_ = freq_ / sample_rate_;

  // Calculate delay line size: 2 + DELAY + WIDTH * 2
  delay_line_size_ = 2 + delay_samples_ + width_samples_ * 2;

  // Belt and braces: the clamp above already guarantees this, but the buffer
  // index in Process() is taken modulo delay_line_size_, so it must never
  // exceed what was allocated.
  if (delay_line_capacity_ > 0 && delay_line_size_ > delay_line_capacity_) {
    delay_line_size_ = delay_line_capacity_;
  }

  // Validate width is not greater than delay
  if (width_samples_ > delay_samples_) {
    width_samples_ = delay_samples_;
  }
}

float Vibrato::Process(const float &in) {
  // Init() has not run: there is no delay line to read or write.
  if (delay_line_ == nullptr) {
    return 0.0f;
  }

  // Write input to delay line
  delay_line_[write_ptr_] = in;

  // Calculate modulation
  float mod = std::sin(2.0f * M_PI * mod_freq_samples_ * write_ptr_);

  // Calculate tap position
  float tap = 1.0f + delay_samples_ + width_samples_ * mod;

  // Backstop for the whole class of faults this file had: a non-finite tap
  // casts to INT_MIN, the subtraction below then overflows, and the result
  // indexes outside the delay line. The parameter guards above close every
  // route to this that is known; this closes the ones that are not.
  if (!std::isfinite(tap)) {
    tap = 1.0f;
  }

  // Calculate integer and fractional parts
  int i = static_cast<int>(std::floor(tap));
  float frac = tap - i;

  // Backstop on the index itself. The clamps above keep the tap inside the
  // delay line for every state this class can reach, but a negative or
  // oversized i would make the remainder below negative and index outside the
  // allocation - so bound it here rather than rely on that reasoning holding
  // for every future change.
  if (i < 0) {
    i = 0;
  }
  if (i >= delay_line_size_) {
    i = delay_line_size_ - 1;
  }

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
