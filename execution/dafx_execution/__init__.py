"""DAFX Execution Scripts Package.

A robust, modular Python execution environment for the DAFX-to-DaisySP library.
Provides DSP algorithm validation, automated testing, and data processing utilities.
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

__version__ = "0.1.0"
__all__ = [
    # Exceptions
    "ExecutionError",
    "ConfigurationError",
    "ProcessingError",
    "ResourceError",
    "ValidationError",
    # Logging
    "get_logger",
    "configure_logging",
    # Engine
    "ExecutionEngine",
]
