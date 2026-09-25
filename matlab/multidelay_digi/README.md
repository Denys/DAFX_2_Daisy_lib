# Mono-first DIGI model (MATLAB / Octave + Simulink)

Non-product reference model of the ADR-0019 mono-first `DIGI` delay: `SINGLE`,
`DUAL SERIES` and `DUAL PARALLEL` on two mono engines with one output. It implements
the code-free contract v1 (custom-pedals
`work_products/delay/runs/2026-09-24_mono_first_digi_contract_v1.md`, accepted
2026-09-25). It lives outside `src/`, is not built by the root CMake and changes no
firmware.

Each engine follows the law of `phh::DigitalDelayNode::Process` @ `73976da`
(`src/pedal_harness/digital_delay_node.hpp:73-143`, filters `180-227`), in mono:

```text
tap = h[D];  cond = LP(HP(tap));  h <- send*in + fb*cond
in1 = x;  in2 = 0 (SINGLE) | tap1 (SERIES) | x (PARALLEL)
y   = Dry*x + Wet*(g1*tap1 + g2*tap2),  g = [1 0] | [1 1] | 0.5*[1 1]
SERIES feedback per engine: f = 1 - sqrt(1 - k)   (contract section 6)
```

## Files

| File | Role |
|---|---|
| `mdd_default_params.m` | parameter struct with the contract v1 defaults |
| `mdd_controls_to_params.m` | contract sections 5–6: TIME, ratio, FEEDBACK (+ SERIES compensation), COLOR, MIX → engine parameters |
| `mdd_sanitize_params.m` | clamps and fallbacks mirroring the node's `ClampFinite` |
| `mdd_init_state.m` | histories (power-of-two ring), filter/allpass/smoother state, counters |
| `mdd_read.m` | readers: `integer` (parity with main), `linear`, `allpass1`, `lagrange3` |
| `mdd_process_block.m` | pure kernel: `[y, state, taps] = mdd_process_block(x, params, state)` |
| `mdd_transfer_oracle.m` | independent rational transfer function per configuration |
| `MultiDelayRef.m` | `matlab.System` wrapper (MATLAB only, no DSP inside) |
| `multidelay_fixtures.m` | stimuli, raw float32 outputs and JSON metrics (M1–M3, F1–F7, parity inputs) |
| `build_mdd_digi_slx.m` | Simulink builder, SVG export and smoke simulation |
| `tests/run_mdd_tests.m` | test runner T1–T14 |
| `docs/slx_export/*.svg` | exported block diagrams (top, engine, router, mixer) |
| `../../tools/matlab_parity/` | standalone C++ dump of `DigitalDelayNode` for test T9 |

## Commands

From the repository root:

```text
matlab -batch "cd('matlab/multidelay_digi/tests'); run_mdd_tests"            quick suite
matlab -batch "cd('matlab/multidelay_digi/tests'); run_mdd_tests('full')"    T13 at contract length (60 s)
octave-cli --no-gui --eval "cd matlab/multidelay_digi/tests; run_mdd_tests"  same suite in Octave >= 8 (T14 SKIP)
matlab -batch "cd('matlab/multidelay_digi'); multidelay_fixtures('fixtures_out','Quick',true)"
cmake -S tools/matlab_parity -B tools/matlab_parity/build && cmake --build tools/matlab_parity/build
tools/matlab_parity/build/dump_digital_delay_node matlab/multidelay_digi/fixtures_out/parity/P1_in.f32 tools/matlab_parity/out/P1_out.f32 $(cat matlab/multidelay_digi/fixtures_out/parity/P1_params.txt)
tools/matlab_parity/build/dump_digital_delay_node matlab/multidelay_digi/fixtures_out/parity/P1_in.f32 tools/matlab_parity/out/P2_out.f32 $(cat matlab/multidelay_digi/fixtures_out/parity/P2_params.txt)
matlab -batch "cd('matlab/multidelay_digi'); build_mdd_digi_slx(mdd_default_params(),'ExportSvg',true)"
```

T9 (C++ parity) runs only after the fixture and dump commands; otherwise it prints
`SKIP`. On Windows, build the parity tool with a host compiler (for example MSYS2
`-G "MinGW Makefiles" -DCMAKE_CXX_COMPILER=C:/msys64/ucrt64/bin/g++.exe`), not the ARM
cross compiler of the Daisy toolchain.

## Simulink diagram

Colour legend: **green** = mirrors `VERIFIED` main code (the engine law); **orange** =
topology from contract v1 (router, mixer, gains); **grey** = `HOLD` (config transition
policy, future stereo seam). Each engine's history is drawn as `Unit Delay` + variable
`Delay (D-1)`: a single variable-length Delay closes an algebraic loop in Simulink. The
router and mixer `Multiport Switch` blocks use one-based data ports (`DIGI_CONFIG` 1, 2, 3),
and an out-of-range value is an error. The model is double precision with the integer
reader only. The builder's smoke simulation compares it with `mdd_process_block`.

## Contract options (defaults fixed by contract v1, kept as switches)

| Option | Default | Alternative |
|---|---|---|
| `SeriesFeedPoint` | `'tap'` (E2 repeats E1's echoes) | `'write'` |
| `SeriesGains` | `[1 1]` | any `[g1 g2]` |
| `ParallelNorm` | `0.5` | `1/sqrt(2)`, `1` |
| `SingleE2Input` | `'zero'` (E2 decays) | `'hold'` |
| `DIGI CONFIG` transition | hard switch at the block boundary | crossfade `HOLD` (v2, fixture F8) |

## Limits

Host results only. Nothing here proves target timing, audio quality, electrical
behaviour or product readiness. Fixture metrics carry no pass/fail threshold
(`FREEZE_AT_G2`). No code from `DAFX-MATLAB/` (book code, educational licence) is reused.
