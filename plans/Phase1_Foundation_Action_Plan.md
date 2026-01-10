# Phase 1: Foundation - Action Plan

**Date:** 2026-01-10
**Version:** 1.0
**Status:** Active

---

## Executive Summary

Phase 1: Foundation establishes the core infrastructure and implements 10 essential DSP algorithms from the DAFX textbook. This phase creates a solid groundwork for the DAFX_2_Daisy_lib project, ensuring proper build systems, testing frameworks, and style guide compliance before advancing to Phase 2.

---

## 1. Core Objectives

### 1.1 Primary Goals

| Objective | Description | Success Metric |
|-----------|-------------|----------------|
| **Infrastructure Setup** | Establish build system, directory structure, and testing framework | CMake builds successfully, tests run |
| **Style Guide Compliance** | Ensure all code follows DaisySP style guide | Zero compiler warnings, style check passes |
| **Algorithm Implementation** | Implement 10 core DSP effects from Phase 1 | All 10 effects compile and pass tests |
| **Documentation** | Complete documentation for all modules | 100% of modules have header docs |
| **Validation** | Verify accuracy against MATLAB references | All effects within ±0.5 dB tolerance |

### 1.2 Alignment with Project Goals

- **Hardware Independence**: All modules are pure DSP, no hardware dependencies
- **DaisySP Compatibility**: Follows established API patterns and conventions
- **Real-time Performance**: Optimized for ARM Cortex-M7 at 96kHz/24-bit
- **Open Source**: Extensible codebase with clear documentation

---

## 2. Resource Assessment

### 2.1 Existing Resources

| Resource | Location | Status |
|----------|----------|--------|
| **Tube Distortion** | `src/effects/tube.cpp`, `src/effects/tube.h` | ✅ Implemented (needs style fix) |
| **DAFX MATLAB Code** | `DAFX-MATLAB/M_files_chap*/` | ✅ Available |
| **DaisySP Reference** | `docs/daisysp_reference.pdf` | ✅ Available |
| **Style Guide** | `docs/style_guide.pdf` | ✅ Available |
| **Gap Analysis** | `plans/DAFX_DaisySP_Gap_Analysis.md` | ✅ Available |
| **Implementation Plan** | `plans/DAFX_DaisySP_Implementation_Plan.md` | ✅ Available |

### 2.2 Required Resources

| Resource | Status | Action Needed |
|----------|--------|---------------|
| **CMake Build System** | ❌ Missing | Create CMakeLists.txt |
| **Unit Test Framework** | ❌ Missing | Set up Google Test or Catch2 |
| **Directory Structure** | ⚠️ Partial | Create filters/, dynamics/, modulation/, spatial/ |
| **Validation Scripts** | ❌ Missing | Create Python scripts for MATLAB comparison |
| **Examples** | ❌ Missing | Create hardware-agnostic examples |

---

## 3. Step-by-Step Action Plan

### 3.1 Infrastructure Setup (Week 1)

#### Task 1.1: Fix Style Guide Compliance in Tube Module
**Priority:** HIGH
**Estimated Time:** 1 hour
**Responsible:** Developer

**Actions:**
1. Update `tube.h` - Change `Process(float in)` to `Process(const float &in)`
2. Update `tube.h` - Change all setters to use `const float &` parameters
3. Update `tube.cpp` - Update function signatures to match header
4. Verify compilation with ARM GCC

**Progress Indicators:**
- [ ] tube.h updated with const reference parameters
- [ ] tube.cpp updated with const reference parameters
- [ ] Compilation succeeds with zero warnings

**Dependencies:** None

---

#### Task 1.2: Create Directory Structure
**Priority:** HIGH
**Estimated Time:** 30 minutes
**Responsible:** Developer

**Actions:**
1. Create `src/filters/` directory
2. Create `src/dynamics/` directory
3. Create `src/modulation/` directory
4. Create `src/spatial/` directory
5. Create `src/analysis/` directory
6. Create `src/utility/` directory
7. Create `tests/` directory
8. Create `examples/` directory

**Progress Indicators:**
- [ ] All directories created
- [ ] Directory structure matches implementation plan Section 4.1

**Dependencies:** None

---

#### Task 1.3: Set Up CMake Build System
**Priority:** HIGH
**Estimated Time:** 2 hours
**Responsible:** Developer

**Actions:**
1. Create root `CMakeLists.txt` with:
   - Project configuration
   - C++17 standard
   - ARM GCC toolchain support
   - Library target for dafx_daisysp
