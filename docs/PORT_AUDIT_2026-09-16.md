# DAFX → C++ Port Audit

**Date:** 2026-09-16
**Scope:** every module under `src/` verified against its DAFX MATLAB reference
(`DAFX-MATLAB/`) and, where no `.m` exists, against the DAFX book (Zölzer, 2nd ed.).
**Method:** line-by-line comparison plus executed numerical cross-checks. Every
`VERIFIED` finding below was reproduced by running code, not by reading alone.

## Evidence labels

| Label | Meaning |
|---|---|
| `VERIFIED` | reproduced by an executed check in this audit |
| `DERIVED` | proven algebraically from the two sources, not executed |
| `PROPOSED` | suggested fix, not implemented or tested here |
| `UNVERIFIED` | read-level observation only |

Host-side numerical agreement does **not** establish target timing, audio quality,
electrical or product readiness.

---

## 1. Headline

The port is **not** in the state the repository claims. `CHECKPOINT.md` marks 10/10
Phase-1 effects and the Phase-2 set as complete with "Validate output matches MATLAB
reference within tolerance" in the porting checklist, and `tests/` reports **151/151
passing**. Both statements are true and both are misleading: the test suite contains
essentially no numerical validation against MATLAB, so it passes while several modules
are functionally dead.

`VERIFIED` — test suite built with GCC 12 and executed: 151/151 pass.
`VERIFIED` — of 38 test files, only `test_princarg.cpp` compares against a MATLAB
reference value. The assertion population is dominated by getter/setter echo
(`EXPECT_FLOAT_EQ` on `GetX()` after `SetX()`), finiteness (`std::isfinite`),
zero-in/zero-out and `EXPECT_NO_THROW`. No test checks a transfer function, an impulse
response, or a sample-by-sample comparison against a `.m` file.

Consequence: **every defect in section 2 below is invisible to the current suite.**

---

## 2. Confirmed defects

Ordered by severity. Each row was reproduced by execution.

### 2.1 `NoiseGate` — three independent CRITICAL defects; ships as a pass-through

`src/dynamics/noisegate.cpp:48-95` vs `M_files_chap04/noisegt.m:32-59`.

**(a) Disjunction collapsed into a conjunction.** `VERIFIED`

MATLAB entry condition is an OR:
```matlab
if (h(i)<=ltrhold) | ((h(i)<utrhold) & (lthcnt>0))
```
C++ made it an AND (`noisegate.cpp:48-50`):
```cpp
if (envelope_ <= threshold_linear_ &&
    envelope_ <  threshold_upper_linear_ &&
    low_threshold_count_ > 0)
```
`low_threshold_count_` is initialised to 0 and is incremented **only inside this
branch**, so the branch can never be entered. The identical inversion is present in the
opening branch at `noisegate.cpp:75-77`.

Measured: 1 s of a 440 Hz tone at −60 dBFS with the threshold at −40 dB gives
`max |out/in| = 1.000000`. The gate never closes; `gate_gain_` stays at its initial 1.0
for the whole run.

**(b) The release branches are swapped.** `VERIFIED`

MATLAB: `if lthcnt>(rel+ht) → g=0` (fully closed), `else → g=1-(lthcnt-ht)/rel` (fading).
C++ (`noisegate.cpp:57-70`) has the two bodies exchanged. Simulating the corrected OR
condition with the shipped branch bodies, `gate_gain_` slams to 0 during the hold window
and then decreases without bound: `g[2880] = −0.0004`, `g[5000] = −0.8838`,
`g[7999] = −2.1333`, still falling. The gate would invert phase and amplify.

**(c) The hold period closes the gate instead of holding it open.** `VERIFIED`

MATLAB's `else` inside the below-threshold branch sets `g(i)=1` — the signal stays open
until the hold time expires. C++ `noisegate.cpp:69` sets `gate_gain_ = 0.0f`. Hold time
has the opposite of its documented effect.

Fixing (a) alone makes the module worse than it is today. All three must be fixed
together. `PROPOSED`

### 2.2 `WahWah` — no sweep, and the poles sit on the unit circle

