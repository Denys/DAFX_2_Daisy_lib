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

Four separate reasons that number means nothing:

**(a) Nine of 29 test files are excluded from the build.** `tests/CMakeLists.txt`
comments six of them out and never lists the other three:

| Excluded test | How | Module's audit verdict |
|---|---|---|
| `test_tonestack.cpp` | commented, `# TODO: Fix SetMid/GetMid API mismatch` | 2.3 — no filter at all |
| `test_sola.cpp` | commented, no reason given | 2.16 — does not implement SOLA |
| `test_yin.cpp` | commented, `# TODO: Add M_PI include` | 2.17 — streaming mode broken |
| `test_fdn_reverb.cpp` | commented, no reason given | 4.3 — core bit-exact, config is not |
| `test_xcorr.cpp` | commented, `# TODO: Add M_PI include` | 2.20 — biased toward small lags |
| `test_envelopefollower.cpp` | commented, `# TODO: Add M_PI include` | correct |
| `test_universal_comb.cpp` | never listed | 2.5 — `SetAllpass` is an identity |
| `test_compressor_expander.cpp` | never listed | 2.10 — attack/release inverted vs reference |
| `test_lp_iir_comb.cpp` | never listed | 2.19 — loop filter has no pole |

The exclusions correlate with the defects. Four of the nine untested modules are among
the worst in this audit. `test_sola.cpp:95` asserts `EXPECT_GT(slow_len, unity_len)` at
stretch 0.5, which the shipped code contradicts (3072 vs 6144) — that assertion would be
red if the file were compiled, which is presumably why it is not.

**(b) Of the 20 files that do build, only `test_princarg.cpp` compares against a MATLAB
reference value.** The assertion population is dominated by getter/setter echo
(`EXPECT_FLOAT_EQ` on `GetX()` after `SetX()`), finiteness (`std::isfinite`),
zero-in/zero-out and `EXPECT_NO_THROW`. No test checks a transfer function, an impulse
response, or a sample stream against a `.m` file.

**(c) `SpectralFilter` is never instantiated at its default size**, which is why 2.12
(a hard compile error at `SpectralFilter<1280>`) survived.

**(d) CI never runs the suite, and the one job that would is allowed to fail.**
`.github/workflows/build.yml` builds only the `dafx_daisysp` and `pedal_harness_tests`
targets on Linux, Windows and macOS — `unit_tests` is not among them, and neither is
`examples/`. The sole job that configures the full aggregate and runs `ctest` over it,
`legacy-regression`, is marked **`continue-on-error: true`** (line 90), so it can fail
without turning the check red. The workflow header states the intent plainly: *"The
broader legacy aggregate suite remains tracked separately until its API debt is
repaired."*

Because the library target is built from `src/*.cpp` only, and the defective spectral
modules are header-only templates instantiated exclusively by tests, the green Linux
build does not compile them at all. That is why 4.1 below — three `examples/` files
calling methods that do not exist — has been able to persist.

Consequence: **every defect in section 2 below is invisible to the current suite**, a
green run carries no information about correctness, and a red one would not turn CI red
either.

---

## 2. Confirmed defects

Ordered by severity. Most findings here were reproduced by execution and say so; the
exceptions are labelled and are not measurements. Specifically: in 2.9
(`CrosstalkCanceller`) the uninitialised HRIR tail is `VERIFIED`, but the degradation from
the omitted `fftshift` is `DERIVED` from the reference computation and not measured against
the shipped C++ end to end; in 2.10 (`CompressorExpander`) the coefficient mismatch is
`VERIFIED` by computation while the `tav_` clobbering is `DERIVED` from the call graph;
2.11 (`Tube` normalisation) and 2.14 (frame-burst CPU) are `DERIVED` from the code. Read
the label on each claim, not this heading.

### 2.1 `NoiseGate` — three CRITICAL logic defects plus a detector mismatch; ships as a pass-through

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

**(d) The envelope detector is one pole where the reference has two.** `DERIVED`

`noisegt.m:29` is `filter([(1-a)^2], [1 -2a a^2], abs(x))` — the denominator factorises as
`(1 - a·z⁻¹)²`, a **cascaded two-pole** detector with a repeated real pole, i.e. critically
damped. `noisegate.cpp:45` is a single pole: `envelope_ = alpha_*envelope_ + (1-alpha_)*abs_in`.
The in-file comment calls it "first-order lowpass", so the simplification is disclosed —
but it changes the envelope's attack curvature, and therefore *when* the threshold is
crossed and when the hold window starts, for any transient or level-varying input.

Fixing (a) alone makes the module worse than it is today; fixing (a)–(c) makes it a
working gate but not this reference's gate. An earlier revision said "all three must be
fixed together" and stopped there, implying that was the complete repair. It is not — the
detector topology is a fourth difference and belongs in the plan. Found by the Codex
review on PR #9. `PROPOSED`

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
peak tap near index 502.

**The overlap-add is also broken, and this changes the diagnosis above.** `DERIVED` by
tracing the two functions across two blocks.

`Process()` emits `left_out_buffer_[i] + left_overlap_[i]` and then clears **both**
(`crosstalk_canceller.h:125-132`). Over one block of `HRIR_LENGTH` calls it therefore
zeroes every element of `left_overlap_` before `ProcessBlock()` next runs. So when
`ProcessBlock()` executes `left_out_buffer_[i] = left_time[i] + left_overlap_[i]`
(`crosstalk_canceller.h:371`), the addend is always zero:

```
block k   ProcessBlock: out_buffer[i] = time_k[i]          overlap[i] = time_k[i+N]
block k+1 Process:      output[i] = out_buffer[i] + overlap[i]
                                  = time_k[i] + time_k[i+N]     <- both halves, same position
                        then clears overlap[i]
block k+1 ProcessBlock: out_buffer[i] = time_{k+1}[i] + 0        <- tail already consumed
```

The second half of each IFFT is summed onto the **first half of the same transform**, at
the same output position — not carried into the next block. That is circular convolution:
the fold the `2·HRIR_LENGTH` zero-padding exists to prevent. It corrupts every nonzero
inverse filter on its own.

An earlier revision of this document said the dominant tap was "displaced by a full block
(≈5.3 ms)". That was wrong, and the error was in reading the scheduling rather than the
maths: the tail is folded, not delayed, so a timing displacement is not the failure mode
and the `fftshift` diagnosis cannot be stated in those terms. Found by the Codex review on
PR #9. The missing `fftshift` remains a real omission, but which taps land where cannot be
assessed until the overlap-add is fixed. `UNVERIFIED` end to end against the shipped C++.

**The inverse filters are also built from uninitialised memory.** `VERIFIED`, and it bites
before the missing `fftshift` matters. `ComputeInverseFilters()` declares four
`float hrir_ll[HRIR_LENGTH]` stack arrays (`crosstalk_canceller.h:192-195`) with
`HRIR_LENGTH` defaulting to 256, but `SimpleHRIR::Generate()` zeroes only
`length_ * sizeof(float)` (`simple_hrir.h:62`), and `length_` is `0.003·fs` — 144 samples
at 48 kHz, 132 at 44.1 kHz. `PadAndFFT` then copies the full `HRIR_LENGTH`
(`crosstalk_canceller.h:214`).

Filling the buffer with a sentinel before the call and counting what survives:

```
GetLength() = 144   buffer passed by CrosstalkCanceller<256> = 256
slots still holding the sentinel after Generate(): 112, first at index 144
tail the FFT then consumes: -999.0 -999.0 -999.0 -999.0 -999.0 -999.0
```

112 indeterminate floats enter the FFT of each of the four HRIR paths, so the inverse
filters — and therefore the cancellation — are nondeterministic run to run. Fixing the
`fftshift` alone would leave this in place. `PROPOSED`: zero the whole caller-supplied
buffer, or have `Generate()` take and honour a capacity. `Generate()`'s own doc comment
only promises `GetLength()` samples, so the caller is the party at fault — but the
mismatch is invisible at the call site.

Note this module also consumes `SimpleHRIR`, so defect 2.7 compounds it as well: the
canceller's default operating point is ±half the speaker angle, exactly the small-angle
range where the ITD collapses to zero.

The 2×2 complex matrix algebra and the ipsi/contra channel routing were both checked
term by term and are **correct** — no channel swap.

### 2.10 `CompressorExpander` — MATLAB's coefficients reused as if they were times

`src/dynamics/compressor_expander.h:52-53, 205-212` vs `M_files_chap04/compexp.m:13-15`.

**(a) The defaults do not match the reference, and invert attack against release.**

`compexp.m` declares `tav = 0.01`, `at = 0.03`, `rt = 0.003` as **raw one-pole
coefficients** applied per sample. The C++ constructor takes the same three literals —
`attack_time_(0.03f)`, `release_time_(0.003f)`, `tav_(0.01f)` — and `RecalculateCoefficients()`
reinterprets the first two as **seconds**, converting with `1-exp(-1/(t·fs))`. `VERIFIED`
by computing both at 48 kHz:

| | `compexp.m` | C++ | ratio |
|---|---:|---:|---:|
| attack coeff | 0.030000 | 0.000694 | 43.2× |
| release coeff | 0.003000 | 0.006920 | 0.4× |
| `tav` | 0.010000 | 0.002083 | 4.8× |

As equivalent time constants: the reference attacks in **0.68 ms** and releases in
**6.93 ms**; the port attacks in **30 ms** and releases in **3 ms**. The port's attack is
ten times *slower* than its release — the reference's relationship inverted, not merely
rescaled. A compressor with that shape lets transients through and then pumps.

Interpreting the numbers as times is the right instinct, since raw coefficients are
sample-rate dependent and do not port. Carrying the reference's literal values across the
unit change is what produces the defect: the constants look like they match `compexp.m`
and do not. `PROPOSED`: pick time constants that reproduce the reference's coefficients at
44.1 kHz (≈0.68 ms and ≈6.9 ms) and say in the header that they were converted.

**(b) `SetRmsTime()` is silently overwritten.**

`RecalculateCoefficients()` unconditionally executes `tav_ = 1.0f/(sample_rate_*0.01f);`
and is called from `SetAttackTime()`, `SetReleaseTime()` and `Init()`. Any caller who sets
the RMS window and *then* touches attack or release silently loses it. The same line also
makes the constructor's `tav_(0.01f)` dead — it is the one default that did match the
reference, and it never survives `Init()`. `DERIVED` from the call graph.

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

### 2.12 `SpectralFilter<1280>` does not compile

`src/spectral/spectral_filter.h:46-48`. `FFT_SIZE = 2 * FIR_LENGTH`, and the default
template argument is `FIR_LENGTH = 1280` — taken straight from `VX_filter.m:17-18`
(`s_FIR = 1280; s_win = 2*s_FIR`). That makes `FFT_SIZE = 2560`, which is not a power of
two, so `fft_handler.h:80` fires:

```
error: static assertion failed: FFT size must be power of 2
```

`VERIFIED`: instantiating `SpectralFilter<1280>` — the default, and the size the header's
own usage example passes — is a hard compile error. The module is only instantiable at
power-of-two `FIR_LENGTH`, i.e. never at the DAFX reference size. Nothing in the test
suite instantiates it at the default, which is why this survived.

### 2.13 Spectral effects: the DSP cores are right, the buffering is not

This is the clearest pattern in the whole audit. The low-level maths is sound and the
wrappers around it are not.

**Correct and measured:** the FFT (`fft_handler.h`) agrees with a double-precision
reference DFT to float epsilon at N = 8/16/1024, with the `1/N` inverse scaling applied
exactly once; `princarg.h` matches `princarg.m` on the interior and uses `floor` (not
`fmod`, so the negative-input sign trap is avoided), though it differs by 2π at odd
multiples of π — see 2.18; `windows.h` implements the **periodic** Hanning that
`hanningz.m` requires — measured COLA of `w²` is exactly 1.5 at hop N/4 and 3.0 at
hop N/8.

**Broken above that layer:**

- `Robotization` and `Whisperization` share two identical buffering faults.
  `input_pos_` is reset to 0 every hop, so only `input_buffer_[0 .. hop-1]` is ever
  written and the rest of the analysis grain stays permanently zero — 75 % zeros at
  hop N/4, 87.5 % at the default hop N/8. Separately, the overlap buffer is written
  modulo `2N` but read modulo `N`, so every grain tail above index N is discarded unread.
  Proof that the spectral core itself is exact: at hop = N, where neither fault can fire,
  `Robotization` cross-correlates **1.0000** against a faithful reimplementation of
  `VX_robot.m`; at hop N/4 it falls to 0.6652. `Whisperization`'s output level *rises* as
  overlap increases (0.0096 → 0.1056 rms from hop N/8 to N) — the inverse of correct
  overlap-add behaviour.
- `SpectralFilter`'s overlap-add is a no-op: `Process()` moves the tail out of
  `overlap_buffer_` and zeroes it *before* `ProcessBlock()` runs, and `ProcessBlock` then
  overwrites the destination rather than adding to it. Measured against exact time-domain
  convolution with the class's own FIR, the per-sample difference spikes 35× at every
  index where `n mod FIR_LENGTH == 0` — a click at the block rate. Its FIR also uses
  `exp(+damping·n)` where `VX_filter.m:32` has `alpha = -0.002`, so the impulse response
  **grows** (|h| 0 → 7.15 over 1024 taps) instead of decaying.
- `PhaseVocoder` transcribes the phase propagation correctly — ω uses the analysis hop,
  the bin index has no off-by-one, and the ratio multiplies the whole `delta_phi` — but
  applies the **reciprocal** stretch ratio at `phase_vocoder.h:256`
  (`tstretch = 1.0f / pitch_ratio_` where `VX_pitch_pv.m:22` has `n2/n1`). Measured pitch
  error up to **105 cents**; changing that one expression makes every upward ratio exact
  to ≤ 0.05 Hz. Two further faults: each grain's first `HOP_SIZE` samples are emitted
  twice (gain sweeps 1.5 → 1.74 with period H, i.e. ±0.7 dB AM at `fs/H`), and
  `grain_length_` is clamped to `FFT_SIZE`, which makes the resampler an identity map for
  every ratio < 1 — the entire documented 0.5–1.0 downward range does nothing.
- None of the four normalises the overlap-add gain. The MATLAB scripts peak-normalise
  offline, which has no real-time equivalent. The right factor is per implementation, not
  one number for the family: for the windowed, overlapping effects (`Robotization`,
  `Whisperization`) the squared-Hann overlap sum is `(3/8)·(N/H)` — measured exactly 1.5 at
  hop N/4 and 3.0 at hop N/8. It does **not** apply to `SpectralFilter`, which uses
  non-overlapping `FIR_LENGTH` blocks with no analysis or synthesis window (its Hanning
  call at `spectral_filter.h:122` shapes the FIR taps at design time, not the input
  frames); once its overlap-add is fixed, standard FFT convolution already has unity gain
  and applying the factor would make it wrong. `PhaseVocoder`'s grain length is
  ratio-dependent, so its factor varies with the pitch ratio. An earlier revision
  prescribed the single factor for all four; found by the Codex review on PR #9.

Two corrections to the brief this audit issued to itself, caught against the files:
`VX_robot.m:35` has **one** fftshift (output only), not two, and the C++ matches it;
`VX_filter.m` has no dB-domain spectral-envelope step — it is plain FFT overlap-add FIR
convolution.

### 2.14 Frame-burst CPU makes the spectral family non-real-time as written

`UNVERIFIED` on target — nothing here was profiled on a Daisy, and the verdict is a
structural one about *when* the work happens, not a measured budget.

What all four share, `DERIVED` from the code: the entire frame is processed inside a single
`Process()` call — the one where the hop counter wraps — rather than amortised across the
hop. The per-frame work differs sharply between them, and an earlier revision of this
document attributed the phase vocoder's transcendental load to all four. Counted from the
source:

| module | forward+inverse FFT | `sqrt` | `atan2` | `cos`/`sin` |
|---|:-:|:-:|:-:|:-:|
| `Robotization` | yes | — | — | — |
| `Whisperization` | yes | — | — | per bin |
| `SpectralFilter` | yes | — | — | — (its two `sin` calls are in `SetBandpass`, a control-path function) |
| `PhaseVocoder` | yes | per bin | per bin | per bin |