2. Create source file lists for each module category
3. Configure include directories
4. Add compiler flags for warnings as errors
5. Create test target
6. Create example targets

**Progress Indicators:**
- [ ] CMakeLists.txt created
- [ ] `cmake .. && make` succeeds
- [ ] Library builds without warnings
- [ ] Test target builds successfully

**Dependencies:** Task 1.2 (Directory Structure)

---

#### Task 1.4: Set Up Unit Test Framework
**Priority:** MEDIUM
**Estimated Time:** 3 hours
**Responsible:** Developer

**Actions:**
1. Choose test framework (Google Test recommended)
2. Add Google Test to CMakeLists.txt
3. Create `tests/test_tube.cpp` as reference
4. Create test runner script
5. Define test patterns for DSP effects:
   - Initialization test
   - Parameter range test
   - Output range test
   - Known input/output test

**Progress Indicators:**
- [ ] Test framework integrated
- [ ] test_tube.cpp created and passing
- [ ] Test runner script works
- [ ] Test patterns documented

**Dependencies:** Task 1.3 (CMake Build System)

---

### 3.2 Algorithm Implementation (Weeks 2-4)

#### Task 2.1: Implement Low Shelving Filter
**Priority:** HIGH
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** `DAFX-MATLAB/M_files_chap02/lowshelving.m`

**Actions:**
1. Read and analyze `lowshelving.m`
2. Create `src/filters/lowshelving.h` with:
   - Class definition following DaisySP patterns
   - Init(float sample_rate) method
   - Process(const float &in) method
   - SetFrequency(), SetGain(), SetQ() methods
   - Markdown documentation
3. Create `src/filters/lowshelving.cpp` with:
   - Biquad filter implementation
   - Coefficient calculation
   - Sample-by-sample processing
4. Create `tests/test_lowshelving.cpp`
5. Create validation script against MATLAB

**Progress Indicators:**
- [ ] Header file created with proper documentation
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 5%
- [ ] RAM usage < 100 bytes

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.2: Implement High Shelving Filter
**Priority:** HIGH
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** Derived from lowshelving.m

**Actions:**
1. Derive high shelving from low shelving implementation
2. Create `src/filters/highshelving.h`
3. Create `src/filters/highshelving.cpp`
4. Create `tests/test_highshelving.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 5%
- [ ] RAM usage < 100 bytes

**Dependencies:** Task 2.1 (Low Shelving)

---

#### Task 2.3: Implement Peak/Parametric EQ
**Priority:** HIGH
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** `DAFX-MATLAB/M_files_chap02/peakfilt.m`

**Actions:**
1. Read and analyze `peakfilt.m`
2. Create `src/filters/peakfilter.h`
3. Create `src/filters/peakfilter.cpp`
4. Create `tests/test_peakfilter.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 5%
- [ ] RAM usage < 100 bytes

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.4: Implement Vibrato
**Priority:** HIGH
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** `DAFX-MATLAB/M_files_chap02/vibrato.m`

**Actions:**
1. Read and analyze `vibrato.m`
2. Create `src/modulation/vibrato.h`
3. Create `src/modulation/vibrato.cpp`
4. Create `tests/test_vibrato.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 10%
- [ ] RAM usage < 50 KB

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.5: Implement Ring Modulator
**Priority:** MEDIUM
**Estimated Time:** 0.5 day
**Responsible:** Developer
**MATLAB Source:** Simple implementation

**Actions:**
1. Design ring modulator algorithm
2. Create `src/modulation/ringmod.h`
3. Create `src/modulation/ringmod.cpp`
4. Create `tests/test_ringmod.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 5%
- [ ] RAM usage < 100 bytes

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.6: Implement Stereo Panning
**Priority:** MEDIUM
**Estimated Time:** 0.5 day
**Responsible:** Developer
**MATLAB Source:** `DAFX-MATLAB/M_files_chap05/stereopan.m`

**Actions:**
1. Read and analyze `stereopan.m`
2. Create `src/spatial/stereopan.h`
3. Create `src/spatial/stereopan.cpp`
4. Create `tests/test_stereopan.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 5%
- [ ] RAM usage < 100 bytes

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.7: Implement Noise Gate
**Priority:** HIGH
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** `DAFX-MATLAB/M_files_chap04/noisegt.m`

**Actions:**
1. Read and analyze `noisegt.m`
2. Create `src/dynamics/noisegate.h`
3. Create `src/dynamics/noisegate.cpp`
4. Create `tests/test_noisegate.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 10%
- [ ] RAM usage < 1 KB

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.8: Implement CryBaby Wah
**Priority:** MEDIUM
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** DAFX Chapter 12

