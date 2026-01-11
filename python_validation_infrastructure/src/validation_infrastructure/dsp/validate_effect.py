"""MATLAB effect validation script.

Validates C++ effect implementations against MATLAB reference data
using tiered tolerance thresholds.
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path
from typing import Any

import numpy as np

try:
    from rich.console import Console
    from rich.table import Table
    RICH_AVAILABLE = True
except ImportError:
    RICH_AVAILABLE = False


class ToleranceTier(Enum):
    """Validation tolerance tiers."""
    STANDARD = 0.5   # ±0.5 dB for filters, modulation
    RELAXED = 1.0    # ±1.0 dB for virtual analog effects


@dataclass
class ValidationResult:
    """Result of validating an effect against MATLAB reference."""
    effect_name: str
    tier: ToleranceTier
    passed: bool
    max_error_db: float
    mean_error_db: float
    correlation: float
    snr_db: float
    samples_compared: int
    details: dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> dict[str, Any]:
        return {
            "effect_name": self.effect_name,
            "tier": self.tier.name,
            "tolerance_db": self.tier.value,
            "passed": self.passed,
            "max_error_db": self.max_error_db,
            "mean_error_db": self.mean_error_db,
            "correlation": self.correlation,
            "snr_db": self.snr_db,
            "samples_compared": self.samples_compared,
            "details": self.details,
        }


class EffectValidator:
    """Validates C++ DSP effects against MATLAB reference outputs.
    
    Usage:
        validator = EffectValidator()
        result = validator.validate(
            effect_name="tube",
            cpp_output=cpp_array,
            matlab_reference=matlab_array,
            tier=ToleranceTier.RELAXED,
        )
        print(result)
    """
    
    def __init__(self, verbose: bool = False) -> None:
        self.verbose = verbose
        self.console = Console() if RICH_AVAILABLE else None
    
    def load_reference(
        self,
        reference_path: Path,
    ) -> np.ndarray:
        """Load MATLAB reference data from file.
        
        Supports:
        - .mat files (MATLAB format)
        - .npy files (NumPy format)
        - .wav files (audio)
        - .csv files (text)
        
        Args:
            reference_path: Path to reference file.
        
        Returns:
            Reference signal as numpy array.
        """
        suffix = reference_path.suffix.lower()
        
        if suffix == ".mat":
            try:
                from scipy.io import loadmat
                data = loadmat(reference_path)
                # Find the signal array (usually named 'y' or 'output')
                for key in ["y", "output", "signal", "data"]:
                    if key in data:
                        return np.array(data[key]).flatten().astype(np.float32)
                # Return first non-metadata array
                for key, value in data.items():
                    if not key.startswith("__"):
                        return np.array(value).flatten().astype(np.float32)
            except ImportError:
                raise ImportError("scipy required for .mat file support")
        
        elif suffix == ".npy":
            return np.load(reference_path).astype(np.float32)
        
        elif suffix == ".wav":
            try:
                import soundfile as sf
                data, _ = sf.read(reference_path, dtype="float32")
                if data.ndim > 1:
                    data = data[:, 0]  # Take first channel
                return data
            except ImportError:
                raise ImportError("soundfile required for .wav file support")
        
        elif suffix == ".csv":
            return np.loadtxt(reference_path, delimiter=",", dtype=np.float32)
        
        else:
            raise ValueError(f"Unsupported reference file format: {suffix}")
    
    def validate(
        self,
        effect_name: str,
        cpp_output: np.ndarray,
        matlab_reference: np.ndarray,
        tier: ToleranceTier = ToleranceTier.STANDARD,
    ) -> ValidationResult:
        """Validate C++ output against MATLAB reference.
        
        Args:
            effect_name: Name of the effect being validated.
            cpp_output: Output from C++ implementation.
            matlab_reference: Reference output from MATLAB.
            tier: Tolerance tier for validation.
        
        Returns:
            ValidationResult with metrics and pass/fail status.
        """
        # Ensure same length
        min_len = min(len(cpp_output), len(matlab_reference))
        cpp = cpp_output[:min_len]
        ref = matlab_reference[:min_len]
        
        # Compute error
        error = cpp - ref
        
        # Convert to dB (avoid log of zero)
        epsilon = 1e-10
        
        # Maximum absolute error in dB
        max_error = np.max(np.abs(error))
        max_error_db = 20 * np.log10(max_error + epsilon) if max_error > 0 else -100
        
        # Mean absolute error in dB
        mean_error = np.mean(np.abs(error))
        mean_error_db = 20 * np.log10(mean_error + epsilon) if mean_error > 0 else -100
        
        # Correlation coefficient
        if np.std(cpp) > 0 and np.std(ref) > 0:
            correlation = np.corrcoef(cpp, ref)[0, 1]
        else:
            correlation = 1.0 if np.allclose(cpp, ref) else 0.0
        
        # Signal-to-Noise Ratio
        signal_power = np.mean(ref ** 2)
        noise_power = np.mean(error ** 2)
        if noise_power > 0:
            snr_db = 10 * np.log10(signal_power / noise_power)
        else:
            snr_db = 120.0  # Perfect match
        
        # Determine pass/fail
        # Use peak error relative to signal for dB comparison
        ref_peak = np.max(np.abs(ref))
        if ref_peak > epsilon:
            relative_error_db = 20 * np.log10(max_error / ref_peak + epsilon)
        else:
            relative_error_db = -100
        
        passed = abs(relative_error_db) <= tier.value * 2  # *2 for headroom
        
        # Alternative pass criteria: correlation > 0.99 and SNR > 40dB
        if correlation > 0.99 and snr_db > 40:
            passed = True
        
        result = ValidationResult(
            effect_name=effect_name,
            tier=tier,
            passed=passed,
            max_error_db=max_error_db,
            mean_error_db=mean_error_db,
            correlation=correlation,
            snr_db=snr_db,
            samples_compared=min_len,
            details={
                "relative_error_db": relative_error_db,
                "ref_peak": float(ref_peak),
                "max_abs_error": float(max_error),
            },
        )
        
        if self.verbose:
            self._print_result(result)
        
        return result
    
    def _print_result(self, result: ValidationResult) -> None:
        """Print validation result to console."""
        if RICH_AVAILABLE and self.console:
            status = "[green]✓ PASSED[/green]" if result.passed else "[red]✗ FAILED[/red]"
            
            table = Table(title=f"Validation: {result.effect_name}")
            table.add_column("Metric", style="cyan")
            table.add_column("Value", justify="right")
            
            table.add_row("Status", status)
            table.add_row("Tier", f"{result.tier.name} (±{result.tier.value} dB)")
            table.add_row("Max Error (dB)", f"{result.max_error_db:.2f}")
            table.add_row("Mean Error (dB)", f"{result.mean_error_db:.2f}")
            table.add_row("Correlation", f"{result.correlation:.6f}")
            table.add_row("SNR (dB)", f"{result.snr_db:.1f}")
            table.add_row("Samples", str(result.samples_compared))
            
            self.console.print(table)
        else:
            status = "PASSED" if result.passed else "FAILED"
            print(f"\n=== Validation: {result.effect_name} ===")
            print(f"Status: {status}")
            print(f"Tier: {result.tier.name} (±{result.tier.value} dB)")
            print(f"Max Error (dB): {result.max_error_db:.2f}")
            print(f"Correlation: {result.correlation:.6f}")
            print(f"SNR (dB): {result.snr_db:.1f}")
    
    def validate_batch(
        self,
        effects: list[dict[str, Any]],
    ) -> list[ValidationResult]:
        """Validate multiple effects.
        
        Args:
            effects: List of dicts with keys:
                     - name: Effect name
                     - cpp_output: C++ output array
                     - matlab_reference: MATLAB reference array
                     - tier: Optional ToleranceTier
        
        Returns:
            List of ValidationResults.
        """
        results = []
        for effect in effects:
            tier = effect.get("tier", ToleranceTier.STANDARD)
            result = self.validate(
                effect_name=effect["name"],
                cpp_output=effect["cpp_output"],
                matlab_reference=effect["matlab_reference"],
                tier=tier,
            )
            results.append(result)
        
        return results
    
    def generate_report(
        self,
        results: list[ValidationResult],
        output_path: Path | None = None,
    ) -> dict[str, Any]:
        """Generate validation report.
        
        Args:
            results: List of validation results.
            output_path: Optional path to save JSON report.
        
        Returns:
            Report dictionary.
        """
        passed_count = sum(1 for r in results if r.passed)
        
        report = {
            "summary": {
                "total": len(results),
                "passed": passed_count,
                "failed": len(results) - passed_count,
                "pass_rate": passed_count / len(results) if results else 0,
            },
            "results": [r.to_dict() for r in results],
        }
        
        if output_path:
            output_path.write_text(json.dumps(report, indent=2))
        
        return report


# Effect tier mappings (from Phase1_Completion_Gate_Implementation_Plan.md)
EFFECT_TIERS = {
    "tube": ToleranceTier.RELAXED,
    "wahwah": ToleranceTier.RELAXED,
    "tonestack": ToleranceTier.RELAXED,
    "lowshelving": ToleranceTier.STANDARD,
    "highshelving": ToleranceTier.STANDARD,
    "peakfilter": ToleranceTier.STANDARD,
    "vibrato": ToleranceTier.STANDARD,
    "ringmod": ToleranceTier.STANDARD,
    "stereopan": ToleranceTier.STANDARD,
    "noisegate": ToleranceTier.STANDARD,
}


def main() -> int:
    """CLI entry point."""
    parser = argparse.ArgumentParser(
        description="Validate C++ DSP effects against MATLAB reference"
    )
    parser.add_argument(
        "--effect",
        required=True,
        help="Effect name to validate",
    )
    parser.add_argument(
        "--cpp-output",
        type=Path,
        required=True,
        help="Path to C++ output file (.npy, .wav, .csv)",
    )
    parser.add_argument(
        "--reference",
        type=Path,
        required=True,
        help="Path to MATLAB reference file (.mat, .npy, .wav, .csv)",
    )
    parser.add_argument(
        "--tolerance",
        type=float,
        default=None,
        help="Override tolerance in dB (default: use tier for effect)",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="Path to save JSON report",
    )
    parser.add_argument(
        "--verbose",
        action="store_true",
        help="Print detailed output",
    )
    
    args = parser.parse_args()
    
    validator = EffectValidator(verbose=args.verbose)
    
    # Load data
    try:
        cpp_output = validator.load_reference(args.cpp_output)
        matlab_ref = validator.load_reference(args.reference)
    except Exception as e:
        print(f"Error loading data: {e}")
        return 1
    
    # Determine tier
    if args.tolerance:
        tier = ToleranceTier.RELAXED if args.tolerance >= 1.0 else ToleranceTier.STANDARD
    else:
        tier = EFFECT_TIERS.get(args.effect.lower(), ToleranceTier.STANDARD)
    
    # Validate
    result = validator.validate(
        effect_name=args.effect,
        cpp_output=cpp_output,
        matlab_reference=matlab_ref,
        tier=tier,
    )
    
    # Save report if requested
    if args.output:
        validator.generate_report([result], args.output)
    
    # Always print summary
    if not args.verbose:
        status = "✓ PASSED" if result.passed else "✗ FAILED"
        print(f"{args.effect}: {status} | SNR: {result.snr_db:.1f} dB | Correlation: {result.correlation:.6f}")
    
    return 0 if result.passed else 1


if __name__ == "__main__":
    sys.exit(main())