`src/effects/wahwah.cpp`. No `wahwah.m` exists in this repo (`VERIFIED` by search); the
file's own design note and `plans/archive/DAFX_DaisySP_Gap_Analysis.md` cite the DAFX
Ch. 12 state-variable wah.

**(a) The modulator is a constant.** `VERIFIED`
`wahwah.cpp:41-42` computes `mod = 0.5*(1+depth*sin(2*pi*freq_/sample_rate_))` from
parameters only — there is no phase accumulator and no sample index anywhere in the
class. Measured RMS of an 800 Hz tone over four consecutive 250 ms windows:
0.19842, 0.19831, 0.19857, 0.19822. The response does not change with time. It is a
static bandpass, not a wah.

**(b) The denominator is never normalised, so the filter is undamped.** `VERIFIED`
`a2_` is hard-coded to `1.0f` (`wahwah.cpp:36`) and `a1_mod = -2*cos(wc_mod)` is used
with no `/(1+alpha)` normalisation (`wahwah.cpp:51`). The denominator is
`1 − 2cos(wc)z⁻¹ + z⁻²`, whose poles lie exactly on the unit circle for every value of
`Q`. Measured impulse response: peak |h| is 0.014450 in the first 50 ms and 0.014448 at
t ≈ 1 s — the resonator does not decay at all. `SetQ()` cannot damp it; it only scales
the numerator.

### 2.3 `ToneStack` — three broadband gain knobs, no filter

`src/effects/tonestack.cpp:25-33` is `out = in * bass_gain_ * middle_gain_ * treble_gain_`.
The class holds no filter state and no coefficients. `VERIFIED`: with bass = +12 dB,
mid = 0, treble = −12 dB, the measured gain is −0.0000 dB at 80 Hz, 500 Hz, 3 kHz and
8 kHz alike. The three controls are indistinguishable from one another and from a
single broadband trim. The in-file comment "simple shelving filter approximation" does
not describe the code.

### 2.4 `HighShelving` — the cut branch uses the low-shelf coefficient

`src/filters/highshelving.cpp:43`. No `highshelving.m` exists; the file's header says it
was derived from `lowshelving.m` by "change sign in output equation" — which is exactly
the trap the book's own comment in `lowshelving.m` (`% change to minus for HS`) sets.
The sign flip was done correctly at line 54. The **coefficient** was not.

Zölzer Table 2.4: the *boost* coefficient is shared between low and high shelf, but the
*cut* coefficient is not:

| | boost | cut |
|---|---|---|
| low shelf | `(tanθ−1)/(tanθ+1)` | `(tanθ−V0)/(tanθ+V0)` |
| high shelf | `(tanθ−1)/(tanθ+1)` | `(V0·tanθ−1)/(V0·tanθ+1)` |

`highshelving.cpp:43` uses the low-shelf cut formula.

`VERIFIED` by two independent checks:

1. *Design-criterion check.* The Zölzer cut is defined to be the exact inverse of the
   boost at the same `fc`. Method validated on the low shelf, where `lowshelving.m` is
   authoritative: the MATLAB form gives 0.0000 dB inverse error and the other form gives
   9.007 dB. Applied to the high shelf: the shipped form gives **10.175 dB** error, the
   book form gives **0.0000 dB**.
2. *Sample-level check.* C++ vs a verbatim transcription of the reference, 64 samples,
   fs = 48 kHz:

| case | max rel. error | |
|---|---|---|
| fc=4k G=+12 | 1.18e-07 | match (float32 rounding) |
| fc=8k G=+6 | 1.03e-07 | match |
| fc=4k G=−12 | **1.66e+00** | mismatch |
| fc=8k G=−6 | **4.01e-01** | mismatch |
| fc=2k G=−20 | **6.01e+00** | mismatch |

Boost is correct; every cut is wrong. Audible effect: at G = −12 dB, fc = 4 kHz the
shipped filter attenuates by 0.25 dB at fc and 4.9 dB at 16 kHz, where the reference
gives 9.3 dB and 11.9 dB — the shelf corner is displaced upward by roughly two octaves,
so the control barely works.

Fix: `c_ = (v0 * tan_half_wc - 1.0f) / (v0 * tan_half_wc + 1.0f);` in the cut branch. `PROPOSED`