So the burst is worst for `PhaseVocoder` by a wide margin and is a pair of transforms for
`Robotization` and `SpectralFilter`. At N = 2048 even a bare FFT pair in one 48-sample
callback is a problem on a Cortex-M7, which is why the structural point stands for the
family — but the cost is not uniform and the earlier table overstated three of the four.
Profile before quoting a figure. `PhaseVocoder` also holds roughly 150 KB per instance at
N = 2048 and puts 8–16 KB frames on the callback stack.

### 2.15 `Vibrato` — heap overflow reachable from the public API

`src/modulation/vibrato.cpp:22-39`. `RecalculateCoefficients()` recomputes
`delay_line_size_` on every `SetWidth()`/`SetFrequency()` call, but `delay_line_` is
allocated once, in `Init()`. `Init(48000)` followed by `SetWidth(0.05f)` — a value inside
the range the header documents at `vibrato.h:10` (0.0001–0.1 s) — grows the logical size
from 722 to 7202 floats against a 722-float allocation.

`VERIFIED` under AddressSanitizer:
```
ERROR: AddressSanitizer: heap-buffer-overflow
READ of size 4 ... in daisysp::Vibrato::Process at vibrato.cpp:61
```
Line 43 writes out of bounds on the same path. On a Cortex-M7 with no MMU this is silent
memory corruption rather than a crash. `tests/test_vibrato.cpp:28` already calls
`SetWidth(0.01f)` after `Init` and escapes only because it never calls `Process()`
afterwards.

Two further defects in the same file:

- **The LFO runs on a wrapping pointer.** `vibrato.cpp:46` computes the modulator from
  `write_ptr_`, which wraps modulo `delay_line_size_`. At the defaults (5 Hz, 5 ms,
  48 kHz) the buffer is 722 samples, so the "5 Hz sine" restarts every 722 samples —
  **66.5 Hz** — and its phase only ever spans 0→0.472 rad, so the modulator ramps
  0 → 0.455 and snaps back. It is a sawtooth at 45 % depth, not a sine. The fix is a
  free-running phase accumulator.
- **The interpolation reads the wrong tap pair.** `vibrato.m:32` blends delay `i`
  (weight `frac`) with delay `i−1` (weight `1−frac`); `vibrato.cpp:56-61` blends delay
  `i` with delay `i+1` — the older neighbour. The effective delay is long by `2−2·frac`
  samples, a 2-sample sawtooth riding on the delay trajectory. Correcting both the
  pointers and the phase reproduces the MATLAB delay trajectory to ±2.8e-14.

`vibrato.h:34` also leaves `delay_line_` uninitialised in the constructor while the
destructor tests it against `nullptr` and calls `delete[]`, and there is no rule of three.

### 2.16 `SOLATimeStretch` — the synchronisation step is computed and discarded

`src/effects/sola_time_stretch.h` vs `M_files_chap06/TimeScaleSOLA.m`.

SOLA is *synchronised* overlap-add: a cross-correlation search picks the lag at which the
incoming grain best aligns with the output tail. This port computes `optimal_offset` and
then never applies it. At `sola_time_stretch.h:192` it feeds only a `fade_len` expression
that is immediately clamped to `synthesis_hop_`; since `offset ≤ 127` and
`grain_size_ ≥ 1024`, that clamp always binds, and line 211 indexes `current_grain_[i]`
rather than `[offset+i]`. The result is plain unsynchronised overlap-add.

Proof: forcing `offset` to 0, 1, 63 or 127 produces **bit-identical** output at stretch
0.5, 1.0 and 1.5 (`max|diff| = 0.000e+00`). On a 500 Hz sine at α = 1.5 the reference
gives −30.2 dB sidebands and −0.5 dB level loss; this code gives **+79.9 dB sidebands**
and −4.5 dB loss.

The streaming path is separately non-functional: `OutputAvailable()` goes false while
`grain_ready_` is still set, and only `GetOutput()` clears the flag. 200 000 input samples
at α = 1.5 yield **1 grain and 384 output samples** where ~300 000 are expected. Forcing
the flag clear collapses the analysis hop to 1 sample and produces 75 816 000 samples —
379× the input. `sola_time_stretch.h:321` also resets the output tail to the *start* of
the previous grain, so a second grain would splice unrelated audio.

The crossfade is also 4.7–14× shorter than the reference (256 or 384 samples against a
measured 1560–1818), the correlation searches only non-negative lags where `xcorr`
searches ±(L−1), and the header's "0.5 = half speed / slower playback" documentation is
inverted with respect to the code's own `Ss = Sa·α`.

This needs a rewrite rather than a patch.

### 2.17 `YIN` — block mode correct, streaming mode analyses a rotated frame

`src/analysis/yin.h`. The algorithm itself is right, and deliberately closer to canonical
YIN than `yinDAFX.m` is: `d'(0)` is set to 1 explicitly, the running sum starts at τ=1,
the search takes the first dip below threshold and descends to the *local* minimum, and
the parabolic interpolation sign and scale are correct. Block mode on a 220 Hz frame
returns **220.0008 Hz at confidence 0.99997**, and the interpolation improves the estimate
600× over the integer-τ result.

