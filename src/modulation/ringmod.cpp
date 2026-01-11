#include "ringmod.h"
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

namespace daisysp {
void RingModulator::Init(float sample_rate) {
  sample_rate_ = sample_rate;
  freq_ = 440.0f;
  depth_ = 1.0f;
  phase_ = 0.0f;

  RecalculatePhaseIncrement();
}

void RingModulator::RecalculatePhaseIncrement() {
  // Calculate phase increment for sine wave oscillator
  // phase_increment = 2 * pi * frequency / sample_rate
  phase_increment_ = 2.0f * M_PI * freq_ / sample_rate_;
}

float RingModulator::Process(const float &in) {
  // Generate sine wave for modulation
  float mod = std::sin(phase_);

  // Update phase
  phase_ += phase_increment_;
  if (phase_ >= 2.0f * M_PI) {
    phase_ -= 2.0f * M_PI;
  }

  // Ring modulation: multiply input by modulated sine wave
  float out = in * (1.0f + depth_ * mod);

  return out;
}

} // namespace daisysp