### 2.5 `UniversalComb::SetAllpass` — the preset is an identity

`src/effects/universal_comb.h:182-187` sets `FB=g, FF=−g, BL=1`. `DERIVED`: with
`Xh = X/(1−g z⁻ᴹ)` and `Y = Xh(1 − g z⁻ᴹ)`, `H(z) ≡ 1`. The DAFX allpass row is
`BL=a, FB=−a, FF=1`, giving `H(z) = (a + z⁻ᴹ)/(1 + a z⁻ᴹ)`.

`VERIFIED` by impulse response, M = 10, g = 0.5:
```
shipped : 1.000 0 0 0 0 0 0 0 0 0 0.000 0 ...      (impulse in, impulse out)
book    : 0.500 0 0 0 0 0 0 0 0 0 0.750 0 ... -0.375 at n=20
```
Fix: `feedback_ = -g; feedforward_ = 1.0f; blend_ = g;` `PROPOSED`

The `Process()` core itself is bit-exact against `unicomb.m` (max abs diff 0.0) — only
the preset is wrong.

### 2.6 `CircularBuffer::Read(0)` returns the oldest sample, not the newest

`src/utility/circularbuffer.h:72-78`. `VERIFIED` with an 8-deep buffer holding 1…8
(8 most recent):

```
Read(0) = 1.0   <- documented as "zero delay"; returns the oldest sample (delay = size)
Read(1) = 8.0   <- actually the zero-delay tap
Read(2) = 7.0 ...
ReadInterpolated(0.5) = 4.500   <- blends the oldest sample with the newest
```
The valid domain is `[1, size−1]`, not `[0, size−1]`. `ReadInterpolated` clamps its lower
bound to `0.0f` (`circularbuffer.h:86-87`), so it admits the broken region; any modulated
delay swept toward zero (flanger, chorus) crosses it and produces full-amplitude garbage
rather than a bypass. `PROPOSED`: clamp the lower bound to `1.0f` and document the domain.

### 2.7 `SimpleHRIR` — the ITD carries no left/right information

`src/utility/simple_hrir.h:86-100` vs `M_files_chap05/simpleHRIR.m:16,28-32`.

The MATLAB reassigns the angle once at the top (`theta = theta + 90;`), so *every*
later use — the `abs(theta) < 90` hemisphere test and the `cos(theta*pi/180)` formula
alike — operates on the shifted angle. The C++ computes `theta_shifted` (line 72) and
uses it correctly for the ILD coefficient `alfa`, then reverts to the **unshifted**
`theta` for both the branch test and the delay formula.

`VERIFIED` by counting the leading zeros in the buffer the shipped code actually
generates, fs = 48 kHz:

| θ | `simpleHRIR.m` | `src/` |
|---:|---:|---:|
| −90° | 0 | 11 |
| −30° | 6 | 2 |
| −5° | 10 | 0 |
| 0° | 11 | 0 |
| +5° | 13 | 0 |
| +30° | 18 | 2 |
| +90° | 30 | 11 |

Two separate failures. The values are wrong at every angle, and — because the unshifted
`cos θ` is an even function — the C++ ITD is **symmetric about 0°**: ±5° both give 0,
±30° both give 2, ±90° both give 11. The interaural time difference is the dominant
lateralisation cue below ~1.5 kHz, and as shipped it cannot distinguish left from right
at all. The reference is correctly monotonic, 0 → 30 samples across the arc.

The surrounding code is right: the `alfa` formula, the `b0/b1/a1` algebra and the
delay-and-truncate placement all match the reference. Only the `gdelay` value is wrong.
Fix: use `theta_shifted` in the branch test and in both formula arms. `PROPOSED`

### 2.8 `StereoPan` — `SetSpeakerAngle()` is a dead API

`src/spatial/stereopan.cpp:16-25` vs `M_files_chap05/stereopan.m:19-23`.

The reference uses the **tangent law**, in which the loudspeaker half-angle `lsbase`
enters the gain computation:
```matlab
g(2)=1;  g(1) = -(tan(theta)-tan(lsbase)) / (tan(theta)+tan(lsbase)+eps);
g = g/sqrt(sum(g.^2));
```
The C++ implements the sin/cos equal-power law and never reads `speaker_angle_` at all.

