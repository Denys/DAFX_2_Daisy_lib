# DSP Effect Test Patterns

This document defines the standard test patterns for all DSP effects in the DAFX_2_Daisy_lib library. Following these patterns ensures consistent, comprehensive testing across all modules.

---

## Overview

Each DSP effect module should have a corresponding test file (e.g., `test_tube.cpp` for `tube.h/tube.cpp`). Tests use Google Test framework and should cover:

1. **Initialization Tests** - Verify default state
2. **Parameter Tests** - Test setters/getters
3. **Processing Tests** - Validate audio processing
4. **Edge Case Tests** - Handle boundary conditions
5. **Performance Tests** - Optional benchmarks

---

## Test File Structure

```cpp
// Unit Tests for [Effect Name]
// Tests initialization, parameter setting, and basic processing

#include <gtest/gtest.h>
#include "[category]/[effect].h"

using namespace daisysp;

class [Effect]Test : public ::testing::Test
{
protected:
    [Effect] effect_;

    void SetUp() override
    {
        // Initialize with standard sample rate
        effect_.Init(48000.0f);
    }
};

// Test cases follow...
```

---

## Standard Test Categories

### 1. Initialization Test

**Purpose:** Verify that the module initializes correctly with expected default values.

```cpp
TEST_F(EffectTest, Initialization)
{
    // Verify default parameters match documented values
    EXPECT_FLOAT_EQ(effect_.GetParam1(), expected_default);
    EXPECT_FLOAT_EQ(effect_.GetParam2(), expected_default);
    // ... verify all parameters
}
```

**Requirements:**
- [ ] All parameters have expected default values
- [ ] No uninitialized memory accessed
- [ ] State variables properly reset

---

### 2. Parameter Setting Tests

**Purpose:** Verify that setters correctly update internal state and getters return accurate values.

```cpp
TEST_F(EffectTest, ParameterSetting)
{
    // Test each parameter
    effect_.SetParam1(value);
    EXPECT_FLOAT_EQ(effect_.GetParam1(), value);
    
    // Test parameter chaining
    effect_.SetParam1(v1).SetParam2(v2);  // If chaining supported
}
```

**Requirements:**
- [ ] All setters update corresponding getters
- [ ] Parameter values stored accurately (use EXPECT_FLOAT_EQ)
- [ ] No side effects on other parameters

---

### 3. Parameter Range Tests

**Purpose:** Verify that parameters accept valid ranges without crashing.

```cpp
TEST_F(EffectTest, ParameterRanges)
{
    // Test minimum valid values
    EXPECT_NO_THROW(effect_.SetParam1(MIN_VALUE));
    
    // Test maximum valid values
    EXPECT_NO_THROW(effect_.SetParam1(MAX_VALUE));
    
    // Test typical values
    EXPECT_NO_THROW(effect_.SetParam1(TYPICAL_VALUE));
}
```

**Requirements:**
- [ ] No crashes on boundary values
- [ ] Clamping works correctly (if implemented)
- [ ] Documented ranges are enforced

---

### 4. Zero Input Test

**Purpose:** Verify silence produces silence (or expected minimal output).

```cpp
TEST_F(EffectTest, ZeroInput)
{
    float in = 0.0f;
    float out = effect_.Process(in);
    
    // Zero input should produce zero output (or very close)
    EXPECT_NEAR(out, 0.0f, 1e-6f);
}
```

**Requirements:**
- [ ] Zero input produces negligible output
- [ ] No DC offset introduced
- [ ] Filters converge to zero

---

### 5. Unity Gain Test

**Purpose:** Verify bypass or unity settings pass signal unchanged.

```cpp
TEST_F(EffectTest, UnityGain)
{
    // Configure for unity/bypass
    effect_.SetMix(0.0f);  // Fully dry
    
    float in = 0.5f;
    float out = effect_.Process(in);
    
    EXPECT_NEAR(out, in, tolerance);
}
```

**Requirements:**
- [ ] Dry mix passes input unchanged
- [ ] Bypass setting available (if applicable)
- [ ] No unintended gain changes

---

### 6. Output Range Test

**Purpose:** Verify outputs stay within valid bounds and are always finite.

```cpp
TEST_F(EffectTest, OutputRange)
{
    // Process a range of inputs
    for (int i = -10; i <= 10; i++)
    {
        float in = static_cast<float>(i) * 0.1f;
        float out = effect_.Process(in);
        
        // Output should be finite
        EXPECT_TRUE(std::isfinite(out)) << "Failed for input: " << in;
        
        // Optional: check bounded output
        EXPECT_GE(out, -MAX_OUTPUT);
        EXPECT_LE(out, MAX_OUTPUT);
    }
}
```

**Requirements:**
- [ ] No NaN values produced
- [ ] No infinity values produced
- [ ] Output within documented range

