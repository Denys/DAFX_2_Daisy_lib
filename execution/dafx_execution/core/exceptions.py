"""Custom exception hierarchy for DAFX execution scripts.

All exceptions inherit from ExecutionError to enable unified exception handling.
Each exception captures context (file, parameters, etc.) for debugging.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any


@dataclass
class ErrorContext:
    """Context information for debugging execution errors.
    
    Attributes:
        file_path: Path to the file being processed when error occurred.
        parameters: Parameters in use when error occurred.
        operation: Description of the operation that failed.
        additional: Any additional context data.
    """
    file_path: Path | None = None
    parameters: dict[str, Any] = field(default_factory=dict)
    operation: str = ""
    additional: dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> dict[str, Any]:
        """Convert context to dictionary for logging."""
        return {
            "file_path": str(self.file_path) if self.file_path else None,
            "parameters": self.parameters,
            "operation": self.operation,
            "additional": self.additional,
        }


class ExecutionError(Exception):
    """Base exception for all DAFX execution errors.
    
    All custom exceptions should inherit from this class to enable
    unified exception handling and consistent error reporting.
    
    Attributes:
        message: Human-readable error description.
        context: Structured context for debugging.
        exit_code: Suggested POSIX exit code for CLI.
    """
    
    exit_code: int = 99  # Unexpected error
    
    def __init__(
        self,
        message: str,
        context: ErrorContext | None = None,
        cause: Exception | None = None,
    ) -> None:
        """Initialize the execution error.
        
        Args:
            message: Human-readable error description.
            context: Structured debugging context.
            cause: Original exception that caused this error.
        """
        super().__init__(message)
        self.message = message
        self.context = context or ErrorContext()
        self.cause = cause
    
    def __str__(self) -> str:
        """Format error message with context."""
        parts = [self.message]
        
        if self.context.operation:
            parts.append(f"Operation: {self.context.operation}")
        
        if self.context.file_path:
            parts.append(f"File: {self.context.file_path}")
        
        if self.cause:
            parts.append(f"Caused by: {type(self.cause).__name__}: {self.cause}")
        
        return " | ".join(parts)
    
    def to_dict(self) -> dict[str, Any]:
        """Convert error to dictionary for structured logging."""
        return {
            "error_type": type(self).__name__,
            "message": self.message,
            "context": self.context.to_dict(),
            "exit_code": self.exit_code,
            "cause": str(self.cause) if self.cause else None,
        }


class ConfigurationError(ExecutionError):
    """Error in configuration loading or validation.
    
    Raised when:
    - Config file not found or unreadable
    - Config file has invalid syntax
    - Required configuration values are missing
    - Configuration values fail validation
    """
    
    exit_code: int = 2
    
    def __init__(
        self,
        message: str,
        config_key: str | None = None,
        config_file: Path | None = None,
        **kwargs: Any,
    ) -> None:
        """Initialize configuration error.
        
        Args:
            message: Error description.
            config_key: The configuration key that caused the error.
            config_file: Path to the configuration file.
            **kwargs: Additional arguments passed to parent.
        """
        context = kwargs.pop("context", None) or ErrorContext()
        context.additional["config_key"] = config_key
        if config_file:
            context.file_path = config_file
        kwargs["context"] = context
        super().__init__(message, **kwargs)
        self.config_key = config_key
        self.config_file = config_file


class ProcessingError(ExecutionError):
    """Error during data or DSP processing.
    
    Raised when:
    - Audio file processing fails
    - DSP algorithm produces invalid output
    - Data transformation fails
    - Numerical computation errors
    """
    
    exit_code: int = 1
    
    def __init__(
        self,
        message: str,
        input_data: Any = None,
        stage: str = "",
        **kwargs: Any,
    ) -> None:
        """Initialize processing error.
        
        Args:
            message: Error description.
            input_data: Summary of input data (avoid large objects).
            stage: Processing stage where error occurred.
            **kwargs: Additional arguments passed to parent.
        """
        context = kwargs.pop("context", None) or ErrorContext()
        context.operation = stage or context.operation
        context.additional["input_summary"] = (
            repr(input_data)[:200] if input_data else None
        )
        kwargs["context"] = context
        super().__init__(message, **kwargs)
        self.stage = stage


class ResourceError(ExecutionError):
    """Error accessing external resources.
    
    Raised when:
    - File not found or permission denied
    - Database connection fails
    - Network resource unavailable
    - Memory allocation fails
    """
    
    exit_code: int = 3
    
    def __init__(
        self,
        message: str,
        resource_type: str = "",
        resource_path: str | Path | None = None,
        **kwargs: Any,
    ) -> None:
        """Initialize resource error.
        
        Args:
            message: Error description.
            resource_type: Type of resource (file, database, network, memory).
            resource_path: Path or identifier of the resource.
            **kwargs: Additional arguments passed to parent.
        """
        context = kwargs.pop("context", None) or ErrorContext()
        context.additional["resource_type"] = resource_type
        if resource_path:
            context.file_path = Path(resource_path) if isinstance(resource_path, str) else resource_path
        kwargs["context"] = context
        super().__init__(message, **kwargs)
        self.resource_type = resource_type
        self.resource_path = resource_path


class ValidationError(ExecutionError):
    """Error validating input data or parameters.
    
    Raised when:
    - Input data fails schema validation
    - Parameters are out of acceptable range
    - Type validation fails
    - Business rule validation fails
    """
    
    exit_code: int = 1
    
    def __init__(
        self,
        message: str,
        field_name: str | None = None,
        invalid_value: Any = None,
        expected: str = "",
        **kwargs: Any,
    ) -> None:
        """Initialize validation error.
        
        Args:
            message: Error description.
            field_name: Name of the field that failed validation.
            invalid_value: The value that failed validation.
            expected: Description of expected value/format.
            **kwargs: Additional arguments passed to parent.
        """
        context = kwargs.pop("context", None) or ErrorContext()
        context.additional.update({
            "field_name": field_name,
            "invalid_value": repr(invalid_value)[:100] if invalid_value is not None else None,
            "expected": expected,
        })
        kwargs["context"] = context
        super().__init__(message, **kwargs)
        self.field_name = field_name
        self.invalid_value = invalid_value
        self.expected = expected


class TimeoutError(ExecutionError):
    """Error when an operation exceeds its time limit.
    
    Raised when:
    - External API call times out
    - Long-running computation exceeds limit
    - Resource acquisition times out
    """
    
    exit_code: int = 4
    
    def __init__(
        self,
        message: str,
        timeout_seconds: float = 0.0,
        elapsed_seconds: float = 0.0,
        **kwargs: Any,
    ) -> None:
        """Initialize timeout error.
        
        Args:
            message: Error description.
            timeout_seconds: The timeout limit that was exceeded.
            elapsed_seconds: Actual time elapsed before timeout.
            **kwargs: Additional arguments passed to parent.
        """
        context = kwargs.pop("context", None) or ErrorContext()
        context.additional.update({
            "timeout_seconds": timeout_seconds,
            "elapsed_seconds": elapsed_seconds,
        })
        kwargs["context"] = context
        super().__init__(message, **kwargs)
        self.timeout_seconds = timeout_seconds
        self.elapsed_seconds = elapsed_seconds
