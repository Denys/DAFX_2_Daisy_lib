"""Validation utilities for comparing DSP outputs.

Provides tools for comparing Python DSP outputs against C++ implementations
or MATLAB reference data (golden data).
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray

from dafx_execution.core import get_logger


@dataclass
class ValidationResult:
    """Result of a validation comparison.
    
    Attributes:
        passed: Whether validation passed.
        max_error: Maximum absolute error.
        mean_error: Mean absolute error.
        rms_error: Root mean square error.
        snr_db: Signal-to-noise ratio in dB.
        details: Additional validation details.
    """
    passed: bool
    max_error: float = 0.0
    mean_error: float = 0.0
    rms_error: float = 0.0
    snr_db: float = float("inf")
    details: dict[str, Any] = field(default_factory=dict)
    
    def __str__(self) -> str:
        status = "✓ PASSED" if self.passed else "✗ FAILED"
        return (
            f"{status} | Max Error: {self.max_error:.6f} | "
            f"RMS Error: {self.rms_error:.6f} | SNR: {self.snr_db:.1f} dB"
        )


class Validator:
    """Base class for validation operations."""
    
    def __init__(self, tolerance: float = 1e-6) -> None:
        """Initialize validator.
        
        Args:
            tolerance: Maximum allowed error for passing validation.
        """
        self.tolerance = tolerance
        self._logger = get_logger(__name__)
    
    def validate(
        self,
        actual: NDArray[np.float32],
        expected: NDArray[np.float32],
    ) -> ValidationResult:
        """Validate actual output against expected.
        
        Args:
            actual: Actual output from processing.
            expected: Expected (reference) output.
        
        Returns:
            ValidationResult with error metrics.
        """
        raise NotImplementedError


class OutputComparator(Validator):
    """Compares DSP outputs with numerical error metrics.
    
    Computes various error metrics between two signals:
    - Maximum absolute error
    - Mean absolute error
    - Root mean square error
    - Signal-to-noise ratio
    """
    
    def __init__(
        self,
        tolerance: float = 1e-6,
        snr_threshold_db: float = 60.0,
    ) -> None:
        """Initialize comparator.
        
        Args:
            tolerance: Max allowed error for each sample.
            snr_threshold_db: Minimum SNR for passing (in dB).
        """
        super().__init__(tolerance)
        self.snr_threshold_db = snr_threshold_db
    
    def validate(
        self,
        actual: NDArray[np.float32],
        expected: NDArray[np.float32],
    ) -> ValidationResult:
        """Compare two signals.
        
        Args:
            actual: Actual output signal.
            expected: Expected (reference) signal.
        
        Returns:
            ValidationResult with comparison metrics.
        """
        # Ensure same shape
        if actual.shape != expected.shape:
            return ValidationResult(
                passed=False,
                details={"error": f"Shape mismatch: {actual.shape} vs {expected.shape}"},
            )
        
        # Compute error
        error = actual - expected
        
        # Metrics
        max_error = float(np.max(np.abs(error)))
        mean_error = float(np.mean(np.abs(error)))
        rms_error = float(np.sqrt(np.mean(error ** 2)))
        
        # SNR (signal-to-noise ratio)
        signal_power = float(np.mean(expected ** 2))
        noise_power = float(np.mean(error ** 2))
        
        if noise_power > 0:
            snr_db = 10.0 * np.log10(signal_power / noise_power)
        else:
            snr_db = float("inf")
        
        # Pass/fail decision
        passed = max_error <= self.tolerance and snr_db >= self.snr_threshold_db
        
        result = ValidationResult(
            passed=passed,
            max_error=max_error,
            mean_error=mean_error,
            rms_error=rms_error,
            snr_db=float(snr_db),
            details={
                "num_samples": len(actual),
                "tolerance": self.tolerance,
                "snr_threshold_db": self.snr_threshold_db,
            },
        )
        
        self._logger.info(
            "Validation complete",
            passed=passed,
            max_error=max_error,
            snr_db=snr_db,
        )
        
        return result


class GoldenDataValidator(Validator):
    """Validates against saved golden (reference) data.
    
    Golden data can be from MATLAB outputs or verified C++ outputs.
    """
    
    def __init__(
        self,
        golden_dir: Path,
        tolerance: float = 1e-5,
    ) -> None:
        """Initialize golden data validator.
        
        Args:
            golden_dir: Directory containing golden data files.
            tolerance: Maximum allowed error.
        """
        super().__init__(tolerance)
        self.golden_dir = golden_dir
        self._comparator = OutputComparator(tolerance=tolerance)
    
    def load_golden(self, name: str) -> NDArray[np.float32]:
        """Load golden data by name.
        
        Args:
            name: Name of the golden data file (without extension).
        
        Returns:
            Golden data as numpy array.
        
        Raises:
            FileNotFoundError: If golden data doesn't exist.
        """
        # Try different extensions
        for ext in [".npy", ".npz", ".csv", ".wav"]:
            path = self.golden_dir / f"{name}{ext}"
            if path.exists():
                if ext == ".npy":
                    return np.load(path).astype(np.float32)
                elif ext == ".npz":
                    data = np.load(path)
                    return data[data.files[0]].astype(np.float32)
                elif ext == ".csv":
                    return np.loadtxt(path, delimiter=",", dtype=np.float32)
                elif ext == ".wav":
                    import soundfile as sf
                    data, _ = sf.read(path, dtype="float32")
                    return data
        
        raise FileNotFoundError(f"No golden data found for '{name}' in {self.golden_dir}")
    
    def validate_against_golden(
        self,
        actual: NDArray[np.float32],
        golden_name: str,
    ) -> ValidationResult:
        """Validate against golden data file.
        
        Args:
            actual: Actual output to validate.
            golden_name: Name of golden data file.
        
        Returns:
            ValidationResult with comparison metrics.
        """
        try:
            expected = self.load_golden(golden_name)
            return self._comparator.validate(actual, expected)
        except FileNotFoundError as e:
            return ValidationResult(
                passed=False,
                details={"error": str(e)},
            )
    
    def validate(
        self,
        actual: NDArray[np.float32],
        expected: NDArray[np.float32],
    ) -> ValidationResult:
        """Validate actual against expected."""
        return self._comparator.validate(actual, expected)
    
    def save_golden(
        self,
        data: NDArray[np.float32],
        name: str,
    ) -> Path:
        """Save data as new golden reference.
        
        Args:
            data: Data to save as golden.
            name: Name for the golden data file.
        
        Returns:
            Path to saved file.
        """
        self.golden_dir.mkdir(parents=True, exist_ok=True)
        path = self.golden_dir / f"{name}.npy"
        np.save(path, data)
        self._logger.info("Saved golden data", path=str(path))
        return path
