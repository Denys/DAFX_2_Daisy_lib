"""Audit logging for validation operations."""

from __future__ import annotations

import logging
import threading
import uuid
from dataclasses import dataclass, field
from datetime import datetime, timezone
from enum import Enum
from functools import wraps
from typing import Any, Callable, Dict, List, Optional, TypeVar, Union

from validation_infrastructure.core.base import ValidationResult

T = TypeVar("T")


class AuditEventType(Enum):
    """Types of audit events."""

    VALIDATION_START = "validation_start"
    VALIDATION_SUCCESS = "validation_success"
    VALIDATION_FAILURE = "validation_failure"
    VALIDATION_ERROR = "validation_error"
    SANITIZATION_START = "sanitization_start"
    SANITIZATION_COMPLETE = "sanitization_complete"
    SCHEMA_VALIDATION = "schema_validation"
    TYPE_CHECK = "type_check"
    CONSTRAINT_CHECK = "constraint_check"
    CUSTOM_RULE = "custom_rule"
    CONFIG_LOAD = "config_load"
    ENV_VALIDATION = "env_validation"
    FILE_VALIDATION = "file_validation"
    API_REQUEST_VALIDATION = "api_request_validation"
    API_RESPONSE_VALIDATION = "api_response_validation"
    DATABASE_VALIDATION = "database_validation"


@dataclass
class AuditEvent:
    """Represents an audit event."""

    event_id: str
    event_type: AuditEventType
    timestamp: datetime
    validator_name: str
    success: bool
    duration_ms: float
    input_summary: Optional[str] = None
    output_summary: Optional[str] = None
    error_message: Optional[str] = None
    error_code: Optional[str] = None
    metadata: Dict[str, Any] = field(default_factory=dict)
    correlation_id: Optional[str] = None
    user_id: Optional[str] = None
    request_id: Optional[str] = None
    ip_address: Optional[str] = None
    field_path: Optional[str] = None
    issues_count: int = 0

    def to_dict(self) -> Dict[str, Any]:
        """Convert event to dictionary."""
        return {
            "event_id": self.event_id,
            "event_type": self.event_type.value,
            "timestamp": self.timestamp.isoformat(),
            "validator_name": self.validator_name,
            "success": self.success,
            "duration_ms": self.duration_ms,
            "input_summary": self.input_summary,
            "output_summary": self.output_summary,
            "error_message": self.error_message,
            "error_code": self.error_code,
            "metadata": self.metadata,
            "correlation_id": self.correlation_id,
            "user_id": self.user_id,
            "request_id": self.request_id,
            "ip_address": self.ip_address,
            "field_path": self.field_path,
            "issues_count": self.issues_count,
        }


class AuditLogger:
    """Centralized audit logger for validation operations."""

    _instance: Optional[AuditLogger] = None
    _lock = threading.Lock()

    def __new__(cls) -> AuditLogger:
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance

    def __init__(self):
        if self._initialized:
            return

        self._logger = logging.getLogger("validation.audit")
        self._handlers: List[logging.Handler] = []
        self._enabled = True
        self._min_duration_ms = 0.0  # Only log if duration exceeds this
        self._include_input_summary = True
        self._max_input_length = 100
        self._correlation_id: Optional[str] = None
        self._context_data: Dict[str, Any] = {}
        self._event_listeners: List[Callable[[AuditEvent], None]] = []
        self._initialized = True

    def configure(
        self,
        enabled: bool = True,
        min_duration_ms: float = 0.0,
        include_input_summary: bool = True,
        max_input_length: int = 100,
        level: int = logging.INFO,
    ) -> None:
        """Configure the audit logger."""
        self._enabled = enabled
        self._min_duration_ms = min_duration_ms
        self._include_input_summary = include_input_summary
        self._max_input_length = max_input_length
        self._logger.setLevel(level)

    def add_handler(self, handler: logging.Handler) -> None:
        """Add a logging handler."""
        self._logger.addHandler(handler)
        self._handlers.append(handler)

    def remove_handler(self, handler: logging.Handler) -> None:
        """Remove a logging handler."""
        self._logger.removeHandler(handler)
        if handler in self._handlers:
            self._handlers.remove(handler)

    def add_listener(self, listener: Callable[[AuditEvent], None]) -> None:
        """Add an event listener for custom processing."""
        self._event_listeners.append(listener)

    def remove_listener(self, listener: Callable[[AuditEvent], None]) -> None:
        """Remove an event listener."""
        if listener in self._event_listeners:
            self._event_listeners.remove(listener)

    def set_correlation_id(self, correlation_id: Optional[str]) -> None:
        """Set the correlation ID for request tracking."""
        self._correlation_id = correlation_id

    def set_context(self, **kwargs: Any) -> None:
        """Set context data to include in all events."""
        self._context_data.update(kwargs)

    def clear_context(self) -> None:
        """Clear context data."""
        self._context_data.clear()
        self._correlation_id = None

    def _summarize_value(self, value: Any) -> Optional[str]:
        """Create a summary of the input value."""
        if not self._include_input_summary:
            return None

        try:
            if value is None:
                return "None"
            
            val_str = str(value)
            if len(val_str) > self._max_input_length:
                return val_str[: self._max_input_length] + "..."
            return val_str
        except Exception:
            return "<unserializable>"

    def log_event(self, event: AuditEvent) -> None:
        """Log an audit event."""
        if not self._enabled:
            return

        if event.duration_ms < self._min_duration_ms:
            return

        # Add correlation ID if set
        if self._correlation_id and not event.correlation_id:
            event.correlation_id = self._correlation_id

        # Add context data to metadata
        event.metadata.update(self._context_data)

        # Log to standard logger
        log_level = logging.INFO if event.success else logging.WARNING
        self._logger.log(
            log_level,
            "Validation %s: %s [%s] (%.2fms) - %s",
            event.event_type.value,
            event.validator_name,
            "OK" if event.success else "FAILED",
            event.duration_ms,
            event.error_message or "success",
            extra={"audit_event": event.to_dict()},
        )

        # Notify listeners
        for listener in self._event_listeners:
            try:
                listener(event)
            except Exception:
                pass  # Don't let listener errors break logging

    def log_validation(
        self,
        event_type: AuditEventType,
        validator_name: str,
        success: bool,
        duration_ms: float,
        input_value: Any = None,
        output_value: Any = None,
        error_message: Optional[str] = None,
        error_code: Optional[str] = None,
        field_path: Optional[str] = None,
        issues_count: int = 0,
        **metadata: Any,
    ) -> AuditEvent:
        """Convenience method to log a validation event."""
        event = AuditEvent(
            event_id=str(uuid.uuid4()),
            event_type=event_type,
            timestamp=datetime.now(timezone.utc),
            validator_name=validator_name,
            success=success,
            duration_ms=duration_ms,
            input_summary=self._summarize_value(input_value),
            output_summary=self._summarize_value(output_value),
            error_message=error_message,
            error_code=error_code,
            field_path=field_path,
            issues_count=issues_count,
            metadata=metadata,
        )
        self.log_event(event)
        return event

    def log_validation_result(
        self,
        result: ValidationResult[Any],
        validator_name: str,
        duration_ms: float,
        input_value: Any = None,
        **metadata: Any,
    ) -> AuditEvent:
        """Log a validation result."""
        error_message = None
        error_code = None

        if not result.is_valid and result.issues:
            first_issue = result.issues[0]
            error_message = first_issue.message
            error_code = first_issue.code

        event_type = (
            AuditEventType.VALIDATION_SUCCESS
            if result.is_valid
            else AuditEventType.VALIDATION_FAILURE
        )

        return self.log_validation(
            event_type=event_type,
            validator_name=validator_name,
            success=result.is_valid,
            duration_ms=duration_ms,
            input_value=input_value,
            output_value=result.value,
            error_message=error_message,
            error_code=error_code,
            issues_count=len(result.issues),
            **metadata,
        )