The streaming path is broken: `ProcessSample` writes into a ring buffer modulo
`YinLen+MaxTau` (`yin.h:144`) but `ComputeDifferenceFunction` reads linearly from index 0
(`yin.h:315`) with no rotation by `input_pos_`. The analysis frame is a rotated copy of
the signal with a splice whose position moves every hop. On a clean, noise-free 220 Hz
tone only **114 of 187 frames** come back voiced, with five frames reporting `f0 = 0`.
Block mode on the same signal is exact. `Process()` is usable as-is; `ProcessSample()` is
not.

### 2.18 `princarg` differs from the reference by 2π at the wrap boundary

`src/utility/princarg.h:44` vs `M_files_chap07/princarg.m`.

The two expressions are algebraically identical wherever `u = (p+π)/2π` is not an integer:
MATLAB's `mod(p+π, −2π) + π` expands to `p + 2π − 2π·ceil(u)`, the C++ to `p − 2π·floor(u)`,
and `ceil(u) = floor(u)+1` collapses them. **At integer `u` — that is, at odd multiples of
π — `ceil(u) = floor(u)` and the two differ by exactly 2π.**

`VERIFIED`, sweeping float inputs and comparing against a double-precision evaluation of
the MATLAB expression:

```
p = +3.141592741 (float π)   MATLAB = -3.141592566   C++ = +3.141592741   diff = 6.283185
p = +9.424777985 (float 3π)  MATLAB = -3.141592630   C++ = +3.141592503   diff = 6.283185
p = -9.424777985             MATLAB = +3.141592630   C++ = -3.141592503   diff = 6.283185
sweep ±200 rad at 1e-4: 2 points with |diff| > 1 rad, worst 6.283181 at p = 172.7876 (55π)
```

One ULP either side of the boundary the two agree to 1e-7, so the discrepancy is a knife
edge, not a region — but it is reachable, and `princarg` is applied in the phase vocoder to
`phi − phi0 − omega`, a phase difference that sits near ±π routinely. A 2π error there
corrupts that bin's frequency estimate for that frame.

Which side each implementation lands on is not the interesting part and depends on the
float representation and on `TWOPI` being a float literal (6.28318548f) while `M_PI` is a
double — the measured direction is in fact the opposite of what the algebra predicts for
exact reals, because `float(π) > π`. The finding is the 2π gap itself.

This supersedes the "equivalent refactor, worst deviation 1.3e-05 rad" claim in an earlier
revision of this document, which sampled the boundary at a decimal literal rather than at
the exact float and therefore missed it. Found by the Codex review on PR #9.

### 2.19 `LPIIRComb` — the loop filter is not the book's, and the damping map is non-monotonic

`src/effects/lp_iir_comb.h:220-227` vs `M_files_chap02/lpiircomb.m:11-14`. This module was
listed as defective in the tally with only a passing phrase and no evidence; that gap was
found by the Codex review on PR #9 and this section supplies it.

The comb structure itself is a faithful port — the LP is applied to the delayed sample
before the feedback gain, the one-pole state is updated inside the loop, and the delay line
stores `y`, all matching `lpiircomb.m:17-24`. The **loop filter** is not.

The reference uses `b_0 = 0.5, b_1 = 0.5, a_1 = 0.7` — a one-pole, one-zero lowpass.
`RecalculateCoefficients()` hard-codes `a1_ = 0.0f` with the comment "Simple 1-pole LP",
and derives `b0_ = 1 - damping_`, `b1_ = damping_`. With no pole it is not a one-pole
filter at all; it is a two-tap FIR. `VERIFIED` by evaluating both:

| | H(DC) | H(Nyquist) |
|---|---:|---:|
| `lpiircomb.m` | 0.5882 | 0.0000 |
| C++, `damping` = 0.0 | 1.0000 | +1.0000 |
| C++, `damping` = 0.5 | 1.0000 | 0.0000 |
| C++, `damping` = 1.0 | 1.0000 | −1.0000 |

Two consequences:

- **The decay is far longer than the reference for the same feedback gain.** Loop gain is
  `g · H_lp`, and the reference's LP contributes 0.588 at DC where the port contributes
  1.000 — so a `g` chosen from the book gives a much longer tail here. `SetFeedback` clamps
  to ±0.999 and `max|H_lp| = |b0| + |b1| = 1`, so the stability condition holds, but only
  marginally and it is undocumented in the header.
- **`damping` is non-monotonic and its documented sense is wrong.** Maximum HF attenuation
  is at `damping = 0.5` (null at Nyquist). Above that the attenuation *decreases* and the
  HF phase inverts; at `damping = 1.0` the filter is unity-gain at every frequency — a pure
  one-sample delay. The header documents 1.0 as maximum damping, which is exactly the
  setting that produces none, and which silently lengthens the loop by a sample.

The book's coefficients are unreachable through the public API: `a1_` is a dead member with
no setter. `PROPOSED`: expose `b0/b1/a1`, or map `damping` onto a real one-pole whose
response is monotonic in the parameter, and correct both comments.

### 2.20 `CrossCorrelation::ComputeNormalized` is biased toward small lags

`src/utility/xcorr.h:71-93`. This module was counted defective in the tally on the grounds
that its normalisation differs from `xcorr_norm.m` — which §4.2 of this same document then
called defensible, since `xcorr.h` cites Chapter 6 §6.3 (SOLA) and never claims the
Chapter 9 routine. That was a contradiction, and the Codex review on PR #9 was right to
flag it. The classification stands, but for a defect of the module's own, not a mismatch
against a reference it never invoked.