Equal-power panning is a defensible choice in its own right, and the constant-power
property does hold. The defect is that `StereoPan::SetSpeakerAngle()` is a documented,
public, callable setter that has **no effect on the output** — the member is stored and
never used. Either wire it into a tangent-law implementation or remove it; leaving a
no-op control on the API is the worst of the three options. The header's claim of a
"tangent law" should be corrected either way. `VERIFIED` by reading both sources.

### 2.9 `CrosstalkCanceller` — the `fftshift` is omitted

`src/spatial/crosstalk_canceller.h:365-376` vs `M_files_chap05/crosstalkcanceler.m:33-34`.

The reference takes the inverse FFT of the regularised inverse filter and then
**re-centres it**: `H_n = fftshift(real(ifft(H_f)))`. The C++ uses the inverse-FFT
output directly. `FFTHandler::FFTShift()` exists at `fft_handler.h:202-209` and is never
called anywhere in this file.

The regularised inverse `inv(CᴴC+βI)Cᴴ` is acausal; without the shift its dominant taps
wrap to the top of the buffer. Simulation of the reference computation at θ = ±5°,
FFT_SIZE = 512 puts ~80 % of the filter energy in the second half of the buffer with the
peak tap near index 502. The overlap-add at lines 371-376 sends everything above
`HRIR_LENGTH` into the *next* block, so the dominant cancellation tap is displaced by a
full block (≈5.3 ms at 48 kHz). `UNVERIFIED` against the shipped C++ end to end — the
mechanism and the omission are certain, the exact audible degradation is not measured
here.

Note this module also consumes `SimpleHRIR`, so defect 2.7 compounds it: the canceller's
default operating point is ±half the speaker angle, exactly the small-angle range where
the ITD collapses to zero.

The 2×2 complex matrix algebra and the ipsi/contra channel routing were both checked
term by term and are **correct** — no channel swap.

### 2.10 `CompressorExpander::SetRmsTime()` is silently overwritten

`src/dynamics/compressor_expander.h:205-212`. `RecalculateCoefficients()` unconditionally
executes `tav_ = 1.0f/(sample_rate_*0.01f);` and is called from `SetAttackTime()`,
`SetReleaseTime()` and `Init()`. Any caller who sets the RMS window and *then* touches
attack or release silently loses it, reverting to a hard-coded ~10 ms. Order-dependent
and invisible. `UNVERIFIED` by execution; `DERIVED` from the call graph.

### 2.11 Undocumented real-time deviations in `Tube`

`tube.m` normalises three times over the whole signal (input by `max(abs(x))`, the
waveshaper output by `max(abs(z))`, and the final mix again). None of this is realisable
per-sample, and the C++ correctly drops all three — but nowhere says so. The consequence
is real: the book's curve assumes a ±1-normalised input, so with the normalisation gone
the operating point relative to `bias_` and `dist_` now tracks raw input amplitude.
Callers get a different distortion character at different input levels, with no guidance
in the header. This is a legitimate adaptation recorded as a defect only because it is
undocumented. `DERIVED`.

Separately, `SetDistortion()` has no guard: `dist_ = 0` makes `1/dist_` and every
`1-exp(...)` term divide by zero, latching NaN into the HP/LP filter state.

---

## 3. Verified-correct modules

These were compared line by line and, where noted, numerically:

- **`LowShelving`** — algebraically identical to `lowshelving.m`. `VERIFIED` sample-level
  agreement with a verbatim transcription across 5 parameter sets, max rel. error 2.7e-07
  (float32 rounding only).
- **`PeakFilter`** — identical to `peakfilt.m`, including the `d = −cos(πWc)` term
  correctly *not* halved and the two-sample allpass state ordering. `VERIFIED` agreement
  to float32 precision; the largest residual (2.7e-06 rel. at fc=1 kHz, fb=200 Hz) is
  float32 accumulation in a high-Q resonator, reproduced by a float32-exact reference.
- **`UniversalComb::Process`** — bit-exact against `unicomb.m` (max abs diff 0.0).
- **`FDNReverb::Process`** — bit-exact against `delaynetwork.m` over 1200 samples
  (max abs diff 0.0), including all sixteen feedback-matrix entries and signs.
