# Pedal & Synth Harness — Daily engineering run
## 2026-07-25

## Executive summary

**WP-02 now has a green host-CI implementation of the first portable DIGI-delay node.**

The increment implements a real audio processor against the WP-01 contract instead of expanding the framework again. It uses caller-owned stereo memory, integer delay timing, independent input injection and feedback, optional feedback HPF/LPF conditioning, deterministic reset and finite/index guards.

The dedicated `Pedal Harness Contract` workflow configured, built and passed the complete scoped test target on a clean Linux runner. It has **not** been built for ARM/Daisy, profiled, flashed, listened to or measured electrically.

---

## 1. State and changes since the previous run

### Previous state

- Draft PR #1 contained the DSP contract, static graph and contract smoke tests.
- The dedicated contract workflow was green.
- The next action was one bounded portable DIGI-delay node.

### Changes made today

- Added `src/pedal_harness/digital_delay_node.hpp`.
- Added `tests/test_digital_delay_node.cpp`.
- Registered the delay tests in `pedal_harness_tests`.
- Extended the scoped workflow path trigger to the new test file.
- Ran the scoped workflow successfully:
  - configure: PASS;
  - build: PASS;
  - tests: PASS.

### Current GitHub state

- Repository: `Denys/DAFX_2_Daisy_lib`
- Branch: `feat/pedal-harness-wp01-contract`
- Draft PR: #1
- Scoped portable-harness workflow: **PASS**
- General aggregate workflow: Linux/macOS legacy builds still fail; code-quality passes
- Merge: not performed

---

## 2. Main design decision

### ACCEPTED

Use **caller-owned stereo ring memory inside the DIGI node**, allocated through `StaticArena`, with this processing order:

```text
read delayed sample
    -> feedback HPF
    -> feedback LPF
    -> feedback gain
    -> add independent new-input send
    -> write ring memory

output = dry input + wet delayed sample
```

The key separation is:

```text
write = input_send * input + feedback * delayed_conditioned
```

not:

```text
write = (input + delayed) * feedback
```

The second form incorrectly disables input injection when feedback is zero and couples two controls that must remain independent.

### DEFERRED

- fractional interpolation;
- smooth large delay-time changes;
- ping-pong/cross-feedback;
- saturation and feedback limiting;
- tape transport semantics;
- reverse/granular scheduling;
- freeze state machine;
- tap tempo and MIDI clock;
- Daisy runtime adapter.

---

## 3. Implementation contract

### Node identity

- stable ID: `phh.delay.digi.v0`
- layout: stereo only in this increment
- default tail policy: preserve
- memory: `2 × (max_delay_samples + 1) × sizeof(float)`
- external-memory advisory: true above 64 KiB

### Parameters

| Index | Parameter | Current behavior |
|---:|---|---|
| 0 | delay samples | rounded and clamped to `1…max_delay_samples` |
| 1 | feedback | clamped to `−0.999…+0.999` |
| 2 | input send | clamped to `0…1` |
| 3 | dry mix | clamped to `0…2` |
| 4 | wet mix | clamped to `0…2` |
| 5 | feedback low cut | one-pole HPF, bypass at 0 Hz |
| 6 | feedback high cut | one-pole LPF, bypass at the Nyquist guard |

### Reset and tails

`Reset()` flushes ring memory and filter state. Tail preservation therefore means **do not call reset** during a transition that should spill over. The orchestration layer owns that decision until real preset/mode-change requirements justify a richer API.

---

## 4. Executed verification

### Scoped command

```bash
cmake -S . -B phh-build -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_TESTING=ON \
  -DBUILD_EXAMPLES=OFF

cmake --build phh-build \
  --target pedal_harness_tests \
  --config Release

ctest --test-dir phh-build \
  -C Release \
  -R "^PedalHarnessContractTests$" \
  --output-on-failure --verbose
```

### Covered by tests