`energy_x` is accumulated once over the **full** `length` (lines 74-77), while both the
numerator `sum` and `energy_y` are accumulated only over `overlap = length - lag`
(lines 84-87). The denominator's support therefore does not shrink with the numerator's,
and the returned value is scaled by `sqrt(overlap/length)` regardless of the true
correlation.

`VERIFIED` by autocorrelating a perfectly periodic signal with itself (N = 256,
period = 32), where every multiple of the period must return exactly 1.0:

| lag | returned | true |
|---:|---:|---:|
| 0 | 1.000000 | 1.0 |
| 32 | 0.935414 | 1.0 |
| 64 | 0.866025 | 1.0 |
| 96 | 0.790569 | 1.0 |
| 127 | 0.693314 | — |

The decay is exactly `sqrt((N-lag)/N)`. The docstring at line 63 promises a result "in
range [-1, 1]" and scale-invariant matching; the peak is attenuated monotonically instead,
so a lag search using this function is biased toward short lags. That matters precisely
because the header cites SOLA as its purpose, and a SOLA lag search is what it would be
used for. `PROPOSED`: accumulate `energy_x` over the same `overlap` window as the
numerator.

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
- **`FDNReverb::Process`** — the recirculating core is bit-exact against `delaynetwork.m`
  over 1200 samples (max abs diff 0.0), including all sixteen feedback-matrix entries and
  signs. **Measured with damping disabled and dry and wet both forced to unity**, which is
  the only configuration in which the two are comparable. As shipped it does not reproduce
  the reference: `damping_` defaults to 0.3 (the reference has no damping at all) and the
  output is a convex mix `(1−mix)·dry + mix·wet` with `mix_` defaulting to 0.5, whereas
  `delaynetwork.m:37` emits `dry + wet` at unity each. No value of `mix_` makes a convex
  combination equal to a sum, so the reference's output level is unreachable through the
  public API. See also the non-prime delay lengths after sample-rate scaling (4.3).
- **`CompressorExpander`** core — attack/release selection direction and the lookahead
  topology both match `compexp.m`.
- **`Tube`** waveshaper — both removable singularities (`x==Q` and the `Q==0`, `x==0`
  case) are handled; the HP and LP difference equations match `tube.m` exactly. The
  waveshaper only; the shipped audio path drops the reference's normalisations (2.11), so
  the module is counted defective.
- **`RingMod`** — true multiplicative modulation with a correctly wrapped phase
  accumulator.
- **`EnvelopeFollower`** — canonical one-pole; consistent with the complementary form
  used inside `CompressorExpander`.
- **`FFTHandler`** — `VERIFIED` against a double-precision reference DFT at N = 8, 16 and
  1024: forward, inverse and round-trip all agree to float epsilon, with the `1/N` inverse
  scaling applied exactly once. Correct twiddle sign and bit-reversal.
- **`princarg`** — equivalent to `princarg.m` on the interior, and it uses `floor` rather
  than `fmod`, avoiding the sign trap for negative input. **It is not equivalent at the
  wrap boundary** — see 2.18.
- **`Windows::Hanning`** — the **periodic** form `hanningz.m` requires, not MATLAB's
  symmetric `hann`. `VERIFIED` COLA of `w²`: exactly 1.5 at hop N/4, 3.0 at hop N/8.
  (The in-file comment calls it "symmetric"; the comment is wrong, the code is right.)
- **`CrosstalkCanceller`** matrix algebra — the 2×2 complex `inv(CᴴC+βI)Cᴴ` and the
  ipsi/contra channel routing both check out term by term. No channel swap.
- **`Whisperization`** phase/magnitude handling and **`Robotization`** spectral core —
  both exact; see 2.13 for the buffering that defeats them.
- **`YIN`** block mode — `VERIFIED` 220.0008 Hz at confidence 0.99997 on a 220 Hz frame,
  correct on silence and on noise, ASAN-clean. Only `ProcessSample()` is broken (2.17).
- **`Vibrato`** coefficient derivation — `DELAY`, `WIDTH` and the buffer length all match
  `vibrato.m:12-21`; the effect is 100 % wet as the reference is, with no undocumented
  mix control. The defects are in the buffer lifetime, the LFO phase and the taps (2.15).

### Scope summary

Counting a module as **clean** only where its audio path matches the reference with no
finding above MINOR:

| Group | Clean | Defective | Modules |
|---|---:|---:|---|
| Filters | 2 | 1 | ✔ LowShelving, PeakFilter — ✘ HighShelving |
| Delay / comb / reverb | 0 | 4 | ✘ FDNReverb, UniversalComb, LPIIRComb, CircularBuffer |
| Dynamics / nonlinear | 2 | 5 | ✔ RingMod, EnvelopeFollower — ✘ Tube, NoiseGate, WahWah, ToneStack, CompressorExpander |
| Spatial | 0 | 3 | ✘ StereoPan, CrosstalkCanceller, SimpleHRIR |
| Spectral | 0 | 4 | ✘ Robotization, Whisperization, SpectralFilter, PhaseVocoder |
| Time-domain pitch/time | 0 | 3 | ✘ SOLATimeStretch, Vibrato, YIN |
| Low-level utility | 2 | 2 | ✔ FFTHandler, Windows — ✘ princarg, xcorr |
| **Total** | **6** | **22** | |

Five of the twenty-two are defective only in a preset, one mode or a single boundary value
(`UniversalComb`, `YIN`, `princarg`), or in configuration rather than structure
(`FDNReverb`, `Tube`), and have a correct core. That distinction is worth keeping when
planning work — a wrong preset is a one-line fix, a wrong algorithm is a rewrite — but it
does not make those modules clean.

