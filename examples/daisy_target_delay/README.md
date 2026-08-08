# Daisy Pod / Field portable DIGI target harness

Status: `TARGET_FIRMWARE_COMPOSITION_CANDIDATE`

This example is a bounded target adapter for the portable Pedal Harness DSP contract and the fractional-read DIGI candidate. It is intentionally separate from the existing `Daisy_Pedal_Projects` single-`activeEffect` application runtime.

## Purpose

The harness exists to answer target-specific questions that host tests cannot answer:

- does the portable stereo DIGI node compile for Cortex-M7 through libDaisy;
- what flash/internal-memory/SDRAM image does the linker produce;
- what is the callback cost for linear versus cubic Lagrange fractional reads;
- how do block size and control workload affect callback margin;
- can Pod and Field exercise the same DSP graph through thin board adapters;
- can control, gestures, UI status and diagnostics remain outside the audio callback while publishing coherent bounded snapshots.

A successful build does **not** prove flash/run behavior, audio quality, analog I/O performance, cache behavior under the final product graph, pop-free switching, or production suitability.

## Frozen graph for this increment

```text
stereo codec input
    -> phh::StaticSerialGraph<1>
        -> phh::DigitalDelayNode
    -> stereo codec output
```

- sample rate: 48 kHz;
- maximum delay: 2.0 s / 96,000 samples;
- one static graph node;
- one 1 MiB `StaticArena` backing store placed in `.sdram_bss`;
- no heap, logging, control scan, display, storage, blocking USB, MIDI service or unbounded work in the audio callback;
- coherent parameters plus bypass state are transferred through a bounded lock-free mailbox;
- callback profiling is compile-time removable with `ENABLE_PROFILING=0`.

## Runtime-domain composition

### Audio callback

The callback is limited to:

1. at most two bounded mailbox-read attempts;
2. bounded construction of the effective parameter frame;
3. one fixed-capacity DIGI graph process call;
4. optional DWT cycle-sample capture.

No control scan, logging, display update, storage, MIDI parsing or graph allocation occurs in the callback.

### Control and parameter mapping

The main loop scans board controls, rejects non-finite normalized values through deterministic fallbacks, maps harness ranges, updates the bounded gesture state and publishes one coherent realtime frame.

### Gesture state

Two rising-edge toggles are implemented for the bring-up harness:

- interpolation policy: linear ↔ cubic Lagrange;
- bypass policy: effect ↔ bypass-with-trails.

Long-press, double-click, encoder-rotation and preset/mode gestures are intentionally not active in this increment.

### Bypass and trails

The first explicit bypass policy is `preserve_trails`:

- dry path becomes unity;
- new input send into the delay becomes zero;
- the existing delay/feedback state keeps processing and decaying;
- wet level remains the current harness mix value.

This is a deterministic state policy, **not** evidence of a click-free or musically accepted transition. Bypass smoothing/crossfade remains `HOLD` for later transition testing.

### Reset

Large graph resets are prohibited in the callback because clearing the long SDRAM history is not bounded to callback time. The graph receives an explicit `PowerOn` reset before audio starts. Future mode/preset/fault resets must occur with audio stopped or through a separately validated bounded transition mechanism.

### UI / status

Only the Daisy Seed status LED is updated, and only from main/control context. Full OLED/menu rendering is absent.

### Tempo / MIDI / storage

Not relevant to the current DIGI profiling graph and therefore inactive. Their absence is deliberate rather than accidental callback work.

### Diagnostics

Profile accumulation occurs in the callback using fixed storage. Sorting, averaging, p99.9/max summarization and USB logging occur only after the sample buffer is frozen and only in main context.

## Harness-only controls

These mappings are for bring-up and profiling. They are **not** accepted product control ranges.

