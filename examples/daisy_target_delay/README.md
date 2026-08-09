# Daisy Pod / Field portable DIGI target harness

Status: `FIRMWARE_COMPOSITION_CANDIDATE`

This example is a bounded target adapter for the portable Pedal Harness DSP contract and the fractional-read DIGI candidate. It remains intentionally separate from the existing `Daisy_Pedal_Projects` single-`activeEffect` application runtime.

## Purpose

The harness now covers two distinct gates:

1. ARM/libDaisy target buildability for Pod and Field;
2. a minimal firmware-composition boundary that keeps audio, control/gesture and diagnostics work in explicit domains before physical profiling.

A successful build does **not** prove flash/run behavior, Cortex-M7 timing margin, audio quality, analog I/O performance, cache behavior under the final product graph, or production suitability.

## Frozen graph for this increment

```text
stereo codec input
    -> phh::DelayFirmwareRuntime
        -> phh::StaticSerialGraph<1>
            -> phh::DigitalDelayNode
    -> stereo codec output
```

- sample rate: 48 kHz;
- maximum delay: 2.0 s / 96,000 samples;
- one static graph node;
- one 1 MiB `StaticArena` backing store placed in `.sdram_bss`;
- no heap, logging, control scan, display, storage or blocking USB work in the audio callback;
- coherent parameter snapshots are transferred through a bounded lock-free mailbox;
- callback profiling is compile-time removable with `ENABLE_PROFILING=0`.

## Firmware service domains

### Audio callback

The callback performs only bounded work:

1. capture the DWT start cycle when profiling is enabled;
2. read at most two attempts of the coherent parameter snapshot;
3. resolve the requested effect/bypass state at the block boundary;
4. process the fixed one-node graph or direct dry bypass;
5. record one cycle sample when profiling is enabled.

No control scanning, gesture decoding, USB logging, statistics sorting, display rendering, storage, MIDI service or heap work is allowed here.

### Control and gesture domain

`ServiceControlDomain()` runs in the main loop. It:

- calls the board control scanner;
- evaluates switch rising edges;
- owns the small harness gesture state machine;
- maps board controls to the current DIGI parameter array;
- publishes one complete parameter snapshot;
- publishes the requested bypass state.

The control mapping is a bring-up harness, not the final product range contract.

### Diagnostics domain

`ServiceDiagnosticsDomain()` runs in the main loop. It freezes/sorts the 4096-sample DWT capture only after the callback has stopped writing that capture buffer and emits the summary through USB logging outside the callback.

### Tempo/MIDI, UI/display and storage

These domains are deliberately **inactive** in this increment. Their absence is part of the bounded graph, not evidence that they are unnecessary in the product. When introduced they remain main-loop or lower-priority services and must not migrate into the audio callback.

## Bypass, tails and reset behavior

`phh::DelayFirmwareRuntime` makes the policies explicit:

- **effect**: normal graph processing;
- **preserve-tails bypass**: new input injection is forced to zero, dry path is unity, the requested wet level remains audible, and feedback continues to decay the stored tail;
- **flush bypass**: new input and feedback are forced to zero, output is dry-only, and the circular history is overwritten with zeros over one complete delay-buffer capacity; after that the runtime enters true dry bypass;
- **non-real-time reset**: `ResetNonRealtime()` clears the complete graph state immediately and is documented as safe only while audio is stopped.

The flush policy avoids an O(N) buffer clear inside the callback. A re-engage request received during flush does not expose stale history; flush completes first, then the latest requested state is honored.

The board harness exposes preserve-tails bypass only. Flush/reset remain host-tested runtime policies until a later target gesture/transition package intentionally maps them.

## Harness-only controls

These mappings are for bring-up and profiling. They are **not** accepted product control ranges.

| Board | Control | Harness mapping |
|---|---|---|
| Daisy Pod | knob 1 | delay time, 1 sample to 2 s |
| Daisy Pod | knob 2 | feedback, 0 to 0.95 |
| Daisy Pod | button 1 | toggle linear / cubic Lagrange interpolation |
| Daisy Pod | button 2 | toggle effect / preserve-tails bypass |
| Daisy Pod | MIX | fixed 0.5 dry / 0.5 wet |
| Daisy Field | knob 1 | delay time, 1 sample to 2 s |
| Daisy Field | knob 2 | feedback, 0 to 0.95 |
| Daisy Field | knob 3 | wet mix, 0 to 1 with complementary dry mix |
| Daisy Field | switch 1 | toggle linear / cubic Lagrange interpolation |
| Daisy Field | switch 2 | toggle effect / preserve-tails bypass |

`COLOR`, `MOTION`, and `MODE/PRESET/CONTEXT` remain intentionally absent from this first DIGI graph. Their final mappings remain `HOLD` until the corresponding algorithms and transition policies exist on target.

## Build

The application uses libDaisy's Cortex-M7 Makefile and linker flow. CI pins the libDaisy revision used for evidence; local builds should use the same pinned revision when reproducing a recorded result.

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
PHH_TARGET board=<...> fs=<...> block=<...> sysclk=<...> arena_used=<...> arena_capacity=<...> arena_region=<...> max_delay=<...> profile=<...>
PHH_PROFILE board=<...> samples=<...> avg=<...> p999=<...> max=<...> budget=<...> overruns=<...> snapshot_misses=<...> param_gen=<...> interp=<linear|cubic> audio_state=<...> flush_remaining=<...>
```

The profiler uses the Cortex-M7 DWT cycle counter and stores 4096 callback samples before sorting/summarizing them in the main loop. `budget` is calculated from the observed system clock, block size and sample rate.

These records become target evidence only after they have actually been captured from a named board/revision and firmware commit. CI compilation alone must not populate performance tables.

## First target test matrix

After the firmware-composition build passes, run identical Pod/Field firmware cells at 48 kHz for block sizes 4, 16, 32 and 48 where the board/toolchain supports them. At minimum compare:

- linear and cubic Lagrange interpolation;
- static delay time;
- continuously moving delay time;
- high feedback near the harness clamp;
- simultaneous control movement;
- preserve-tails bypass transitions;
- long history in SDRAM;
- control/UI workload enabled versus minimized when those services exist.

Record average, p99.9, max cycles, callback budget, overruns, run duration, memory placement, toolchain and exact source revisions. A 30-minute zero-overrun soak is a later campaign gate, not evidence produced by this build-only increment.