`FDNReverb` and `Tube` both moved out of the clean column on review, for the same reason
and in two successive rounds. Their cores are faithful — `FDNReverb`'s recirculating
network is bit-exact, `Tube`'s waveshaper handles both removable singularities correctly —
but the criterion stated above is that the **audio path** matches the reference, and
neither does as shipped: `FDNReverb`'s damping and convex mix cannot express the
reference's output (§3, 4.3), and `Tube` drops all three of the reference's signal-dependent
normalisations (2.11). Recording a caveat under the table while leaving the row unchanged
is not the same as counting it correctly, and it took an external reviewer to say so twice.

The shape of the result matters more than the count: **the low-level layer is sounder than
the wrappers around it.** Every FFT, window and matrix-algebra check passed, and the one
low-level defect found (`princarg`, 2.18) is a single boundary value rather than a
structural error. Almost every failure is in buffering, state management, or a parameter
mapping — not in the DSP mathematics.

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

### 4.2 Source attribution is present but not always actionable

An earlier revision of this document claimed that eleven headers carry no DAFX reference
and named nine of them. **That was wrong.** It was produced by a script that searched only
the first 25 lines of each header for a `.m` filename, so it missed every citation written
as a chapter and section, and its arithmetic did not match its own list. Checking the full
text of each header instead:

```
analysis/yin.h              DAFX 2nd Ed., Chapter 9, yinDAFX.m
effects/tonestack.h         DAFX 2nd Ed., Chapter 12, Section 12.4
effects/wahwah.h            DAFX 2nd Ed., Chapter 12, Section 12.3
modulation/ringmod.h        DAFX 2nd Ed., Chapter 3, Section 3.2
utility/circularbuffer.h    DAFX 2nd Ed., Chapter 2, Section 2.4
utility/envelopefollower.h  DAFX 2nd Ed., Chapter 4, Section 4.2
utility/fft_handler.h       DAFX 2nd Ed., Chapter 7
utility/windows.h           DAFX 2nd Ed., Chapter 6 §6.2 and Chapter 7 §7.1
utility/xcorr.h             DAFX 2nd Ed., Chapter 6, Section 6.3
```

Every one carries a citation. Only the three `pedal_harness/*.hpp` files have none, and
they are harness code, legitimately outside DAFX scope. Found by the Codex review on PR #9.

The real attribution problem is narrower and is a genuine cause of defects in this audit:

- **`highshelving.h` cites `lowshelving.m`** and describes itself as that file with the
  output sign changed. That is exactly the mis-attribution that produced defect 2.4 — the
  cut coefficient differs between the two filters and the citation gave no reason to check.
- **A chapter-and-section citation cannot be diffed.** Where a `.m` file exists, naming the
  chapter instead of the file means a reviewer cannot mechanically compare the port to its
  source. `xcorr.h` cites Chapter 6 §6.3 (SOLA) while implementing a normalisation that
  differs from `xcorr_norm.m` in Chapter 9. That difference is defensible — it never
  claimed that file — and is not what makes the module defective; see 2.20 for the defect
  that is its own.
- **Where no `.m` exists, the citation should say so.** `tonestack.h` and `wahwah.h` cite
  Chapter 12 sections that have no accompanying script, so no bit-exactness test is
  possible for them at all. Recording that explicitly would have made the gap visible
  instead of implying a reference that cannot be checked.

### 4.3 `FDNReverb`'s delay scaling does not preserve coprimality

`src/effects/fdn_reverb.h:265-274`. The base delays are the four primes
`149, 211, 263, 293` that `delaynetwork.m:28` specifies, but `RecalculateDelays()` scales
them by `sample_rate_/44100` and truncates, which preserves nothing about their factors.
`VERIFIED` across the sample rates a Daisy project might plausibly use:

| fs | delays after scaling | pairwise coprime? |
|---|---|---|
| 8 kHz | 27, 38, 47, 53 | yes |
| 12 kHz | 40, 57, 71, 79 | yes |
| 16 kHz | 54, 76, 95, 106 | **no** — max gcd 19 |
| 22.05 kHz | 74, 105, 131, 146 | **no** — max gcd 2 |
| 32 kHz | 108, 153, 190, 212 | **no** — max gcd 9 |
| 44.1 kHz | 149, 211, 263, 293 | yes (the base case) |
| **48 kHz** | **162, 229, 286, 318** | **no** — max gcd 6 |
| 88.2 kHz | 298, 422, 526, 586 | **no** — max gcd 2 |
| 96 kHz | 324, 459, 572, 637 | **no** — max gcd 27 |

Six of the nine lose it, including the library's own documented 48 kHz, where three of the
four delays become even. It survives at 8 and 12 kHz by luck, not by construction — an
earlier revision of this section claimed the property was lost at *every* rate but
44.1 kHz, which is false and was found by the Codex review on PR #9.

Mutually prime lengths are what keep the network's echo times from coinciding; once they
share factors the modal density collapses onto repeated delays and the tail acquires a
metallic flutter. `SetDelayScale()` (`fdn_reverb.h:191`) has the same effect at arbitrary
scale factors, and there the outcome is not predictable at all. `PROPOSED`: snap each
scaled length to the nearest prime rather than truncating — which makes the property hold
by construction instead of by arithmetic accident.

### 4.4 A note on the reference itself

