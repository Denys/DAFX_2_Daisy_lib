"""Core module for DAFX execution infrastructure.

Contains the execution engine, exception hierarchy, and logging utilities.
"""

from dafx_execution.core.exceptions import (
    ExecutionError,
    ConfigurationError,
    ProcessingError,
    ResourceError,
    ValidationError,
)
from dafx_execution.core.logger import get_logger, configure_logging
from dafx_execution.core.engine import ExecutionEngine

__all__ = [
    "ExecutionError",
    "ConfigurationError",
    "ProcessingError",
    "ResourceError",
    "ValidationError",
    "get_logger",
    "configure_logging",
    "ExecutionEngine",
]
