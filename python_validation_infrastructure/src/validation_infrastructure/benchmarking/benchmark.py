"""Benchmarking tools for validation performance measurement."""

from __future__ import annotations

import gc
import statistics
import time
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Generic, List, Optional, TypeVar, Union

from validation_infrastructure.core.base import BaseValidator, ValidationResult

T = TypeVar("T")


@dataclass
class BenchmarkResult:
    """Results from a benchmark run."""

    name: str
    iterations: int
    total_time_seconds: float
    mean_time_ms: float
    median_time_ms: float
    std_dev_ms: float
    min_time_ms: float
    max_time_ms: float
    throughput_per_second: float
    percentile_95_ms: float
    percentile_99_ms: float
    success_rate: float
    error_count: int
    memory_delta_bytes: Optional[int] = None
    metadata: Dict[str, Any] = field(default_factory=dict)

    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary."""
        return {
            "name": self.name,
            "iterations": self.iterations,
            "total_time_seconds": self.total_time_seconds,
            "mean_time_ms": self.mean_time_ms,
            "median_time_ms": self.median_time_ms,
            "std_dev_ms": self.std_dev_ms,
            "min_time_ms": self.min_time_ms,
            "max_time_ms": self.max_time_ms,
            "throughput_per_second": self.throughput_per_second,
            "percentile_95_ms": self.percentile_95_ms,
            "percentile_99_ms": self.percentile_99_ms,
            "success_rate": self.success_rate,
            "error_count": self.error_count,
            "memory_delta_bytes": self.memory_delta_bytes,
            "metadata": self.metadata,
        }


class Benchmark:
    """Generic benchmarking class."""

    def __init__(
        self,
        name: str,
        func: Callable[..., Any],
        args: tuple = (),
        kwargs: Optional[Dict[str, Any]] = None,
    ):
        self.name = name
        self.func = func
        self.args = args
        self.kwargs = kwargs or {}

    def run(
        self,
        iterations: int = 1000,
        warmup_iterations: int = 100,
        track_memory: bool = False,
    ) -> BenchmarkResult:
        """Run the benchmark."""
        # Warmup
        for _ in range(warmup_iterations):
            self.func(*self.args, **self.kwargs)

        # Force garbage collection before timing
        gc.collect()

        # Track memory if requested
        memory_before = None
        if track_memory:
            try:
                import tracemalloc
                tracemalloc.start()
                memory_before = tracemalloc.get_traced_memory()[0]
            except ImportError:
                pass

        # Run benchmark
        times_ms: List[float] = []
        error_count = 0
        success_count = 0

        start_total = time.perf_counter()

        for _ in range(iterations):
            start = time.perf_counter()
            try:
                result = self.func(*self.args, **self.kwargs)
                elapsed = (time.perf_counter() - start) * 1000
                times_ms.append(elapsed)

                # Check if result indicates success
                if isinstance(result, ValidationResult):
                    if result.is_valid:
                        success_count += 1
                    else:
                        error_count += 1
                else:
                    success_count += 1
            except Exception:
                elapsed = (time.perf_counter() - start) * 1000
                times_ms.append(elapsed)
                error_count += 1

        total_time = time.perf_counter() - start_total

        # Get memory delta
        memory_delta = None
        if track_memory and memory_before is not None:
            try:
                import tracemalloc
                memory_after = tracemalloc.get_traced_memory()[0]
                memory_delta = memory_after - memory_before
                tracemalloc.stop()
            except Exception:
                pass

        # Calculate statistics
        sorted_times = sorted(times_ms)
        p95_idx = int(len(sorted_times) * 0.95)
        p99_idx = int(len(sorted_times) * 0.99)

        return BenchmarkResult(
            name=self.name,
            iterations=iterations,
            total_time_seconds=total_time,
            mean_time_ms=statistics.mean(times_ms),
            median_time_ms=statistics.median(times_ms),
            std_dev_ms=statistics.stdev(times_ms) if len(times_ms) > 1 else 0,
            min_time_ms=min(times_ms),
            max_time_ms=max(times_ms),
            throughput_per_second=iterations / total_time if total_time > 0 else 0,
            percentile_95_ms=sorted_times[p95_idx] if p95_idx < len(sorted_times) else max(times_ms),
            percentile_99_ms=sorted_times[p99_idx] if p99_idx < len(sorted_times) else max(times_ms),
            success_rate=success_count / iterations if iterations > 0 else 0,
            error_count=error_count,
            memory_delta_bytes=memory_delta,
        )


class ValidationBenchmark(Generic[T]):
    """Specialized benchmark for validators."""

    def __init__(
        self,
        validator: BaseValidator[T],
        test_data: List[Any],
        name: Optional[str] = None,
    ):
        self.validator = validator
        self.test_data = test_data
        self.name = name or validator.name

    def run(
        self,
        iterations_per_item: int = 100,
        warmup_iterations: int = 10,
        track_memory: bool = False,
    ) -> BenchmarkResult:
        """Run benchmark across all test data."""
        all_times_ms: List[float] = []
        error_count = 0
        success_count = 0

        # Warmup
        for data in self.test_data[:min(len(self.test_data), 10)]:
            for _ in range(warmup_iterations):
                self.validator.validate(data)

        gc.collect()

        memory_before = None
        if track_memory:
            try:
                import tracemalloc
                tracemalloc.start()
                memory_before = tracemalloc.get_traced_memory()[0]
            except ImportError:
                pass

        start_total = time.perf_counter()

        for data in self.test_data:
            for _ in range(iterations_per_item):
                start = time.perf_counter()
                try:
                    result = self.validator.validate(data)
                    elapsed = (time.perf_counter() - start) * 1000
                    all_times_ms.append(elapsed)

                    if result.is_valid:
                        success_count += 1
                    else:
                        error_count += 1
                except Exception:
                    elapsed = (time.perf_counter() - start) * 1000
                    all_times_ms.append(elapsed)
                    error_count += 1

        total_time = time.perf_counter() - start_total
        total_iterations = len(self.test_data) * iterations_per_item

        memory_delta = None
        if track_memory and memory_before is not None:
            try:
                import tracemalloc
                memory_after = tracemalloc.get_traced_memory()[0]
                memory_delta = memory_after - memory_before
                tracemalloc.stop()
            except Exception:
                pass

        sorted_times = sorted(all_times_ms)
        p95_idx = int(len(sorted_times) * 0.95)
        p99_idx = int(len(sorted_times) * 0.99)

        return BenchmarkResult(
            name=self.name,
            iterations=total_iterations,
            total_time_seconds=total_time,
            mean_time_ms=statistics.mean(all_times_ms),
            median_time_ms=statistics.median(all_times_ms),
            std_dev_ms=statistics.stdev(all_times_ms) if len(all_times_ms) > 1 else 0,
            min_time_ms=min(all_times_ms),
            max_time_ms=max(all_times_ms),
            throughput_per_second=total_iterations / total_time if total_time > 0 else 0,
            percentile_95_ms=sorted_times[p95_idx] if p95_idx < len(sorted_times) else max(all_times_ms),
            percentile_99_ms=sorted_times[p99_idx] if p99_idx < len(sorted_times) else max(all_times_ms),
            success_rate=success_count / total_iterations if total_iterations > 0 else 0,
            error_count=error_count,
            memory_delta_bytes=memory_delta,
            metadata={"test_data_count": len(self.test_data)},
        )


class BenchmarkSuite:
    """Suite of benchmarks to run together."""

    def __init__(self, name: str = "Validation Benchmark Suite"):
        self.name = name
        self.benchmarks: List[Union[Benchmark, ValidationBenchmark[Any]]] = []
        self.results: List[BenchmarkResult] = []

    def add(self, benchmark: Union[Benchmark, ValidationBenchmark[Any]]) -> None:
        """Add a benchmark to the suite."""
        self.benchmarks.append(benchmark)

    def add_validator(
        self,
        validator: BaseValidator[Any],
        test_data: List[Any],
        name: Optional[str] = None,
    ) -> None:
        """Add a validator benchmark."""
        self.benchmarks.append(
            ValidationBenchmark(validator, test_data, name)
        )

    def add_function(
        self,
        name: str,
        func: Callable[..., Any],
        args: tuple = (),
        kwargs: Optional[Dict[str, Any]] = None,
    ) -> None:
        """Add a function benchmark."""
        self.benchmarks.append(Benchmark(name, func, args, kwargs))

    def run(
        self,
        iterations: int = 1000,
        warmup_iterations: int = 100,
        track_memory: bool = False,
    ) -> List[BenchmarkResult]:
        """Run all benchmarks in the suite."""
        self.results = []

        for benchmark in self.benchmarks:
            if isinstance(benchmark, ValidationBenchmark):
                result = benchmark.run(
                    iterations_per_item=iterations,
                    warmup_iterations=warmup_iterations,
                    track_memory=track_memory,
                )
            else:
                result = benchmark.run(
                    iterations=iterations,
                    warmup_iterations=warmup_iterations,
                    track_memory=track_memory,
                )
            self.results.append(result)

        return self.results

    def compare(self) -> Dict[str, Any]:
        """Compare results across benchmarks."""
        if not self.results:
            return {}

        fastest = min(self.results, key=lambda r: r.mean_time_ms)
        slowest = max(self.results, key=lambda r: r.mean_time_ms)
        highest_throughput = max(self.results, key=lambda r: r.throughput_per_second)

        return {
            "fastest": {
                "name": fastest.name,
                "mean_time_ms": fastest.mean_time_ms,
            },
            "slowest": {
                "name": slowest.name,
                "mean_time_ms": slowest.mean_time_ms,
            },
            "highest_throughput": {
                "name": highest_throughput.name,
                "throughput": highest_throughput.throughput_per_second,
            },
            "speedup_factor": slowest.mean_time_ms / fastest.mean_time_ms if fastest.mean_time_ms > 0 else 0,
        }


def run_benchmark(
    validator: BaseValidator[Any],
    test_data: List[Any],
    iterations: int = 1000,
    name: Optional[str] = None,
) -> BenchmarkResult:
    """Convenience function to run a single validator benchmark."""
    benchmark = ValidationBenchmark(validator, test_data, name)
    return benchmark.run(iterations_per_item=iterations)


def compare_validators(
    validators: Dict[str, BaseValidator[Any]],
    test_data: List[Any],
    iterations: int = 1000,
) -> Dict[str, BenchmarkResult]:
    """Compare multiple validators on the same test data."""
    results = {}

    for name, validator in validators.items():
        benchmark = ValidationBenchmark(validator, test_data, name)
        results[name] = benchmark.run(iterations_per_item=iterations)

    return results