- integer stereo impulse timing;
- independent input injection with zero feedback;
- feedback decay at `1.0, 0.5, 0.25`;
- write-pointer continuity across block boundaries and ring wrap;
- reset/tail flush;
- maximum-delay clamp;
- feedback clamp;
- NaN input sanitization and diagnostic count;
- insufficient-memory rejection;
- unsupported-layout rejection;
- existing WP-01 graph/arena tests.

### Not covered

- ARM compiler and Daisy/libDaisy integration;
- Cortex-M7 cycles and SDRAM cache behavior;
- delay-time transition clicks;
- block-to-block coefficient zippering;
- HPF/LPF frequency-response verification;
- long-duration high-feedback soak;
- stereo correlation and mono compatibility;
- codec, analog front end, bypass and real hardware.

---

## 5. Feasibility, BOM/cost, TTM and commercial potential

### Memory and schedule

| Item | Estimate / status |
|---|---:|
| Additional hardware BOM | 0 CHF |
| Stereo 2 s buffer, 48 kHz float32 | 768,000 bytes |
| Stereo 8 s buffer, 48 kHz float32 | 3,072,000 bytes |
| Fractional-read policy | 1–3 focused days |
| Daisy adapter and target profiling | 3–7 focused days |
| First audible DIGI prototype | roughly 1–2 focused weeks |

The memory values are derived from `sample_rate × seconds × channels × 4 bytes`. Capacity is not the likely bottleneck on Daisy-class external SDRAM; access pattern, cache behavior, callback margin and transition policy are more credible constraints.

### Product reuse

The node has plausible reuse as:

- neutral baseline for the pilot delay pedal;
- short-delay primitive for multi-FX chorus/flanger/rotary;
- loopback and latency stimulus for Synth Harness diagnostics;
- testable SDK example for custom firmware clients;
- known-good reference during repair or comparison of third-party delay code.

Direct commercial value remains **pre-product**. A tested header is infrastructure, not a pedal SKU, however inspirational the README eventually becomes.

---

## 6. Independent critical review

### HIGH — delay-time changes are discontinuous

Changing the integer delay moves the read tap instantly and can click. The next transition layer must choose explicitly between pitch-producing slew, dual-tap crossfade and intentional glitch.

### HIGH — stereo-only contract is narrow

The node rejects `Mono`. This is deliberate for the first graph, but mono-in/stereo-out duplication and dual-mono behavior must be defined in the target adapter rather than improvised inside every algorithm.

### MEDIUM — filter coefficients are recalculated per block

Two exponentials per active cutoff per block are bounded but need target profiling. A control-rate coefficient service may later be preferable.

### MEDIUM — no feedback limiter

Feedback is clamped below unity, appropriate for clean DIGI. Freeze and deliberate self-oscillation need a separate bounded policy, not a casual coefficient increase.

### MEDIUM — reset reason is ignored

Every reset flushes. This is unambiguous, but preset recall, mode changes and fault recovery will eventually need explicit orchestration tests.

### LOW — dry/wet gains permit 2×

This is useful for integration experiments but can clip. Product presets need bounded loudness and a later graph/output safety policy.

---

## 7. Acceptance criteria

| Criterion | Result |
|---|---:|
| Clean CMake configure | PASS |
| Strict project build target | PASS |
| Scoped GoogleTest/CTest | PASS |
| Caller-owned memory | PASS by implementation |
| No heap in `Process` | PASS by inspection |
| Input and feedback independently controlled | PASS |
| Integer timing and wrap | PASS |
| Finite/index guards | PASS |
| ARM/Daisy build | NOT RUN |
| CPU/SDRAM profiling | NOT RUN |
| Audio bench/listening | NOT RUN |

**WP-02 status: PASS ON HOST, HOLD FOR TARGET FEASIBILITY.**

---

## 8. Next action

**Add the first fractional-read policy and compare linear versus cubic/Lagrange behavior with repeat-loop test vectors before target integration.**

Acceptance should include static fractional timing, moving-head modulation sidebands, repeated-loop high-frequency loss and CPU-cost estimates. Do not add tape coloration yet. First establish what the read head does; then humanity may give it simulated capstan anxiety.
