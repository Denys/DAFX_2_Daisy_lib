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

## Quick start (how to use the model)

Tested with MATLAB R2024a + Simulink on Windows. Steps 1, 2, 5 and 6 also run in GNU
Octave ≥ 8. Steps 3–4 need Simulink.

**1. Open the folder.** Start MATLAB and go to this folder:

```matlab
cd('C:/path/to/DAFX_2_Daisy_lib/matlab/multidelay_digi')
```

**2. Check that everything works** (about 1–2 minutes). The last line must read
`MDD_TESTS PASS ...`. T9 prints `SKIP` until the C++ parity files exist (see "Commands").

```matlab
cd tests; run_mdd_tests; cd ..
```

**3. See the block diagram.** This builds `mdd_digi_topology.slx` in this folder,
refreshes the four pictures in `docs/slx_export/` and checks the diagram against the
MATLAB code (about 2 minutes):

```matlab
build_mdd_digi_slx(mdd_default_params(), 'ExportSvg', true);
open_system('mdd_digi_topology')
```

Double-click a coloured block to look inside. Press **Run**, then double-click **Scope**
to see the impulse response (a click followed by its echoes).

**4. Try another configuration in Simulink.** Rebuild the model from different front-panel
settings (table below), then press **Run**:

```matlab
p = mdd_default_params(); c = p.Ctl;
c.Config = 3;                          % 1 SINGLE, 2 DUAL SERIES, 3 DUAL PARALLEL
p = mdd_controls_to_params(c, p);
build_mdd_digi_slx(p, 'ExportSvg', false); open_system('mdd_digi_topology')
```

Use `'ExportSvg', false` here so the committed pictures keep the default settings.

**5. Listen to your own guitar through the DIGI.** Any WAV file works (mono or stereo,
any sample rate). A few seconds of silence are added so the echoes can ring out. Processing
takes about 1 s per 5 s of audio.

```matlab
[x, fs] = audioread('my_guitar.wav');
x = mean(x, 2);                        % the model is mono
x = [x; zeros(3 * fs, 1)];             % room for the echo tail
p = mdd_default_params();
p.Fs = fs;                             % set the sample rate first
c = p.Ctl;
c.Config = 2;                          % 1 SINGLE, 2 DUAL SERIES, 3 DUAL PARALLEL
c.Time = 0.6;                          % 0 ... 1  ->  20 ms ... 2.5 s
c.RatioIndex = 6;                      % E2 time = 1/4 1/3 3/8 1/2 2/3 3/4 1 of E1 (index 1 ... 7)
c.Feedback = 0.5;                      % 0 ... 1  ->  repeats; 1 = the 0.95 maximum
c.Color = 0.3;                         % 0 = bright ... 1 = dark (2 kHz)
c.Mix = 0.5;                           % 0 = dry only ... 1 = full echo level
p = mdd_controls_to_params(c, p);
st = mdd_init_state(p);
[y, st] = mdd_process_block(x, p, st);
y = double(y);
y = y / max(1, max(abs(y)));           % keep the file from clipping
audiowrite('my_guitar_digi.wav', y, fs);
sound(y, fs)
```

`st.diag.clipped_samples` counts the output samples above full scale before that last
safety scaling. High feedback on loud input can clip; the model reports it and does not
hide it.

**6. Move a knob while playing.** Settings are read once per block of 48 samples, as on
the pedal. This sweeps TIME from 0.4 to 0.7 across the file:

```matlab
p = mdd_default_params(); p.Fs = fs; c = p.Ctl;
st = mdd_init_state(p); y = zeros(size(x), 'single'); B = p.BlockSize;
for i0 = 1:B:numel(x)
  i1 = min(i0 + B - 1, numel(x));
  c.Time = 0.4 + 0.3 * i0 / numel(x);
  p = mdd_controls_to_params(c, p);
  [y(i0:i1), st] = mdd_process_block(x(i0:i1), p, st);
end
```

The delay time jumps from block to block, as on main. For smooth pitch-bending sweeps, set
`p.TimeSmoothMs = 20` and `p.Reader = 'linear'` before the loop.

### Front-panel settings (`p.Ctl`, contract v1 section 5)

| Field | Range | Meaning |
|---|---|---|
| `Config` | 1, 2, 3 | `SINGLE`, `DUAL SERIES` (E2 repeats E1's echoes), `DUAL PARALLEL` (both engines hear the guitar) |
| `Time` | 0 … 1 | E1 delay, 20 ms … 2.5 s (log taper) |
| `RatioIndex` | 1 … 7 | E2 delay as a fraction of E1: 1/4, 1/3, 3/8, 1/2, 2/3, 3/4 (default), 1 (`SHIFT`+`TIME`) |
| `Feedback` | 0 … 1 | number of repeats; 1 = the 0.95 maximum. In `DUAL SERIES` it is reduced automatically so the echoes do not build up |
| `FeedbackE2` | `NaN` or 0 … 1 | `NaN` = same as `Feedback`; a number sets E2 on its own (`SHIFT`+`FEEDBACK`) |
| `Color` | 0 … 1 | tone of the repeats: 0 = no low-pass, 1 = low-pass at 2 kHz; a 40 Hz high-pass is always on |
| `Mix` | 0 … 1 | echo level; the dry guitar always stays at full level |

Always pass the settings through `mdd_controls_to_params`. It turns them into the engine
values (`p.E(1)`, `p.E(2)`, `p.Dry`, `p.Wet`) exactly as the contract specifies.

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