| Board | Control | Harness mapping |
|---|---|---|
| Daisy Pod | knob 1 | delay time, 1 sample to 2 s |
| Daisy Pod | knob 2 | feedback, 0 to 0.95 |
| Daisy Pod | button 1 | toggle linear / cubic Lagrange interpolation |
| Daisy Pod | button 2 | toggle effect / bypass-with-trails |
| Daisy Pod | MIX | fixed 0.5 dry / 0.5 wet while effect is active |
| Daisy Field | knob 1 | delay time, 1 sample to 2 s |
| Daisy Field | knob 2 | feedback, 0 to 0.95 |
| Daisy Field | knob 3 | wet mix, 0 to 1 with complementary dry mix while effect is active |
| Daisy Field | switch 1 | toggle linear / cubic Lagrange interpolation |
| Daisy Field | switch 2 | toggle effect / bypass-with-trails |

`COLOR`, `MOTION`, and `MODE/PRESET/CONTEXT` are intentionally absent from this DIGI graph. Their final mappings remain `HOLD` until the corresponding algorithms and transition policies exist on target.

## Portable runtime-control contract

`src/pedal_harness/runtime_control.hpp` provides:

- fixed-size realtime control frames;
- a bounded seqlock-style mailbox backed only by lock-free 32-bit atomics;
- deterministic toggle gesture state;
- normalized-input finite/range guards.

The host acceptance suite covers frame coherence/generation, bypass-state publication, invalid normalized inputs and deterministic gesture toggles.

## Build

The application uses libDaisy's Cortex-M7 Makefile and linker flow. Repository CI pins the libDaisy revision used for evidence; local builds should use the same pinned revision when reproducing a recorded result.

From the repository root, with libDaisy checked out at `external/libDaisy`:

```sh
make -C external/libDaisy -j2
make -C examples/daisy_target_delay clean
make -C examples/daisy_target_delay -j2 BOARD=POD AUDIO_BLOCK_SIZE=48 ENABLE_PROFILING=1
```

For Field:

```sh
make -C examples/daisy_target_delay clean
make -C examples/daisy_target_delay -j2 BOARD=FIELD AUDIO_BLOCK_SIZE=48 ENABLE_PROFILING=1
```

The libDaisy core Makefile emits:

- `build/daisy_target_delay.elf`;
- `build/daisy_target_delay.bin`;
- `build/daisy_target_delay.hex`;
- `build/daisy_target_delay.map`;
- linker `--print-memory-usage` output.

The default application type is `BOOT_SRAM`, matching the current pedal application's boot style. Flashing is deliberately outside CI.

## Runtime records

When actually run on hardware, the main loop emits two record types. Logging is outside the audio callback.

```text
PHH_TARGET board=<...> fs=<...> block=<...> sysclk=<...> arena_used=<...> arena_capacity=<...> arena_region=<...> max_delay=<...> profile=<...> bypass_policy=preserve_trails reset_policy=audio_stopped_only
PHH_PROFILE board=<...> samples=<...> avg=<...> p999=<...> max=<...> budget=<...> overruns=<...> snapshot_misses=<...> interp=<linear|cubic> bypass=<effect|trails>
```

The profiler uses the Cortex-M7 DWT cycle counter and stores 4096 callback samples before sorting/summarizing them in the main loop. `budget` is calculated from the observed system clock, block size and sample rate.

These records are target evidence only after they have actually been captured from a named board/revision and firmware commit. CI compilation alone must not populate performance tables.

## First target test matrix

After the build gate passes, run identical Pod/Field firmware cells at 48 kHz for block sizes 4, 16, 32 and 48 where the board/toolchain supports them. At minimum compare:

- linear and cubic Lagrange interpolation;
- static delay time;
- continuously moving delay time;
- high feedback near the harness clamp;
- simultaneous control movement;
- effect versus bypass-with-trails workload;
- long history in SDRAM;
- control/UI workload enabled versus minimized.

Record average, p99.9, max cycles, callback budget, overruns, run duration, memory placement, toolchain and exact source revisions. A 30-minute zero-overrun soak is a later campaign gate, not evidence produced by this build-only increment.
