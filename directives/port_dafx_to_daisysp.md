# Port DAFX MATLAB Effects to DaisySP C++

## Goal
Port selected digital audio effects from the DAFX MATLAB codebase to C++ code compatible with the Electro-Smith DaisySP library for use on Daisy hardware platforms.

## Inputs
- MATLAB source files from `DAFX-MATLAB/M_files_chap*/` directories
- DaisySP API documentation (`docs/daisysp_reference.pdf`)
- DAFX book reference (`docs/DAFX- Digital Audio Effects - 2ed 2011 - Udo Zölzer.pdf`)

## Tools/Scripts Used
- `execution/parse_matlab.py` - Parse MATLAB code and extract algorithm structure
- `execution/generate_daisysp_code.py` - Generate C++ code using DaisySP components
- `execution/test_effect.py` - Test generated code for correctness

## Outputs
- C++ header and implementation files in `src/effects/` directory
- Unit tests in `tests/`
- Documentation updates

## Process Steps
1. Select target effect from MATLAB files
2. Analyze MATLAB algorithm and dependencies
3. Map MATLAB functions to DaisySP equivalents
4. Generate C++ code
5. Test and validate against MATLAB reference
6. Document and integrate

## Edge Cases
- Handle MATLAB matrix operations vs C++ arrays
- Convert sampling rate dependencies
- Manage memory allocation for real-time processing
- Handle floating-point precision differences

## Success Criteria
- Generated C++ code compiles without errors
- Audio output matches MATLAB reference within acceptable tolerance
- Code follows DaisySP coding standards
- Real-time performance suitable for Daisy hardware