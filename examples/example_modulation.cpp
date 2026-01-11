// Example 3: Modulation Effects
// Demonstrates: Vibrato + Ring Modulator + Stereo Panning
//
// This example shows how to use modulation effects to create
// movement and stereo interest in a signal.

#include "modulation/ringmod.h"
#include "modulation/vibrato.h"
#include "spatial/stereopan.h"
#include <cmath>
#include <iostream>


using namespace daisysp;

// Simulation parameters
constexpr float SAMPLE_RATE = 48000.0f;
constexpr int BUFFER_SIZE = 256;

int main() {
  std::cout << "=== Modulation Effects Example ===" << std::endl;
  std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz" << std::endl;
  std::cout << std::endl;

  // Initialize effects
  Vibrato vibrato;
  vibrato.Init(SAMPLE_RATE);
  vibrato.SetFrequency(5.0f); // 5 Hz modulation rate
  vibrato.SetWidth(0.003f);   // 3 ms depth

  RingMod ringmod;
  ringmod.Init(SAMPLE_RATE);
  ringmod.SetFrequency(50.0f); // 50 Hz carrier for bell-like tone

  StereoPan panner;
  panner.Init();
  // Pan will be modulated in the loop

  std::cout << "Effect Settings:" << std::endl;
  std::cout << "  Vibrato: Rate=" << vibrato.GetFrequency()
            << " Hz, Width=" << (vibrato.GetWidth() * 1000.0f) << " ms"
            << std::endl;
  std::cout << "  Ring Mod: Carrier=" << ringmod.GetFrequency() << " Hz"
            << std::endl;
  std::cout << "  Panner: Auto-pan enabled" << std::endl;
  std::cout << std::endl;

  // Generate test signal (pure 440 Hz sine wave)
  float test_buffer[BUFFER_SIZE];
  float left_buffer[BUFFER_SIZE];
  float right_buffer[BUFFER_SIZE];

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float t = static_cast<float>(i) / SAMPLE_RATE;
    test_buffer[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * t);
  }

  // Process through modulation chain
  float peak_left = 0.0f;
  float peak_right = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float sample = test_buffer[i];

    // Apply vibrato
    sample = vibrato.Process(sample);

    // Apply ring modulation for bell-like harmonics
    sample = ringmod.Process(sample);

    // Apply auto-pan (LFO-controlled panning)
    float t = static_cast<float>(i) / SAMPLE_RATE;
    float pan_lfo =
        0.5f + 0.5f * std::sin(2.0f * M_PI * 0.5f * t); // 0.5 Hz pan
    panner.SetPan(pan_lfo);

    float left, right;
    panner.Process(sample, &left, &right);

    left_buffer[i] = left;
    right_buffer[i] = right;

    peak_left = std::max(peak_left, std::abs(left));
    peak_right = std::max(peak_right, std::abs(right));
  }

  std::cout << "Processing complete!" << std::endl;
  std::cout << "  Peak Left:  " << peak_left << std::endl;
  std::cout << "  Peak Right: " << peak_right << std::endl;
  std::cout << "  Stereo Output: " << BUFFER_SIZE << " samples per channel"
            << std::endl;

  return 0;
}
