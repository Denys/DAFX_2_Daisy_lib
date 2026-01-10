#include "tonestack.h"
#include <cmath>

namespace daisysp
{
void ToneStack::Init(float sample_rate)
{
    sample_rate_ = sample_rate;
    bass_ = 0.0f;
    middle_ = 0.0f;
    treble_ = 0.0f;

    RecalculateCoefficients();
}

void ToneStack::RecalculateCoefficients()
{
    // Calculate gains from control parameters (-1 to +1 range)
    // Using simple shelving filter approximation
    bass_gain_ = std::pow(10.0f, bass_ * 6.0f / 20.0f);  // ±6 dB range
    middle_gain_ = std::pow(10.0f, middle_ * 3.0f / 20.0f);  // ±3 dB range
    treble_gain_ = std::pow(10.0f, treble_ * 6.0f / 20.0f);  // ±6 dB range
}

float ToneStack::Process(const float &in)
{
    // Apply tone stack using simple gain multiplication
    // This is a simplified implementation - a real tone stack would use
    // more complex circuit modeling with filters
    float out = in * bass_gain_ * middle_gain_ * treble_gain_;

    return out;
}

} // namespace daisysp
