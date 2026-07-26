#!/usr/bin/env python3
import csv
import math
import re
import sys
from pathlib import Path

HEADER = [
    "metric",
    "policy",
    "frequency_norm",
    "fraction",
    "mod_depth",
    "passes",
    "feedback_gain",
    "value",
    "unit",
]

FIXED_UNITS = {
    "static_rms_error": "FS_rms",
    "measured_gain_magnitude_error": "absolute",
    "measured_gain_phase_error": "rad",
    "predicted_static_cascade_loss_db": "dB",
    "predicted_feedback_cascade_loss_db": "dB",
    "mod_residual_rms": "FS_rms",
    "mod_residual_max_local_spur": "dBc",
    "host_full_reader_time_median": "ns_per_call",
    "host_full_reader_time_iqr": "ns_per_call",
    "host_full_reader_ratio": "ratio",
    "boundary_expected_error": "FS_abs",
    "boundary_period_error": "FS_abs",
}

MOD_ACTUAL_IDEAL = re.compile(
    r"mod_(actual|ideal)_(upper|lower)_k[1-4]_db$"
)
MOD_DEVIATION = re.compile(
    r"mod_deviation_(upper|lower)_k[1-4]_db$"
)
MOD_ASYMMETRY = re.compile(r"mod_asymmetry_k[1-4]_db$")


def fail(message: str) -> None:
    raise ValueError(message)


def parse_optional_float(row: dict[str, str], field: str, line: int):
    text = row[field].strip()
    if not text:
        return None
    try:
        value = float(text)
    except ValueError:
        fail(f"line {line}: {field} is not numeric: {text!r}")
    if not math.isfinite(value):
        fail(f"line {line}: {field} is non-finite: {text!r}")
    return value


def expected_unit(metric: str) -> str:
    if metric in FIXED_UNITS:
        return FIXED_UNITS[metric]
    if MOD_ACTUAL_IDEAL.fullmatch(metric):
        return "dBc"
    if MOD_DEVIATION.fullmatch(metric) or MOD_ASYMMETRY.fullmatch(metric):
        return "dB"
    fail(f"unknown metric: {metric!r}")


def require_present(row: dict[str, str],
                    fields: tuple[str, ...],
                    line: int) -> None:
    for field in fields:
        if not row[field].strip():
            fail(f"line {line}: {row['metric']} requires {field}")


def validate(path: Path) -> None:
    with path.open(newline="", encoding="utf-8") as handle:
        reader = csv.DictReader(handle)
        if reader.fieldnames != HEADER:
            fail(f"header mismatch: expected {HEADER}, got {reader.fieldnames}")
        rows = list(reader)

    if not rows:
        fail("artifact contains no measurement rows")

    seen_metrics: set[str] = set()
    static_policies: set[str] = set()

    for line, row in enumerate(rows, start=2):
        if None in row:
            fail(f"line {line}: extra fields detected: {row[None]}")
        if any(row[field] is None for field in HEADER):
            fail(f"line {line}: missing field")

        metric = row["metric"].strip()
        policy = row["policy"].strip()
        unit = row["unit"].strip()
        if policy not in {"linear", "cubic_lagrange", "comparison"}:
            fail(f"line {line}: unknown policy {policy!r}")

        required_unit = expected_unit(metric)
        if unit != required_unit:
            fail(
                f"line {line}: metric {metric!r} requires unit "
                f"{required_unit!r}, got {unit!r}"
            )

        for field in (
            "frequency_norm",
            "fraction",
            "mod_depth",
            "feedback_gain",
            "value",
        ):
            value = parse_optional_float(row, field, line)
            if field == "value" and value is None:
                fail(f"line {line}: value is required")

        passes = row["passes"].strip()
        if passes:
            try:
                parsed_passes = int(passes)
            except ValueError:
                fail(f"line {line}: passes is not an integer: {passes!r}")
            if parsed_passes < 0:
                fail(f"line {line}: passes must be non-negative")

        if metric in {
            "static_rms_error",
            "measured_gain_magnitude_error",
            "measured_gain_phase_error",
        }:
            require_present(row, ("frequency_norm", "fraction"), line)
        elif metric in {
            "predicted_static_cascade_loss_db",
            "predicted_feedback_cascade_loss_db",
        }:
            require_present(
                row,
                ("frequency_norm", "fraction", "passes", "feedback_gain"),
                line,
            )
        elif metric.startswith("mod_"):
            require_present(row, ("frequency_norm", "mod_depth"), line)
        elif metric.startswith("host_full_reader_"):
            if metric == "host_full_reader_ratio" and policy != "comparison":
                fail(f"line {line}: host ratio must use comparison policy")
        elif metric.startswith("boundary_"):
            require_present(row, ("fraction",), line)

        if metric != "host_full_reader_ratio" and policy == "comparison":
            fail(f"line {line}: comparison policy is only valid for host ratio")

        seen_metrics.add(metric)
        if metric == "static_rms_error":
            static_policies.add(policy)

    required = {
        "static_rms_error",
        "measured_gain_magnitude_error",
        "measured_gain_phase_error",
        "predicted_static_cascade_loss_db",
        "predicted_feedback_cascade_loss_db",
        "mod_residual_rms",
        "mod_residual_max_local_spur",
        "host_full_reader_time_median",
        "host_full_reader_time_iqr",
        "host_full_reader_ratio",
        "boundary_expected_error",
        "boundary_period_error",
    }
    missing = required - seen_metrics
    if missing:
        fail(f"required metrics missing: {sorted(missing)}")
    if static_policies != {"linear", "cubic_lagrange"}:
        fail(f"static comparison is incomplete: {sorted(static_policies)}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(
            "usage: validate_fractional_delay_csv.py ARTIFACT.csv",
            file=sys.stderr,
        )
        raise SystemExit(2)
    try:
        validate(Path(sys.argv[1]))
    except (OSError, ValueError) as exc:
        print(
            f"characterization artifact validation failed: {exc}",
            file=sys.stderr,
        )
        raise SystemExit(1)
    print("characterization artifact validation passed")