def get_audit_logger() -> AuditLogger:
    """Get the global audit logger instance."""
    return AuditLogger()


def log_validation(
    validator_name: Optional[str] = None,
    event_type: AuditEventType = AuditEventType.VALIDATION_START,
    include_result: bool = True,
) -> Callable[[Callable[..., T]], Callable[..., T]]:
    """
    Decorator to automatically log validation function calls.

    Example:
        @log_validation(validator_name="email_validator")
        def validate_email(email: str) -> ValidationResult[str]:
            ...
    """

    def decorator(func: Callable[..., T]) -> Callable[..., T]:
        name = validator_name or func.__name__

        @wraps(func)
        def wrapper(*args: Any, **kwargs: Any) -> T:
            import time

            logger = get_audit_logger()
            start_time = time.perf_counter()

            try:
                result = func(*args, **kwargs)
                duration_ms = (time.perf_counter() - start_time) * 1000

                # If result is a ValidationResult, log it
                if include_result and isinstance(result, ValidationResult):
                    input_value = args[0] if args else kwargs.get("value")
                    logger.log_validation_result(
                        result=result,
                        validator_name=name,
                        duration_ms=duration_ms,
                        input_value=input_value,
                    )
                else:
                    logger.log_validation(
                        event_type=event_type,
                        validator_name=name,
                        success=True,
                        duration_ms=duration_ms,
                    )

                return result

            except Exception as e:
                duration_ms = (time.perf_counter() - start_time) * 1000
                logger.log_validation(
                    event_type=AuditEventType.VALIDATION_ERROR,
                    validator_name=name,
                    success=False,
                    duration_ms=duration_ms,
                    error_message=str(e),
                    error_code=type(e).__name__,
                )
                raise

        return wrapper

    return decorator


class AuditContext:
    """Context manager for grouped audit logging."""

    def __init__(
        self,
        correlation_id: Optional[str] = None,
        **context_data: Any,
    ):
        self.correlation_id = correlation_id or str(uuid.uuid4())
        self.context_data = context_data
        self._logger = get_audit_logger()
        self._previous_correlation_id: Optional[str] = None
        self._previous_context: Dict[str, Any] = {}

    def __enter__(self) -> AuditContext:
        # Save previous state
        self._previous_correlation_id = self._logger._correlation_id
        self._previous_context = self._logger._context_data.copy()

        # Set new context
        self._logger.set_correlation_id(self.correlation_id)
        self._logger.set_context(**self.context_data)

        return self

    def __exit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        # Restore previous state
        self._logger._correlation_id = self._previous_correlation_id
        self._logger._context_data = self._previous_context

    async def __aenter__(self) -> AuditContext:
        return self.__enter__()

    async def __aexit__(self, exc_type: Any, exc_val: Any, exc_tb: Any) -> None:
        self.__exit__(exc_type, exc_val, exc_tb)
