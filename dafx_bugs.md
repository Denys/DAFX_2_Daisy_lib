# DAFX-to-DaisySP Implementation Bug Log

Check this **before** implementing new features.

---

## Implementation Checklist

When porting a new effect:
```
□ Read MATLAB source file and understand algorithm
□ Check DaisySP reference for equivalent components
□ Map MATLAB matrix operations to C++ arrays
□ Handle sampling rate dependencies
□ Manage memory allocation for real-time processing
□ Account for floating-point precision differences
□ Generate C++ header (.h) and implementation (.cpp)
□ Ensure code follows DaisySP coding standards
□ Create unit test
□ Validate output matches MATLAB reference within tolerance
□ Document effect parameters and usage
```

---

## Bug Log

### BUG-001: tube.cpp/tube.h uses value parameters instead of const references

**Date Discovered:** 2026-01-10
**Severity:** Low
**Status:** Open

**Symptom:**
Function parameters in `tube.cpp` and `tube.h` use pass-by-value instead of pass-by-const-reference.

**Root Cause:**
Style guide requirement (`docs/style_guide.pdf`) specifies `const float &` for Process() and setter parameters, but initial implementation used `float` by value.

**Current Code:**
```cpp
// tube.h
float Process(float in);
inline void SetDrive(float drive) { drive_ = drive; }

// tube.cpp
float Tube::Process(float in)
```

**Required Fix:**
```cpp
// tube.h
float Process(const float &in);
inline void SetDrive(const float &drive) { drive_ = drive; }

// tube.cpp
float Tube::Process(const float &in)
```

**Files Affected:**
- `src/effects/tube.h` - All setter functions and Process()
- `src/effects/tube.cpp` - Process() implementation

**Prevention:**
- Added compliance checklist to `plans/DAFX_DaisySP_Implementation_Plan.md` Section 9.6
- Review style guide before implementing new modules

---

## Prevention Strategy

- **Use tube.cpp/tube.h as reference template** for new effects
- Validate against checklist before marking effect complete
- Test on actual Daisy hardware when possible
- Document any MATLAB-to-C++ translation quirks discovered

---

## Archive Policy

When this file exceeds 20 bugs, archive resolved bugs to `dafx_bugs_archive.md`