---

### 7. State Preservation Test

**Purpose:** Verify the effect maintains proper state across samples.

```cpp
TEST_F(EffectTest, StatePreservation)
{
    float out1 = effect_.Process(0.5f);
    float out2 = effect_.Process(0.5f);
    
    // For filters/delays, identical inputs may produce different outputs
    // due to filter state - this is expected behavior
}
```

**Requirements:**
- [ ] State properly maintained between calls
- [ ] No state bleeding between instances
- [ ] Reinitializing clears state

---

### 8. Sample Rate Test

**Purpose:** Verify the effect works at different sample rates.

```cpp
TEST_F(EffectTest, DifferentSampleRates)
{
    EXPECT_NO_THROW(effect_.Init(44100.0f));
    EXPECT_NO_THROW(effect_.Init(48000.0f));
    EXPECT_NO_THROW(effect_.Init(96000.0f));
}
```

**Requirements:**
- [ ] Works at 44.1kHz, 48kHz, 96kHz
- [ ] Frequency-based parameters adjust correctly
- [ ] No crashes on edge sample rates

---

### 9. Dry/Wet Mix Test

**Purpose:** Verify mix control blends dry and wet signals correctly.

```cpp
TEST_F(EffectTest, DryWetMix)
{
    float in = 0.5f;
    
    effect_.SetMix(0.0f);  // Fully dry
    float dry = effect_.Process(in);
    EXPECT_NEAR(dry, in, tolerance);
    
    effect_.SetMix(1.0f);  // Fully wet
    float wet = effect_.Process(in);
    // wet should be processed signal
    
    effect_.SetMix(0.5f);  // 50/50 blend
    float mixed = effect_.Process(in);
    // Should be between dry and wet
}
```

**Requirements:**
- [ ] 0% mix = dry signal only
- [ ] 100% mix = wet signal only
- [ ] Intermediate values blend correctly

---

## Optional Test Categories

### 10. Known Input/Output Test (MATLAB Validation)

**Purpose:** Validate against known reference values from MATLAB implementation.

```cpp
TEST_F(EffectTest, MatlabValidation)
{
    // Test values from MATLAB reference
    const float expected_output = 0.12345f;  // From MATLAB
    const float tolerance_db = 0.5f;         // ±0.5 dB tolerance
    const float tolerance_linear = /* convert dB to linear */;
    
    effect_.SetParam1(matlab_param1);
    float output = effect_.Process(matlab_input);
    
    EXPECT_NEAR(output, expected_output, tolerance_linear);
}
```

---

### 11. Stress Test

**Purpose:** Verify stability under extreme conditions.

```cpp
TEST_F(EffectTest, StressTest)
{
    // Process many samples
    for (int i = 0; i < 1000000; i++)
    {
        float in = /* pseudo-random or sweep */;
        float out = effect_.Process(in);
        EXPECT_TRUE(std::isfinite(out));
    }
}
```

---

## Test Naming Convention

- Test fixtures: `[Effect]Test` (e.g., `TubeTest`, `VibratoTest`)
- Test cases: Descriptive names in PascalCase
  - `Initialization`
  - `ParameterSetting`
  - `ParameterRanges`
  - `ZeroInput`
  - `UnityGain`
  - `OutputRange`
  - `StatePreservation`
  - `DifferentSampleRates`
  - `DryWetMix`
  - `MatlabValidation`

---

## Tolerance Guidelines

| Test Type | Recommended Tolerance |
|-----------|----------------------|
| Exact values | `EXPECT_FLOAT_EQ` |
| Processed audio | `EXPECT_NEAR(..., 1e-6f)` |
| MATLAB validation | `±0.5 dB` linear equivalent |
| Boundary checks | `EXPECT_NEAR(..., 0.1f)` |

---

## Running Tests

### Build and Run All Tests

```bash
# From project root
cd tests
./run_tests.sh        # Linux/macOS
run_tests.cmd         # Windows
```

### Run Specific Tests

```bash
./run_tests.sh --filter "TubeTest.*"
./run_tests.sh --filter "*Initialization*"
```

### Verbose Output

```bash
./run_tests.sh --verbose
```

---

## Checklist for New Effects

When creating tests for a new effect:

- [ ] Create `test_[effect].cpp` in `tests/` directory
- [ ] Include proper headers and namespace
- [ ] Create test fixture class
- [ ] Implement Initialization test
- [ ] Implement ParameterSetting test
- [ ] Implement ParameterRanges test
- [ ] Implement ZeroInput test
- [ ] Implement OutputRange test
- [ ] Add test file to `tests/CMakeLists.txt`
- [ ] Verify all tests pass before committing

---

## Reference Implementation

See [`test_tube.cpp`](test_tube.cpp) for a complete example implementation following these patterns.
