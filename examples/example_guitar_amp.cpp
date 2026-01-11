// Example 1: Guitar Amp Simulation
// Demonstrates: Tube distortion + Tone Stack + CryBaby Wah
//
// This example shows how to chain multiple effects to create
// a complete guitar amplifier simulation.

#include "effects/tonestack.h"
#include "effects/tube.h"
#include "effects/wahwah.h"
#include <cmath>
#include <iostream>


using namespace daisysp;

// Simulation parameters
constexpr float SAMPLE_RATE = 48000.0f;
constexpr int BUFFER_SIZE = 256;

int main() {
  std::cout << "=== Guitar Amp Simulation Example ===" << std::endl;
  std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz" << std::endl;
  std::cout << "Buffer Size: " << BUFFER_SIZE << " samples" << std::endl;
  std::cout << std::endl;

  // Initialize effects
  Tube tube;
  tube.Init(SAMPLE_RATE);
  tube.SetDrive(2.0f); // Moderate overdrive
  tube.SetBias(0.1f);  // Slight asymmetric clipping
  tube.SetDistortion(1.5f);
  tube.SetMix(1.0f);

  ToneStack tonestack;
  tonestack.Init(SAMPLE_RATE);
  tonestack.SetBass(0.6f);   // Slight bass boost
  tonestack.SetMid(0.4f);    // Scooped mids
  tonestack.SetTreble(0.7f); // Bright treble

  WahWah wah;
  wah.Init(SAMPLE_RATE);
  wah.SetFrequency(1.0f); // Slow auto-wah
  wah.SetDepth(0.6f);
  wah.SetQ(5.0f);

  std::cout << "Effect Chain:" << std::endl;
  std::cout << "  1. Tube Distortion (Drive: " << tube.GetDrive() << ")"
            << std::endl;
  std::cout << "  2. Tone Stack (B:" << tonestack.GetBass()
            << " M:" << tonestack.GetMid() << " T:" << tonestack.GetTreble()
            << ")" << std::endl;
  std::cout << "  3. Auto-Wah (Rate: " << wah.GetFrequency() << " Hz)"
            << std::endl;
  std::cout << std::endl;

  // Generate test signal (440 Hz sine wave with harmonics - simulated guitar)
  float test_buffer[BUFFER_SIZE];
  float output_buffer[BUFFER_SIZE];

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float t = static_cast<float>(i) / SAMPLE_RATE;
    // Fundamental + harmonics (guitar-like)
    test_buffer[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * t) +
                     0.25f * std::sin(2.0f * M_PI * 880.0f * t) +
                     0.125f * std::sin(2.0f * M_PI * 1320.0f * t);
  }

  // Process through effect chain
  float peak_in = 0.0f;
  float peak_out = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float sample = test_buffer[i];
    peak_in = std::max(peak_in, std::abs(sample));

    // Process chain: Tube -> ToneStack -> Wah
    sample = tube.Process(sample);
    sample = tonestack.Process(sample);
    sample = wah.Process(sample);

    output_buffer[i] = sample;
    peak_out = std::max(peak_out, std::abs(sample));
  }

  std::cout << "Processing complete!" << std::endl;
  std::cout << "  Peak Input:  " << peak_in << std::endl;
  std::cout << "  Peak Output: " << peak_out << std::endl;
  std::cout << "  Gain: " << (20.0f * std::log10(peak_out / peak_in)) << " dB"
            << std::endl;

  return 0;
}