`M_files_chap04/lpiircomb.m:21` assigns `xhhold` where every other line uses `xhold` — a
typo in the published book code, so that variable stays 0 for the whole run. The C++
implements the *intended* equation and therefore differs from the verbatim script. This
should be recorded in `dafx_bugs.md` before anyone writes a bit-exactness test against
the script. `UNVERIFIED` as to the book's printed text; `DERIVED` from the file.

---

## 5. Recommended order of work

**Memory safety first**, because it corrupts silently on a target with no MMU:

1. `Vibrato` — the `SetWidth` heap overflow (2.15), then the LFO phase and the
   interpolation taps.
2. `CrosstalkCanceller` — zero the full HRIR buffer (2.9). 112 indeterminate floats
   currently reach the FFT, so the module's output is nondeterministic and every other fix
   to it is unmeasurable until this is done. Then fix the overlap-add, which folds each
   transform's tail onto its own head and so reintroduces the circular convolution the
   zero-padding exists to prevent; only after both is the missing `fftshift` worth
   assessing. Then fix the overlap-add, which folds each
   transform's tail onto its own head and reintroduces circular convolution; only after
   both is the missing `fftshift` worth assessing.

**Then the one-line fixes**, which buy the most correctness per unit of risk:

3. `HighShelving:43` — the cut coefficient (2.4).
4. `UniversalComb::SetAllpass` — `FB=-g, FF=1, BL=g` (2.5).
5. `PhaseVocoder:256` — `tstretch = pitch_ratio_` (2.13); measured to make every upward
   ratio exact.
6. `SpectralFilter:48` — the default template argument cannot be instantiated (2.12).
   Nothing else in that file can be tested until this is resolved.
7. `princarg.h:44` — pin the wrap boundary (2.18), and make `TWOPI` and `M_PI` the same
   precision while there. Cheap, and it removes a 2π trap from the one function every
   spectral effect depends on.

**Then the modules that are non-functional as shipped:**

8. `NoiseGate` — the three logic defects together (2.1a-c); fixing the condition alone
   makes it worse. Then the two-pole envelope detector (2.1d), without which the timing
   still will not match the reference.
9. `WahWah` — a real LFO accumulator and a normalised denominator (2.2).
10. `SimpleHRIR` — use `theta_shifted` for the group delay (2.7). This also unblocks
   `CrosstalkCanceller`, which needs its omitted `fftshift` (2.9).
11. The shared analysis/synthesis buffering in `Robotization` and `Whisperization` (2.13).
   The spectral cores are already exact, so this is the only thing between them and a
   correct port.
12. `ToneStack` — implement filters or rename the class and withdraw the claim (2.3).
13. `SOLATimeStretch` — rewrite (2.16).
14. `CircularBuffer` — clamp and document the valid delay domain (2.6).
15. `CrossCorrelation::ComputeNormalized` — accumulate `energy_x` over the same window as
    the numerator (2.20); one loop moved, and it un-biases any lag search built on it.
16. `CompressorExpander` — convert the reference's coefficients to time constants
    properly (2.10a) and stop `RecalculateCoefficients()` clobbering `tav_` (2.10b).
17. `LPIIRComb` — give the loop filter a pole, or map `damping` onto one monotonically,
    and correct the two comments that misdescribe it (2.19).
18. `FDNReverb` — make the reference configuration reachable: damping off by default
    or documented, and a gain structure that can express `dry + wet` at unity (§3, 4.3).

**Then the process problems, which are what let all of the above ship:**

19. Restore the nine excluded test files to the build (1a) and fix what turns red.
20. Fix the GCC build (4.1), then add `unit_tests` to the CI build targets and drop
    `continue-on-error: true` from `legacy-regression` (1d). Until both are done, no
    amount of test-writing changes what CI reports.
21. **Replace the smoke tests with reference comparisons.** Every defect in this audit was
    found by comparing against MATLAB; none by the existing 151 tests. The cheapest
    durable fix is a golden-vector harness: for each module, store a short input and the
    MATLAB output, and assert agreement to a stated tolerance. The drivers written for
    this audit are a working template.

### Defects in the MATLAB references themselves

Record these in `dafx_bugs.md` **before** anyone writes bit-exactness tests, or the tests
will be written against the wrong target:

- `lpiircomb.m:21` assigns `xhhold` where every other line uses `xhold`, so that variable
  stays 0 for the whole run. The C++ implements the intended equation. `DERIVED`.
- `TimeScaleSOLA.m:46` applies the `xcorr` lag with an inverted sign. Negating it measures
  up to 56 dB better on pure tones with no level loss. `DERIVED`.
- `yinDAFX.m:43` starts its cumulative-mean sum at τ=2 while still multiplying by τ, so it
  omits `d(1)` from the denominator and is not canonical YIN. The C++ is canonical and
  therefore will not match it at small τ. `DERIVED`.
- `yinDAFX.m:53` can index `yinTemp(taumax+1)`, past the end. The C++ bounds the descent.

## 6. Checks not run

- Target-hardware build, timing, CPU load, audio, electrical and product readiness:
  `NOT_RUN`. Nothing in this audit was executed on a Daisy.
- `src/pedal_harness/*.hpp` — harness code, not a DAFX port; out of scope here.
- `CrosstalkCanceller`'s end-to-end degradation from the missing `fftshift` (2.9) is
  `DERIVED` from the reference computation, not measured against the shipped C++.
- Frame-burst CPU cost (2.14) is `DERIVED` from the code, not profiled on target.
