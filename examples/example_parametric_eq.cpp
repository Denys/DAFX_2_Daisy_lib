// Example 2: Parametric EQ
// Demonstrates: Low Shelving + High Shelving + Peak Filter
//
// This example shows how to create a 3-band parametric EQ
// using the filter modules.

#include "filters/highshelving.h"
#include "filters/lowshelving.h"
#include "filters/peakfilter.h"
#include <cmath>
#include <iostream>


using namespace daisysp;

// Simulation parameters
constexpr float SAMPLE_RATE = 48000.0f;
constexpr int BUFFER_SIZE = 256;

int main() {
  std::cout << "=== 3-Band Parametric EQ Example ===" << std::endl;
  std::cout << "Sample Rate: " << SAMPLE_RATE << " Hz" << std::endl;
  std::cout << std::endl;

  // Initialize filters
  LowShelving lowShelf;
  lowShelf.Init(SAMPLE_RATE);
  lowShelf.SetFrequency(100.0f); // 100 Hz crossover
  lowShelf.SetGain(3.0f);        // +3 dB bass boost

  PeakFilter midPeak;
  midPeak.Init(SAMPLE_RATE);
  midPeak.SetFrequency(1000.0f); // 1 kHz center
  midPeak.SetQ(1.0f);            // Wide bandwidth
  midPeak.SetGain(-2.0f);        // -2 dB mid cut

  HighShelving highShelf;
  highShelf.Init(SAMPLE_RATE);
  highShelf.SetFrequency(4000.0f); // 4 kHz crossover
  highShelf.SetGain(2.0f);         // +2 dB treble boost

  std::cout << "EQ Settings:" << std::endl;
  std::cout << "  Low Shelf:  " << lowShelf.GetFrequency() << " Hz, "
            << lowShelf.GetGain() << " dB" << std::endl;
  std::cout << "  Mid Peak:   " << midPeak.GetFrequency()
            << " Hz, Q=" << midPeak.GetQ() << ", " << midPeak.GetGain() << " dB"
            << std::endl;
  std::cout << "  High Shelf: " << highShelf.GetFrequency() << " Hz, "
            << highShelf.GetGain() << " dB" << std::endl;
  std::cout << std::endl;

  // Test with white noise approximation
  float test_buffer[BUFFER_SIZE];
  float output_buffer[BUFFER_SIZE];

  // Simple pseudo-random noise
  unsigned int seed = 12345;
  for (int i = 0; i < BUFFER_SIZE; i++) {
    seed = seed * 1103515245 + 12345;
    test_buffer[i] = (static_cast<float>(seed) / 2147483648.0f) - 1.0f;
    test_buffer[i] *= 0.3f; // Scale down
  }

  // Process through EQ chain
  float rms_in = 0.0f;
  float rms_out = 0.0f;

  for (int i = 0; i < BUFFER_SIZE; i++) {
    float sample = test_buffer[i];
    rms_in += sample * sample;

    // Process chain: LowShelf -> MidPeak -> HighShelf
    sample = lowShelf.Process(sample);
    sample = midPeak.Process(sample);
    sample = highShelf.Process(sample);

    output_buffer[i] = sample;
    rms_out += sample * sample;
  }

  rms_in = std::sqrt(rms_in / BUFFER_SIZE);
  rms_out = std::sqrt(rms_out / BUFFER_SIZE);

  std::cout << "Processing complete!" << std::endl;
  std::cout << "  RMS Input:  " << rms_in << std::endl;
  std::cout << "  RMS Output: " << rms_out << std::endl;
  std::cout << "  Average Gain: " << (20.0f * std::log10(rms_out / rms_in))
            << " dB" << std::endl;

  return 0;
}