- **`CompressorExpander`** core — attack/release selection direction and the lookahead
  topology both match `compexp.m`.
- **`Tube`** waveshaper — both removable singularities (`x==Q` and the `Q==0`, `x==0`
  case) are handled; the HP and LP difference equations match `tube.m` exactly.
- **`RingMod`** — true multiplicative modulation with a correctly wrapped phase
  accumulator.
- **`EnvelopeFollower`** — canonical one-pole; consistent with the complementary form
  used inside `CompressorExpander`.

---

## 4. Structural findings

### 4.1 The repository does not build with GCC `VERIFIED`

`CMakeLists.txt` sets `-Wall -Wextra -Wpedantic -Werror`. A clean GCC 12 configure and
build fails before reaching the tests:

- `examples/example_guitar_amp.cpp:37,50` — calls `SetMid`/`GetMid`; the class has
  `SetMiddle`/`GetMiddle`.
- `examples/example_modulation.cpp:31` — `RingMod` not declared in scope.
- `examples/example_parametric_eq.cpp:34,46` — calls `SetQ`/`GetQ`; `PeakFilter` has no
  such member.
- `tests/test_tube.cpp:67,87,88,111,112,147` — `std::isfinite` / `std::fabs` used without
  `#include <cmath>`; MSVC supplied it transitively, GCC does not.
- `src/spectral/robotization.h:47,166,170` and `src/spectral/whisperization.h:53,180,185`
  — member initialiser order does not match declaration order (`-Werror=reorder`).
- `tests/test_spectral_filter.cpp:74` — `-Werror=unused-but-set-variable`.

The checked-in `build/` directory contains MSVC `.vcxproj` artefacts, so the project has
only ever been built on one toolchain. For a library whose stated target is an ARM
Cortex-M7 cross-build, this is a portability gap, not a cosmetic one.

### 4.2 Source attribution is incomplete

Eleven headers under `src/` declare no DAFX reference. Three of those
(`pedal_harness/*.hpp`) are harness code and legitimately out of DAFX scope. The rest —
`analysis/yin.h`, `effects/tonestack.h`, `effects/wahwah.h`, `modulation/ringmod.h`,
`utility/{circularbuffer,envelopefollower,fft_handler,windows,xcorr}.h` — carry no
citation, so a reviewer cannot tell what they are supposed to match. `highshelving.h`
cites `lowshelving.m`, which is precisely the mis-attribution that produced defect 2.4.

### 4.3 A note on the reference itself

`M_files_chap04/lpiircomb.m:21` assigns `xhhold` where every other line uses `xhold` — a
typo in the published book code, so that variable stays 0 for the whole run. The C++
implements the *intended* equation and therefore differs from the verbatim script. This
should be recorded in `dafx_bugs.md` before anyone writes a bit-exactness test against
the script. `UNVERIFIED` as to the book's printed text; `DERIVED` from the file.

---

## 5. Recommended order of work

1. `NoiseGate` — all three defects together (2.1). The module is non-functional.
2. `WahWah` — restore a real LFO accumulator and normalise the denominator (2.2).
3. `ToneStack` — implement actual filters or rename the class and withdraw the claim (2.3).
4. `HighShelving` — one-line coefficient fix (2.4).
5. `UniversalComb::SetAllpass` — one-line preset fix (2.5).
6. `CircularBuffer` — clamp and document the valid delay domain (2.6).
7. Fix the GCC build (4.1) so the suite can run in CI on the toolchain family that
   actually matters.
8. **Replace the smoke tests with reference comparisons.** Every defect above was found
   by comparing against MATLAB and none by the existing 151 tests. Until the suite
   compares transfer functions or sample streams against the `.m` files, a green run
   means nothing about correctness.

## 6. Checks not run

- Target-hardware build, timing, CPU load and audio behaviour: `NOT_RUN`.
- `src/spectral/*`, `src/analysis/yin.h`, `src/effects/sola_time_stretch.h`,
  `src/spatial/*`, `src/utility/{fft_handler,windows,xcorr,simple_hrir}.h`,
  `src/modulation/vibrato.*`: see sections added below.
