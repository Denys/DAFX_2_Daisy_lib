# Daisy Pod / Field portable DIGI target harness

Status: `TARGET_HARNESS_CANDIDATE`

This example is a bounded target adapter for the portable Pedal Harness DSP contract and the fractional-read DIGI candidate. It is intentionally separate from the existing `Daisy_Pedal_Projects` single-`activeEffect` application runtime.

## Purpose

The harness exists to answer target-specific questions that host tests cannot answer:

- does the portable stereo DIGI node compile for Cortex-M7 through libDaisy;
- what flash/internal-memory/SDRAM image does the linker produce;
- what is the callback cost for linear versus cubic Lagrange fractional reads;
- how do block size and control workload affect callback margin;
- can Pod and Field exercise the same DSP graph through thin board adapters.

A successful build does **not** prove flash/run behavior, audio quality, analog I/O performance, cache behavior under the final product graph, or production suitability.

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
- no heap, logging, control scan, display, storage, or blocking USB work in the audio callback;
- coherent parameter snapshots are transferred through a bounded lock-free mailbox;
- callback profiling is compile-time removable with `ENABLE_PROFILING=0`.

## Harness-only controls

These mappings are for bring-up and profiling. They are **not** accepted product control ranges.

| Board | Control | Harness mapping |
|---|---|---|
| Daisy Pod | knob 1 | delay time, 1 sample to 2 s |
| Daisy Pod | knob 2 | feedback, 0 to 0.95 |
| Daisy Pod | button 1 | toggle linear / cubic Lagrange interpolation |
| Daisy Pod | MIX | fixed 0.5 dry / 0.5 wet |
| Daisy Field | knob 1 | delay time, 1 sample to 2 s |
| Daisy Field | knob 2 | feedback, 0 to 0.95 |
| Daisy Field | knob 3 | wet mix, 0 to 1 with complementary dry mix |
| Daisy Field | switch 1 | toggle linear / cubic Lagrange interpolation |

`COLOR`, `MOTION`, and `MODE/PRESET/CONTEXT` are intentionally absent from this first DIGI target graph. Their final mappings remain `HOLD` until the corresponding algorithms and transition policies exist on target.

## Build

The application uses libDaisy's Cortex-M7 Makefile and linker flow. The repository CI pins the libDaisy revision used for evidence; local builds should use the same pinned revision when reproducing a recorded result.

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
PHH_PROFILE board=<...> samples=<...> avg=<...> p999=<...> max=<...> budget=<...> overruns=<...> snapshot_misses=<...> interp=<linear|cubic>
```

The profiler uses the Cortex-M7 DWT cycle counter and stores 4096 callback samples before sorting/summarizing them in the main loop. `budget` is calculated from the observed system clock, block size, and sample rate.

These records are target evidence only after they have actually been captured from a named board/revision and firmware commit. CI compilation alone must not populate performance tables.

## First target test matrix

After the baseline build passes, run identical Pod/Field firmware cells at 48 kHz for block sizes 4, 16, 32, and 48 where the board/toolchain supports them. At minimum compare:

- linear and cubic Lagrange interpolation;
- static delay time;
- continuously moving delay time;
- high feedback near the harness clamp;
- simultaneous control movement;
- long history in SDRAM;
- control/UI workload enabled versus minimized.

Record average, p99.9, max cycles, callback budget, overruns, run duration, memory placement, toolchain and exact source revisions. A 30-minute zero-overrun soak is a later campaign gate, not evidence produced by this baseline harness build.