**Actions:**
1. Read DAFX Chapter 12 on virtual analog
2. Create `src/effects/wahwah.h`
3. Create `src/effects/wahwah.cpp`
4. Create `tests/test_wahwah.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 15%
- [ ] RAM usage < 1 KB

**Dependencies:** Task 1.4 (Test Framework)

---

#### Task 2.9: Implement Tone Stack
**Priority:** MEDIUM
**Estimated Time:** 1 day
**Responsible:** Developer
**MATLAB Source:** DAFX Chapter 12

**Actions:**
1. Read DAFX Chapter 12 on virtual analog
2. Create `src/effects/tonestack.h`
3. Create `src/effects/tonestack.cpp`
4. Create `tests/test_tonestack.cpp`
5. Create validation script

**Progress Indicators:**
- [ ] Header file created
- [ ] Implementation file created
- [ ] Unit tests pass
- [ ] MATLAB validation within ±0.5 dB
- [ ] CPU usage < 15%
- [ ] RAM usage < 1 KB

**Dependencies:** Task 1.4 (Test Framework)

---

### 3.3 Testing and Validation (Week 5)

#### Task 3.1: Create Validation Scripts
**Priority:** MEDIUM
**Estimated Time:** 1 day
**Responsible:** Developer

**Actions:**
1. Create `execution/validate_effect.py` script
2. Implement MATLAB output comparison
3. Add tolerance checking (±0.5 dB)
4. Create batch validation script
5. Document validation process

**Progress Indicators:**
- [ ] Validation script created
- [ ] Can compare C++ and MATLAB outputs
- [ ] Tolerance checking works
- [ ] Batch validation works
- [ ] Documentation complete

**Dependencies:** All algorithm implementations

---

#### Task 3.2: Run Full Test Suite
**Priority:** HIGH
**Estimated Time:** 0.5 day
**Responsible:** Developer

**Actions:**
1. Run all unit tests
2. Verify 100% pass rate
3. Run MATLAB validation for all effects
4. Document any failures
5. Fix any issues found

**Progress Indicators:**
- [ ] All unit tests pass
- [ ] All MATLAB validations pass
- [ ] No outstanding issues
- [ ] Test report generated

**Dependencies:** Task 3.1 (Validation Scripts)

---

#### Task 3.3: Performance Testing
**Priority:** MEDIUM
**Estimated Time:** 0.5 day
**Responsible:** Developer

**Actions:**
1. Measure CPU usage for each effect
2. Measure RAM usage for each effect
3. Verify all effects meet budget
4. Document performance metrics
5. Optimize if needed

**Progress Indicators:**
- [ ] CPU usage measured for all effects
- [ ] RAM usage measured for all effects
- [ ] All effects within budget
- [ ] Performance report generated

**Dependencies:** Task 3.2 (Full Test Suite)

---

### 3.4 Documentation and Examples (Week 5)

#### Task 4.1: Create Hardware-Agnostic Examples
**Priority:** MEDIUM
**Estimated Time:** 1 day
**Responsible:** Developer

**Actions:**
1. Create `examples/example_shelving.cpp`
2. Create `examples/example_vibrato.cpp`
3. Create `examples/example_noisegate.cpp`
4. Add comprehensive comments
5. Document how to adapt to hardware

**Progress Indicators:**
- [ ] 3 examples created
- [ ] All examples compile
- [ ] Examples well-documented
- [ ] Hardware adaptation guide included

**Dependencies:** All algorithm implementations

---

#### Task 4.2: Update Documentation
**Priority:** MEDIUM
**Estimated Time:** 0.5 day
**Responsible:** Developer

**Actions:**
1. Verify all modules have header documentation
2. Update CHECKPOINT.md
3. Update implementation plan
4. Create README for examples
5. Document build process

**Progress Indicators:**
- [ ] All modules documented
- [ ] CHECKPOINT.md updated
- [ ] Implementation plan updated
- [ ] README created
- [ ] Build process documented

**Dependencies:** Task 4.1 (Examples)

---

## 4. Responsibilities

| Role | Responsibilities |
|------|------------------|
| **Developer** | Implement all algorithms, write tests, create examples |
| **Reviewer** | Code review, style guide compliance verification |
| **Tester** | Run test suite, validate against MATLAB, performance testing |

---

## 5. Measurable Progress Indicators

### 5.1 Weekly Milestones

| Week | Milestone | Success Criteria |
|------|-----------|------------------|
| **Week 1** | Infrastructure Complete | CMake builds, tests run, tube style fixed |
| **Week 2** | Filters Complete | Low/High shelving, Peak EQ implemented and tested |
| **Week 3** | Modulation & Dynamics Complete | Vibrato, Ring Mod, Noise Gate implemented |
| **Week 4** | Spatial & Effects Complete | Stereo Pan, Wah, Tone Stack implemented |
| **Week 5** | Validation & Documentation | All tests pass, examples created, docs complete |

### 5.2 Phase Completion Criteria

Phase 1 is complete when:

- [ ] **10 core effects implemented** (Tube + 9 new effects)
- [ ] All effects compile without warnings
- [ ] All effects have unit tests passing (100% pass rate)
- [ ] Documentation complete for all modules (100%)
- [ ] At least 3 hardware-agnostic examples created
- [ ] All effects validated against MATLAB (±0.5 dB tolerance)
- [ ] All effects meet CPU/memory budgets
- [ ] CHECKPOINT.md updated with Phase 1 status
- [ ] No entries in dafx_bugs.md (or all resolved)

---

## 6. Risk Management

### 6.1 Identified Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| **MATLAB validation fails** | Medium | High | Use tolerance, investigate algorithm differences |
| **CPU budget exceeded** | Low | Medium | Optimize critical paths, use ARM SIMD |
| **Style guide conflicts** | Low | Low | Follow style guide strictly, use clang-format |
| **Test framework issues** | Low | Medium | Use proven framework (Google Test) |
| **Documentation gaps** | Medium | Low | Document as you implement, review at end |

### 6.2 Contingency Plans

- If MATLAB validation fails: Increase tolerance to ±1.0 dB, document differences
- If CPU budget exceeded: Profile and optimize, consider reducing effect complexity
- If timeline slips: Prioritize high-priority effects, defer low-priority ones

---

## 7. Transition to Phase 2

### 7.1 Phase 2 Preparation

Before starting Phase 2, ensure:

1. **All Phase 1 deliverables complete**
2. **Lessons learned documented**
3. **Build system stable**
4. **Test framework proven**
5. **Documentation standards established**

### 7.2 Phase 2 Preview

Phase 2 will implement:
- YIN Pitch Detection
- Robotization
- Whisperization
- SOLA Time Stretch
- FDN Reverb
- Compressor/Expander
- Universal Comb Filter

These are more complex effects requiring FFT and advanced DSP techniques.

---

## 8. Success Metrics

### 8.1 Quantitative Metrics

| Metric | Target | Current |
|--------|--------|---------|
| Effects Implemented | 10 | 1 (Tube) |
| Test Coverage | > 80% | 0% |
| Documentation Complete | 100% | 10% |
| MATLAB Validation | 100% | 0% |
| CPU Budget Compliance | 100% | N/A |
| Memory Budget Compliance | 100% | N/A |

### 8.2 Qualitative Metrics

- Code quality meets DaisySP standards
- All modules follow style guide
- Examples are clear and reusable
- Documentation is comprehensive
- Build system is robust

---

## 9. Communication Plan

### 9.1 Status Updates

- **Daily**: Update todo list
- **Weekly**: Update CHECKPOINT.md with progress
- **Phase End**: Comprehensive status report

### 9.2 Issue Tracking

- Use `dafx_bugs.md` for bug tracking
- Document all issues and resolutions
- Review bugs before phase completion

---

## 10. Appendix

### 10.1 Reference Documents

- Implementation Plan: `plans/DAFX_DaisySP_Implementation_Plan.md`
- Gap Analysis: `plans/DAFX_DaisySP_Gap_Analysis.md`
- Style Guide: `docs/style_guide.pdf`
- DaisySP Reference: `docs/daisysp_reference.pdf`

### 10.2 Quick Commands

```bash
# Build library
mkdir build && cd build
cmake ..
make

# Run tests
./tests/run_tests

# Validate against MATLAB
python execution/validate_effect.py --effect all --tolerance 0.5

# Check code style
clang-format -i src/**/*.cpp src/**/*.h
```

---

*This action plan will be updated as progress is made. Refer to CHECKPOINT.md for current status.*
