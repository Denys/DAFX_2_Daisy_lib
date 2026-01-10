"""Timing utilities for performance measurement."""

from __future__ import annotations

import functools
import time
from typing import Any, Callable, TypeVar

from dafx_execution.core import get_logger


logger = get_logger(__name__)

F = TypeVar("F", bound=Callable[..., Any])


def timeit(func: F) -> F:
    """Decorator to measure function execution time.
    
    Logs the function name and execution time at DEBUG level.
    
    Example:
        >>> @timeit
        ... def slow_function():
        ...     time.sleep(1)
        ...     return "done"
        >>> result = slow_function()
        # Logs: slow_function completed in 1000.00ms
    """
    @functools.wraps(func)
    def wrapper(*args: Any, **kwargs: Any) -> Any:
        start = time.perf_counter()
        try:
            result = func(*args, **kwargs)
            return result
        finally:
            elapsed_ms = (time.perf_counter() - start) * 1000
            logger.debug(
                f"{func.__name__} completed",
                duration_ms=f"{elapsed_ms:.2f}",
            )
    
    return wrapper  # type: ignore


class Timer:
    """Context manager for timing code blocks.
    
    Provides elapsed time in seconds and milliseconds.
    
    Example:
        >>> with Timer() as t:
        ...     time.sleep(0.5)
        >>> print(f"Took {t.elapsed_ms:.0f}ms")
        Took 500ms
    """
    
    def __init__(self, name: str = "") -> None:
        """Initialize timer.
        
        Args:
            name: Optional name for logging.
        """
        self.name = name
        self._start: float = 0.0
        self._end: float = 0.0
    
    @property
    def elapsed(self) -> float:
        """Elapsed time in seconds."""
        if self._end > 0:
            return self._end - self._start
        return time.perf_counter() - self._start
    
    @property
    def elapsed_ms(self) -> float:
        """Elapsed time in milliseconds."""
        return self.elapsed * 1000
    
    def __enter__(self) -> "Timer":
        """Start the timer."""
        self._start = time.perf_counter()
        self._end = 0.0
        return self
    
    def __exit__(self, *args: Any) -> None:
        """Stop the timer and optionally log."""
        self._end = time.perf_counter()
        if self.name:
            logger.debug(
                f"{self.name} completed",
                duration_ms=f"{self.elapsed_ms:.2f}",
            )
    
    def reset(self) -> None:
        """Reset the timer."""
        self._start = time.perf_counter()
        self._end = 0.0
