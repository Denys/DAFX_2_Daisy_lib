"""Profiling infrastructure for validation performance analysis.

Provides detailed profiling of validation execution with
function-level timing and memory tracking.
"""

from __future__ import annotations

import cProfile
import functools
import io
import pstats
import time
import tracemalloc
from dataclasses import dataclass, field
from typing import Any, Callable, TypeVar

from validation_infrastructure.core.base import ValidationResult


T = TypeVar("T")


@dataclass
class ProfileResult:
    """Result of a profiling session.
    
    Attributes:
        total_time_ms: Total execution time in milliseconds.
        function_stats: Per-function timing statistics.
        memory_peak_kb: Peak memory usage in KB.
        memory_current_kb: Current memory usage in KB.
        call_count: Number of function calls.
        top_functions: Top N slowest functions.
    """
    total_time_ms: float
    function_stats: dict[str, dict[str, Any]] = field(default_factory=dict)
    memory_peak_kb: float = 0.0
    memory_current_kb: float = 0.0
    call_count: int = 0
    top_functions: list[tuple[str, float]] = field(default_factory=list)
    
    def summary(self) -> str:
        """Get human-readable summary."""
        lines = [
            f"Total Time: {self.total_time_ms:.2f}ms",
            f"Function Calls: {self.call_count}",
            f"Peak Memory: {self.memory_peak_kb:.1f}KB",
            "",
            "Top Functions by Time:",
        ]
        
        for func, time_ms in self.top_functions[:10]:
            lines.append(f"  {func}: {time_ms:.3f}ms")
        
        return "\n".join(lines)


class ValidationProfiler:
    """Profiler for validation performance analysis.
    
    Provides detailed timing and memory profiling for validators.
    
    Usage:
        profiler = ValidationProfiler()
        
        with profiler.profile() as p:
            result = validator.validate(data)
        
        print(p.result.summary())
    """
    
    def __init__(
        self,
        track_memory: bool = True,
        detailed_stats: bool = True,
    ) -> None:
        """Initialize profiler.
        
        Args:
            track_memory: Whether to track memory usage.
            detailed_stats: Whether to collect detailed function stats.
        """
        self.track_memory = track_memory
        self.detailed_stats = detailed_stats
        self._profiles: list[ProfileResult] = []
    
    def profile(self) -> "ProfileContext":
        """Create a profiling context.
        
        Returns:
            ProfileContext for use with 'with' statement.
        """
        return ProfileContext(self)
    
    def profile_function(
        self,
        func: Callable[..., T],
    ) -> Callable[..., tuple[T, ProfileResult]]:
        """Decorator to profile a function.
        
        Returns:
            Wrapped function that returns (result, profile).
        """
        @functools.wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> tuple[T, ProfileResult]:
            with self.profile() as p:
                result = func(*args, **kwargs)
            return result, p.result
        
        return wrapper
    
    def add_result(self, result: ProfileResult) -> None:
        """Store a profile result."""
        self._profiles.append(result)
    
    @property
    def results(self) -> list[ProfileResult]:
        """Get all collected profile results."""
        return self._profiles.copy()
    
    def clear(self) -> None:
        """Clear all stored results."""
        self._profiles.clear()
    
    def aggregate(self) -> ProfileResult:
        """Aggregate all collected profile results.
        
        Returns:
            Combined ProfileResult.
        """
        if not self._profiles:
            return ProfileResult(total_time_ms=0.0)
        
        total_time = sum(p.total_time_ms for p in self._profiles)
        total_calls = sum(p.call_count for p in self._profiles)
        peak_memory = max(p.memory_peak_kb for p in self._profiles)
        
        # Aggregate function stats
        agg_stats: dict[str, dict[str, Any]] = {}
        for profile in self._profiles:
            for func, stats in profile.function_stats.items():
                if func not in agg_stats:
                    agg_stats[func] = {"time_ms": 0.0, "calls": 0}
                agg_stats[func]["time_ms"] += stats.get("time_ms", 0.0)
                agg_stats[func]["calls"] += stats.get("calls", 0)
        
        # Get top functions
        top_functions = sorted(
            [(f, s["time_ms"]) for f, s in agg_stats.items()],
            key=lambda x: x[1],
            reverse=True,
        )[:10]
        
        return ProfileResult(
            total_time_ms=total_time,
            function_stats=agg_stats,
            memory_peak_kb=peak_memory,
            call_count=total_calls,
            top_functions=top_functions,
        )


