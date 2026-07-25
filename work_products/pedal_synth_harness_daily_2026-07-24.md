# Pedal & Synth Harness — Daily engineering run
## 2026-07-24

## Executive summary

**WP-01 now has an executable green acceptance path.**

The portable DSP contract and fixed-capacity serial graph successfully configure, build, and pass their dedicated Linux GitHub Actions test. The repository-wide `Build and Test` workflow remains red because the wider legacy aggregate build still contains unrelated debt.

The useful result of this run was separating:

1. a defect caused by a tracked local CMake cache;
2. failures in the repository-wide legacy build;
3. the actual validity of the new Pedal & Synth Harness contract.

## 1. State and changes

### Previous state

- Draft PR #1 existed for WP-01.
- Added `dsp_contract.hpp`, `static_serial_graph.hpp`, and `test_dsp_contract.cpp`.
- Initial CI failed during CMake configuration on Linux, macOS, and Windows.
- No executable evidence existed for the new contract.

### Changes made today

1. Found that `build/CMakeCache.txt` is tracked and was generated from `C:/Users/denko/Claude/DAFX_2_Daisy_lib/build`, including local MSVC paths.
2. Changed the general workflow to use a clean `ci-build/` directory.
3. Added CMake build directories to `.gitignore`.
4. Updated GoogleTest to `v1.17.0`, aligned with the existing C++17/CMake 3.16 baseline.
5. Added the dedicated target `pedal_harness_tests`.
6. Added `.github/workflows/pedal-harness-contract.yml`.
7. Ran the scoped workflow successfully: configure PASS, build PASS, tests PASS.

### Current GitHub state

- Repository: `Denys/DAFX_2_Daisy_lib`
- Branch: `feat/pedal-harness-wp01-contract`
- Draft PR: `#1 — fw: add WP-01 portable DSP contract and static graph`
- Scoped workflow: **PASS**
- General repository workflow: **FAIL**
- Merge: not performed

## 2. Main design decision

### ACCEPTED

Use a scoped executable acceptance gate for each new Synth Harness work package while keeping legacy repository failures visible.

```text
Legacy library state
        │
        ├── visible aggregate CI failures
        │
        └── new bounded work packages
                 │
                 ├── isolated target
                 ├── isolated tests
                 ├── explicit dependency boundary
                 └── green acceptance evidence
```

### REJECTED

- Disabling failing legacy tests merely to make the main badge green.
- Beginning Daisy target integration before the portable contract is executable.
- Adding arbitrary parallel routing before one real delay node validates the API.
- Starting Harness Lite PCB capture while the software contract is moving.

## 3. Technical feasibility

### Demonstrated

The dedicated CI proves that these compile and execute on a clean Linux runner:

- two serial gain nodes;
- preallocated graph scratch memory;
- empty-graph copy/bypass behavior;
- arena overflow rejection.

### Not demonstrated

- ARM/Daisy build;
- real-time cycle budget;
- heap instrumentation;
- mono/stereo/dual-mono layout enforcement;
- delay-buffer allocation;
- target profiling;
- hardware audio performance.

A host test is a contract check, not a tiny certificate of professional audio virtue.

## 4. BOM, cost, TTM, and commercial potential

| Item | Estimate |
|---|---:|
| Hardware components | 0 CHF |
| Direct infrastructure cost | Negligible |
| Portable DIGI node | 2–5 focused days |
| Daisy runtime adapter after DIGI host pass | additional 3–7 days |

Direct revenue from WP-01 is zero. Its indirect value is a common DSP module format, lower integration cost, host regression evidence, and a foundation for custom firmware, developer tooling, repair diagnostics, and automated tests.

## 5. Concrete artifacts

- portable DSP contract;
- static serial graph;
- GoogleTest contract suite;
- dedicated `pedal_harness_tests` target;
- dedicated `Pedal Harness Contract` workflow;
- clean CI build-directory policy;
- updated `.gitignore`.

### Acceptance command

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

## 6. Independent critical review

1. `StaticSerialGraph` allocates two stereo scratch buffers even for zero or one node.
2. `ChannelLayout` is declared but graph compatibility is not enforced.
3. Invalid frame counts return silently without fault accounting or safe-output policy.
4. Output diagnostics add predictable but nonzero `O(2N)` callback overhead.
5. Virtual dispatch remains unbenchmarked on target.
6. General repository CI is still red.
7. The tracked `build/` tree remains in Git history; `.gitignore` prevents new files but does not untrack existing ones.

## 7. Acceptance criteria

| Criterion | Result |
|---|---:|
| Clean CMake configure | PASS |
| Build `pedal_harness_tests` | PASS |
| Run dedicated CTest | PASS |
| No Daisy/libDaisy dependency | PASS by source boundary |
| No merge performed | PASS |
| General repository CI green | FAIL, outside scoped acceptance |
| ARM/Daisy build | NOT RUN |
| Bench/audio measurement | NOT APPLICABLE |

**WP-01: PASS WITH OPEN REPOSITORY DEBT**

## 8. Next action

**Implement one portable DIGI delay node against WP-01.**

Minimum scope:

- caller-owned mono/stereo delay memory;
- integer delay first, with fractional read policy reserved for the next increment;
- independent input injection and feedback gain;
- feedback low/high-cut hooks;
- finite and index guards;
- explicit reset/tail policy;
- no heap in `Process`.

Required host tests:

1. impulse delay timing;
2. circular-buffer wrap;
3. zero-feedback single repeat;
4. feedback decay;
5. feedback limit and finite-output behavior;
6. reset/tail behavior;
7. maximum block and buffer bounds.

Do not add tape, reverse, freeze, multi-tap, MIDI clock, UI, or Daisy BSP in the same increment. Those are excellent ways to ensure the first delay test is written shortly before the heat death of the universe.
