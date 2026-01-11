"""Pytest integration for validation infrastructure.

Provides fixtures, markers, and test utilities for testing
validation rules and validators.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Iterator, TypeVar

import pytest

from validation_infrastructure.core.base import BaseValidator, ValidationResult


T = TypeVar("T")


# ============================================================================
# Fixtures
# ============================================================================

@pytest.fixture
def validation_context() -> dict[str, Any]:
    """Provide a fresh validation context for each test."""
    return {"test_mode": True, "strict": True}


@pytest.fixture
def temp_config_file(tmp_path: Path) -> Callable[[dict[str, Any], str], Path]:
    """Factory fixture to create temporary config files.
    
    Usage:
        def test_config(temp_config_file):
            config_path = temp_config_file({"key": "value"}, "config.json")
            # Use config_path in test...
    """
    def _create_config(data: dict[str, Any], filename: str = "config.json") -> Path:
        file_path = tmp_path / filename
        
        if filename.endswith(".json"):
            file_path.write_text(json.dumps(data, indent=2))
        elif filename.endswith(".yaml") or filename.endswith(".yml"):
            import yaml
            file_path.write_text(yaml.safe_dump(data))
        elif filename.endswith(".toml"):
            import toml
            file_path.write_text(toml.dumps(data))
        else:
            file_path.write_text(str(data))
        
        return file_path
    
    return _create_config


@pytest.fixture
def sample_valid_data() -> dict[str, Any]:
    """Sample valid data for common validation tests."""
    return {
        "name": "John Doe",
        "email": "john.doe@example.com",
        "age": 30,
        "is_active": True,
        "tags": ["user", "admin"],
        "address": {
            "street": "123 Main St",
            "city": "Anytown",
            "zip": "12345",
        },
    }


@pytest.fixture
def sample_invalid_data() -> dict[str, Any]:
    """Sample invalid data for testing validation failures."""
    return {
        "name": "",  # Too short
        "email": "invalid-email",  # Invalid format
        "age": -5,  # Negative
        "is_active": "yes",  # Wrong type
        "tags": "not-a-list",  # Wrong type
    }


# ============================================================================
# Markers
# ============================================================================

def pytest_configure(config: pytest.Config) -> None:
    """Register custom markers."""
    config.addinivalue_line(
        "markers",
        "validation: mark test as a validation test",
    )
    config.addinivalue_line(
        "markers",
        "slow_validation: mark test as slow validation (requires real I/O)",
    )
    config.addinivalue_line(
        "markers",
        "async_validation: mark test as async validation test",
    )


# ============================================================================
# Assertion Helpers
# ============================================================================

@dataclass
class ValidationAssertions:
    """Helper class for validation assertions.
    
    Usage:
        def test_validator(validation_assertions):
            result = validator.validate(data)
            validation_assertions.assert_valid(result)
    """
    
    def assert_valid(self, result: ValidationResult) -> None:
        """Assert validation passed."""
        assert result.is_valid, (
            f"Expected validation to pass, but got errors: {result.errors}"
        )
    
    def assert_invalid(
        self,
        result: ValidationResult,
        expected_error_count: int | None = None,
    ) -> None:
        """Assert validation failed."""
        assert not result.is_valid, "Expected validation to fail, but it passed"
        
        if expected_error_count is not None:
            assert len(result.errors) == expected_error_count, (
                f"Expected {expected_error_count} errors, got {len(result.errors)}"
            )
    
    def assert_error_contains(
        self,
        result: ValidationResult,
        substring: str,
    ) -> None:
        """Assert that at least one error contains the given substring."""
        error_messages = [str(e) for e in result.errors]
        assert any(substring.lower() in msg.lower() for msg in error_messages), (
            f"Expected error containing '{substring}', got: {error_messages}"
        )
    
    def assert_field_error(
        self,
        result: ValidationResult,
        field_name: str,
    ) -> None:
        """Assert that there's an error for the specified field."""
        error_fields = [getattr(e, 'field', None) for e in result.errors]
        assert field_name in error_fields, (
            f"Expected error for field '{field_name}', got errors for: {error_fields}"
        )


@pytest.fixture
def validation_assertions() -> ValidationAssertions:
    """Provide validation assertion helpers."""
    return ValidationAssertions()


# ============================================================================
# Parameterized Test Helpers
# ============================================================================

