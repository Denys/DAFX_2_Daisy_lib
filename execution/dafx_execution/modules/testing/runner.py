"""Test runner for DSP validation tests.

Provides a simple test framework for running validation tests
against effect implementations.
"""

from __future__ import annotations

import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Any, Callable

from rich.console import Console
from rich.table import Table

from dafx_execution.core import get_logger
from dafx_execution.modules.testing.validator import ValidationResult


class TestStatus(Enum):
    """Test execution status."""
    PENDING = auto()
    RUNNING = auto()
    PASSED = auto()
    FAILED = auto()
    SKIPPED = auto()
    ERROR = auto()


@dataclass
class TestResult:
    """Result of a single test execution.
    
    Attributes:
        name: Test name.
        status: Test status.
        duration_ms: Execution time in milliseconds.
        validation: Validation result if applicable.
        error: Error message if test failed with exception.
        details: Additional test details.
    """
    name: str
    status: TestStatus
    duration_ms: float = 0.0
    validation: ValidationResult | None = None
    error: str | None = None
    details: dict[str, Any] = field(default_factory=dict)
    
    @property
    def passed(self) -> bool:
        """Check if test passed."""
        return self.status == TestStatus.PASSED


class TestCase(ABC):
    """Base class for test cases.
    
    Subclass and implement run() to create custom tests.
    
    Example:
        >>> class MyTest(TestCase):
        ...     def run(self) -> ValidationResult:
        ...         # Test logic here
        ...         return ValidationResult(passed=True)
    """
    
    @property
    def name(self) -> str:
        """Test name for reporting."""
        return self.__class__.__name__
    
    def setup(self) -> None:
        """Setup before test runs. Override as needed."""
        pass
    
    @abstractmethod
    def run(self) -> ValidationResult:
        """Execute the test.
        
        Returns:
            ValidationResult with pass/fail status and metrics.
        """
        ...
    
    def teardown(self) -> None:
        """Cleanup after test runs. Override as needed."""
        pass


@dataclass
class TestSuite:
    """Collection of test cases.
    
    Groups related tests together for execution.
    """
    name: str
    tests: list[TestCase] = field(default_factory=list)
    
    def add(self, test: TestCase) -> "TestSuite":
        """Add a test to the suite.
        
        Args:
            test: Test case to add.
        
        Returns:
            Self for chaining.
        """
        self.tests.append(test)
        return self
    
    def __len__(self) -> int:
        return len(self.tests)


class TestRunner:
    """Runs tests and collects results.
    
    Provides test execution with timing, error handling,
    and result reporting.
    
    Example:
        >>> runner = TestRunner()
        >>> suite = TestSuite("Effects")
        >>> suite.add(TubeTest())
        >>> results = runner.run_suite(suite)
        >>> runner.print_summary()
    """
    
    def __init__(
        self,
        stop_on_failure: bool = False,
        verbose: bool = True,
    ) -> None:
        """Initialize test runner.
        
        Args:
            stop_on_failure: Stop execution on first failure.
            verbose: Print progress during execution.
        """
        self.stop_on_failure = stop_on_failure
        self.verbose = verbose
        self._results: list[TestResult] = []
        self._logger = get_logger(__name__)
        self._console = Console()
    
    @property
    def results(self) -> list[TestResult]:
        """Get all test results."""
        return self._results.copy()
    
    @property
    def passed_count(self) -> int:
        """Number of passed tests."""
        return sum(1 for r in self._results if r.passed)
    
    @property
    def failed_count(self) -> int:
        """Number of failed tests."""
        return sum(1 for r in self._results if not r.passed)
    
    @property
    def total_duration_ms(self) -> float:
        """Total test execution time in milliseconds."""
        return sum(r.duration_ms for r in self._results)
    
    def run_test(self, test: TestCase) -> TestResult:
        """Run a single test case.
        
        Args:
            test: Test to execute.
        
        Returns:
            TestResult with execution details.
        """
        if self.verbose:
            self._console.print(f"  Running: {test.name}...", end=" ")
        
        start_time = time.perf_counter()
        
        try:
            # Setup
            test.setup()
            
            # Run
            validation = test.run()
            
            # Determine status
            status = TestStatus.PASSED if validation.passed else TestStatus.FAILED
            
            result = TestResult(
                name=test.name,
                status=status,
                duration_ms=(time.perf_counter() - start_time) * 1000,
                validation=validation,
            )
        
        except Exception as e:
            result = TestResult(
                name=test.name,
                status=TestStatus.ERROR,
                duration_ms=(time.perf_counter() - start_time) * 1000,
                error=str(e),
            )
            self._logger.exception(f"Test error: {test.name}")
        
        finally:
            # Teardown
            try:
                test.teardown()
            except Exception as e:
                self._logger.warning(f"Teardown error in {test.name}: {e}")
        
        # Print result
        if self.verbose:
            if result.passed:
                self._console.print(f"[green]✓[/green] ({result.duration_ms:.1f}ms)")
            elif result.status == TestStatus.ERROR:
                self._console.print(f"[red]ERROR[/red]: {result.error}")
            else:
                self._console.print(f"[red]✗[/red] ({result.duration_ms:.1f}ms)")
        
        self._results.append(result)
        return result
    
    def run_suite(self, suite: TestSuite) -> list[TestResult]:
        """Run all tests in a suite.
        
        Args:
            suite: Test suite to execute.
        
        Returns:
            List of test results.
        """
        if self.verbose:
            self._console.print(f"\n[bold]Running suite: {suite.name}[/bold] ({len(suite)} tests)")
        
        suite_results: list[TestResult] = []
        
        for test in suite.tests:
            result = self.run_test(test)
            suite_results.append(result)
            
            if self.stop_on_failure and not result.passed:
                self._logger.warning("Stopping on failure")
                break
        
        return suite_results
    
    def print_summary(self) -> None:
        """Print test execution summary."""
        total = len(self._results)
        passed = self.passed_count
        failed = self.failed_count
        
        self._console.print("\n" + "=" * 60)
        self._console.print("[bold]Test Summary[/bold]")
        self._console.print("=" * 60)
        
        # Results table
        table = Table(show_header=True, header_style="bold cyan")
        table.add_column("Test", style="dim")
        table.add_column("Status")
        table.add_column("Duration", justify="right")
        table.add_column("Details")
        
        for result in self._results:
            if result.passed:
                status = "[green]PASSED[/green]"
            elif result.status == TestStatus.ERROR:
                status = "[red]ERROR[/red]"
            else:
                status = "[red]FAILED[/red]"
            
            details = ""
            if result.validation:
                details = f"Max Error: {result.validation.max_error:.6f}"
            elif result.error:
                details = result.error[:40]
            
            table.add_row(
                result.name,
                status,
                f"{result.duration_ms:.1f}ms",
                details,
            )
        
        self._console.print(table)
        
        # Summary line
        success_rate = (passed / total * 100) if total > 0 else 0
        color = "green" if failed == 0 else "red"
        
        self._console.print(
            f"\n[{color}]{passed}/{total} tests passed ({success_rate:.0f}%)[/{color}] "
            f"in {self.total_duration_ms:.0f}ms"
        )
    
    def clear(self) -> None:
        """Clear all test results."""
        self._results.clear()