class ProfileContext:
    """Context manager for profiling.
    
    Usage:
        with ProfileContext(profiler) as ctx:
            # Code to profile...
        print(ctx.result)
    """
    
    def __init__(self, profiler: ValidationProfiler) -> None:
        self._profiler = profiler
        self._cprofile: cProfile.Profile | None = None
        self._start_time: float = 0.0
        self._result: ProfileResult | None = None
    
    @property
    def result(self) -> ProfileResult:
        """Get the profile result."""
        if self._result is None:
            raise RuntimeError("Profiling not yet complete")
        return self._result
    
    def __enter__(self) -> "ProfileContext":
        """Start profiling."""
        # Start memory tracking
        if self._profiler.track_memory:
            tracemalloc.start()
        
        # Start CPU profiling
        if self._profiler.detailed_stats:
            self._cprofile = cProfile.Profile()
            self._cprofile.enable()
        
        self._start_time = time.perf_counter()
        return self
    
    def __exit__(self, *args: Any) -> None:
        """Stop profiling and collect results."""
        total_time_ms = (time.perf_counter() - self._start_time) * 1000
        
        # Collect CPU stats
        function_stats: dict[str, dict[str, Any]] = {}
        call_count = 0
        top_functions: list[tuple[str, float]] = []
        
        if self._cprofile:
            self._cprofile.disable()
            
            # Parse stats
            stream = io.StringIO()
            stats = pstats.Stats(self._cprofile, stream=stream)
            stats.sort_stats("cumulative")
            
            for func_key, (cc, nc, tt, ct, callers) in stats.stats.items():
                filename, line, name = func_key
                func_name = f"{name} ({filename}:{line})"
                function_stats[func_name] = {
                    "calls": nc,
                    "time_ms": tt * 1000,
                    "cumulative_ms": ct * 1000,
                }
                call_count += nc
            
            # Top functions by cumulative time
            sorted_funcs = sorted(
                function_stats.items(),
                key=lambda x: x[1]["cumulative_ms"],
                reverse=True,
            )
            top_functions = [
                (name, stats["cumulative_ms"])
                for name, stats in sorted_funcs[:10]
            ]
        
        # Collect memory stats
        memory_peak_kb = 0.0
        memory_current_kb = 0.0
        
        if self._profiler.track_memory:
            current, peak = tracemalloc.get_traced_memory()
            tracemalloc.stop()
            memory_current_kb = current / 1024
            memory_peak_kb = peak / 1024
        
        self._result = ProfileResult(
            total_time_ms=total_time_ms,
            function_stats=function_stats,
            memory_peak_kb=memory_peak_kb,
            memory_current_kb=memory_current_kb,
            call_count=call_count,
            top_functions=top_functions,
        )
        
        self._profiler.add_result(self._result)


def profile_validator(
    track_memory: bool = True,
) -> Callable[[Callable[..., T]], Callable[..., tuple[T, ProfileResult]]]:
    """Decorator factory for profiling validators.
    
    Usage:
        @profile_validator()
        def validate_data(data):
            # validation logic...
            return result
        
        result, profile = validate_data({"key": "value"})
        print(profile.summary())
    """
    profiler = ValidationProfiler(track_memory=track_memory)
    
    def decorator(func: Callable[..., T]) -> Callable[..., tuple[T, ProfileResult]]:
        return profiler.profile_function(func)
    
    return decorator