@dataclass
class ValidationTestCase:
    """Represents a single validation test case.
    
    Attributes:
        id: Unique identifier for the test case.
        input_data: Input data to validate.
        expected_valid: Whether validation should pass.
        expected_errors: Expected error messages (substrings).
        description: Human-readable description.
    """
    id: str
    input_data: Any
    expected_valid: bool
    expected_errors: list[str] = field(default_factory=list)
    description: str = ""
    
    def __repr__(self) -> str:
        return f"TestCase({self.id})"


def validation_test_cases(
    cases: list[ValidationTestCase],
) -> pytest.MarkDecorator:
    """Create parameterized test cases for validation.
    
    Usage:
        @validation_test_cases([
            ValidationTestCase("valid_email", "test@example.com", True),
            ValidationTestCase("invalid_email", "invalid", False, ["email"]),
        ])
        def test_email_validation(case, validator, validation_assertions):
            result = validator.validate(case.input_data)
            if case.expected_valid:
                validation_assertions.assert_valid(result)
            else:
                validation_assertions.assert_invalid(result)
    """
    return pytest.mark.parametrize(
        "case",
        cases,
        ids=[c.id for c in cases],
    )


# ============================================================================
# Mock Validators
# ============================================================================

class MockValidator(BaseValidator[T]):
    """Mock validator for testing.
    
    Configurable to always pass, always fail, or use custom logic.
    """
    
    def __init__(
        self,
        should_pass: bool = True,
        error_message: str = "Mock validation error",
        validate_fn: Callable[[T], bool] | None = None,
    ) -> None:
        self.should_pass = should_pass
        self.error_message = error_message
        self.validate_fn = validate_fn
        self.call_count = 0
        self.last_value: T | None = None
    
    def validate(self, value: T, context: dict[str, Any] | None = None) -> ValidationResult:
        self.call_count += 1
        self.last_value = value
        
        if self.validate_fn:
            is_valid = self.validate_fn(value)
        else:
            is_valid = self.should_pass
        
        if is_valid:
            return ValidationResult(is_valid=True)
        else:
            return ValidationResult(
                is_valid=False,
                errors=[self.error_message],
            )
    
    def reset(self) -> None:
        """Reset call tracking."""
        self.call_count = 0
        self.last_value = None


@pytest.fixture
def mock_validator() -> Callable[..., MockValidator[Any]]:
    """Factory fixture for creating mock validators."""
    def _create_mock(**kwargs: Any) -> MockValidator[Any]:
        return MockValidator(**kwargs)
    return _create_mock


# ============================================================================
# Async Test Utilities
# ============================================================================

@pytest.fixture
def anyio_backend() -> str:
    """Configure anyio backend for async tests."""
    return "asyncio"


# ============================================================================
# Performance Test Utilities
# ============================================================================

@dataclass
class ValidationBenchmark:
    """Benchmark results for validation performance tests."""
    
    iterations: int
    total_time_ms: float
    avg_time_ms: float
    min_time_ms: float
    max_time_ms: float
    throughput_per_sec: float
    
    def assert_within(self, max_avg_ms: float) -> None:
        """Assert average time is within threshold."""
        assert self.avg_time_ms <= max_avg_ms, (
            f"Validation too slow: {self.avg_time_ms:.3f}ms > {max_avg_ms}ms"
        )


@pytest.fixture
def benchmark_validator() -> Callable[..., ValidationBenchmark]:
    """Fixture for benchmarking validators.
    
    Usage:
        def test_performance(benchmark_validator, my_validator):
            result = benchmark_validator(
                my_validator.validate,
                test_data,
                iterations=1000,
            )
            result.assert_within(max_avg_ms=1.0)
    """
    import time
    
    def _benchmark(
        validate_fn: Callable[[Any], ValidationResult],
        data: Any,
        iterations: int = 100,
    ) -> ValidationBenchmark:
        times: list[float] = []
        
        for _ in range(iterations):
            start = time.perf_counter()
            validate_fn(data)
            elapsed = (time.perf_counter() - start) * 1000  # ms
            times.append(elapsed)
        
        total = sum(times)
        return ValidationBenchmark(
            iterations=iterations,
            total_time_ms=total,
            avg_time_ms=total / iterations,
            min_time_ms=min(times),
            max_time_ms=max(times),
            throughput_per_sec=iterations / (total / 1000),
        )
    
    return _benchmark
