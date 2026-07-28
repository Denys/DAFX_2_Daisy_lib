# Pedal & Synth Harness — Daily engineering run
## 2026-07-28

## Executive summary

**The project no longer waits globally for the next Daisy bench session.**

Today's increment creates a dependency-safe development plan and specifies WP-04, the delay-time transition contract. WP-03B remains the hardware lane for Cortex-M7/SRAM/SDRAM evidence; WP-04, pilot requirements, preset/diagnostic ABI and Harness Lite work may proceed in parallel.

No DSP implementation, merge, flash or hardware measurement is claimed in this run.

## 1. State and changes since the previous run

### Verified repository state

- Repository: `Denys/DAFX_2_Daisy_lib`
- Main baseline: `1f52f7ca4bb6568707de40793e40165a66a6ed60`
- PR #1 is merged and contains the portable contract, static graph and DIGI integer delay.
- PR #2 remains open, mergeable and draft on `feat/pedal-harness-fractional-read`.
- PR #2 explicitly lacks Cortex-M7, SRAM/SDRAM and callback-budget evidence.
- The existing `DigitalDelayNode` still rounds delay time to an integer and changes read index directly.

### Changes made today

Created branch:

```text
docs/pedal-platform-roadmap-wp04
```

Added:

```text
docs/roadmap/pedal_synth_harness_development_plan.md
docs/architecture/WP04_delay_time_transition_contract.md
work_products/pedal_synth_harness_daily_2026-07-28.md
```

The roadmap records parallel lanes, dependencies, evidence gates, work packages, schedule estimates, cost envelope and commercial extraction path.

WP-04 defines explicit policies for:

- intentional single-head slew/pitch behaviour;
- dual-head transition for tap tempo, preset and mode changes;
- explicit immediate/glitch behaviour;
- bounded last-writer-wins request handling during an active crossfade.

## 2. Main design decision

### Accepted

**Hardware availability is a lane dependency, not a programme-wide stop condition.**

Only claims that depend on target evidence wait for WP-03B. Architecture, host vectors, pilot requirements, preset/diagnostic contracts, Harness Lite software and commercial work continue.

### Accepted for WP-04

Delay-time changes are semantic events, not merely new float values.

```text
continuous musical motion
    → SLEW_PITCH

discrete tap/preset/mode target
    → DUAL_HEAD_XFADE

explicit creative/debug jump
    → IMMEDIATE_GLITCH
```

### Rejected

- one global smoothing constant for all delay changes;
- direct read-index assignment as the silent default;
- inferring transition policy only from numerical delta;
- starting tape, reverse or freeze before the base transition service is characterised;
- treating the pending Daisy benchmark as permission to stop client, UI, harness and architecture work.

## 3. Feasibility, cost, TTM and commercial potential

### Technical feasibility

WP-04 is host-testable and does not require libDaisy or connected hardware.

It produces read positions and gains; it does not own memory, feedback filters, MIDI, UI or storage. This keeps the service portable across Daisy, Teensy, custom STM32H7 and host tooling.

### Estimates

| Increment | Focused effort | Hardware cost |
|---|---:|---:|
| Roadmap and WP-04 contract | completed | 0 CHF |
| Host transition state machine | 1–2 days | 0 CHF |
| Host signal characterization | 1–2 days | 0 CHF |
| Fractional-reader integration | 1–2 days after PR #2 resolution | 0 CHF |
| Daisy profiling | 0.5–1 day with local board | 0–30 CHF incremental |

### TTM impact

WP-04 adds roughly 3–6 focused days before advanced delay modes. It reduces risk across:

- tap tempo;
- preset recall;
- subdivision changes;
- chorus/flanger base-delay changes;
- mode entry/exit;
- later reverse/grain transitions;
- click/transient regression tests in Synth Harness.

### Commercial potential

Direct revenue: none.

Indirect value: medium-high. A measured transition service becomes part of:

- custom pedal firmware;
- a reusable audio-device SDK;
- diagnostics and regression testing;
- product-extension services for boutique manufacturers;
- supportable preset and update workflows.

The saleable claim is not “we invented crossfade.” It is “time, preset and mode transitions have explicit policies, vectors and target evidence instead of random zippering inherited from a control callback.”

## 4. Concrete artifact

### Development roadmap

The saved plan defines:

- two separate products with shared foundations;
- Synth Harness roles for users, developers, repairers and manufacturers;
- work packages WP-00 through WP-12;
- parallel DSP, embedded, UX, harness and hardware/commercial lanes;
- exact blocked and non-blocked dependencies;
- schedule/cost estimates;
- commercial extraction path.

### WP-04 contract

The specification includes:

- API sketch;
- state machine;
- request arbitration;
- finite/clamped delay rules;
- slew and dual-head mathematics;
- equal-power implementation options;
- feedback-source proposal;
- host contract and signal tests;
- characterization metrics;
- initial product transition ranges;
- independent critique;
- acceptance criteria.

## 5. Independent critical review

### Strengths

- The plan removes the single-hardware-session bottleneck without weakening measurement gates.
- WP-04 is bounded and reusable.
- Product identities remain separate.
- The design distinguishes continuous motion from discrete retargeting.
- The request queue is finite and deterministic.
- No new target or audio-quality claim is made.

### Weaknesses and risks

1. **This increment is specification-only.**
   It creates no executable evidence yet.
2. **Equal-power is not equal loudness for correlated taps.**
   It may rise or dip depending on source-target phase.
3. **Dual-head transitions temporarily double read cost.**
   This must feed into WP-03B/WP-07 budgeting.
4. **Last-writer-wins queueing adds response latency.**
   Pilot users may prefer interruption or shorter transitions.
5. **The proposed feedback blend can create transient energy peaks.**
   High-feedback tests are mandatory.
6. **Pilot requirements remain incomplete.**
   An elegant transition engine can still implement the wrong feel.
7. **The repository is a DSP library, not the complete product workspace.**
   The roadmap belongs here as the shared core plan, but product hardware/CAD artifacts may later need a dedicated project repository.

### Reviewer verdict

**PASS as a planning and architecture increment.**

**HOLD for implementation claims** until a separate host-code PR builds and passes its tests.

## 6. Acceptance criteria for today's increment

| Criterion | Result |
|---|---:|
| Current main and PR #2 state inspected | PASS |
| Development can proceed without Daisy hardware | PASS by dependency analysis |
| Two product identities preserved | PASS |
| Hardware-dependent claims remain blocked | PASS |
| Roadmap saved on GitHub branch | PASS |
| WP-04 specification saved | PASS |
| Code implemented/built/tested | NOT RUN |
| Hardware benchmark | NOT RUN |
| PR merge | NOT PERFORMED |

## 7. Single next action

**Implement the host-only `DelayTimeTransition` state machine and contract tests on a separate branch from `main`.**

Keep the first code increment limited to:

- prepare/reset/request/next;
- slew and dual-head plans;
- linear and equal-power reference weights;
- one queued target;
- finite/boundary guards;
- exact-duration and retarget tests.

Do not integrate the fractional reader, feedback loop, tape, reverse, freeze, UI or Daisy BSP in the same PR. The project has finally escaped one bottleneck; manufacturing a larger one would show admirable consistency but poor judgement.
