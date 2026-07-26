# Fractional-read characterization — skeptical review resolution

## Status

PR #2 remains **draft** after revision. The interpolation kernel was retained; the measurement harness and target-facing reader API were reworked.

Technical head reviewed by CI: `5c8c87669a82021e0c50038e58f59d26a0b6c73f`.

## Finding resolution

| Finding | Resolution |
|---|---|
| CSV field-count mismatch | Replaced by a fixed nine-column schema and a Python validator that rejects malformed rows, unknown metrics/units, missing required fields and non-finite values. |
| Absolute `float` position loses fractional precision | Replaced by `Read(std::ptrdiff_t base, float fraction, policy)`. Integer wrapping and fractional normalization are separate; no accumulating absolute binary32 position is accepted. |
| Modulation metric measured intended PM | Added ideal-reference output, upper/lower sidebands for orders 1–4, deviation from ideal, asymmetry, residual RMS and a local residual-spur scan. Carrier/modulation are integer DFT bins by construction. |
| “Recirculation” was analytical | Renamed to `predicted_static_cascade_loss_db`; added `predicted_feedback_cascade_loss_db` with an explicit feedback-gain field and pass count. |
| Analytical helper could self-certify | Added an independent complex-gain measurement through `CircularFractionalReader`; magnitude and phase errors are emitted and directly tested. |
| Tests established only relative superiority | Added direct cubic basis values at mu=0.5, endpoint tests, analytical magnitude/phase checks, explicit boundary expectations, multiple-period wrapping, fraction carry/borrow, NaN/+Inf/-Inf, absolute operating-band limits and extreme integer-base coverage. Near-Nyquist degradation is explicitly asserted. |
| CPU benchmark weak and mislabeled | Renamed to host full-reader timing, added warm-up, 11 alternating trials, median, IQR, ratio and a metadata sidecar with compiler, flags, architecture, OS, runner image, build type and commit. |
| “Deterministic” comment included timing | CMake now distinguishes deterministic static metrics from observational CPU rows. |

## Artifact schema

```text
metric,policy,frequency_norm,fraction,mod_depth,passes,feedback_gain,value,unit
```

Validation command:

```bash
python3 tests/validate_fractional_delay_csv.py fractional-delay-characterization.csv
```

## Current Linux artifact observations

These are host observations from the uploaded GitHub Actions artifact, not embedded-target claims.

- first modulation sideband ideal: about `-14.917 dBc`;
- linear first-sideband deviation: about `+0.326 dB`;
- cubic first-sideband deviation: about `+0.038 dB`;
- modulation residual RMS:
  - linear: about `0.0355 FS rms`;
  - cubic: about `0.00388 FS rms`;
- full-reader host timing remains observational only; Cortex-M7 cost is still unmeasured.

## Remaining limitations

- no Cortex-M7/DWT cycle measurement;
- no SRAM-vs-SDRAM comparison;
- no variable-delay transition policy;
- the residual-spur metric scans a local band around the carrier, not full-band SFDR;
- very high-order sideband asymmetry becomes noise-sensitive when both sidebands approach numerical residue;
- no tape, reverse or freeze work is included.

## Gate

All maintained Linux, macOS and Windows builds/tests pass at the stated technical head. The dedicated contract workflow passes, including artifact validation and upload. The known full legacy aggregate remains non-blocking and visible.

**Recommendation:** keep PR #2 draft for one independent re-review; do not merge solely because CI is green.
