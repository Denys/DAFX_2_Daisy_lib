"""Centralized logging configuration for DAFX execution scripts.

Provides:
- Structured JSON logging for machine parsing
- Rich console output for human readability
- Configurable log levels per destination
- Context injection for tracing
"""

from __future__ import annotations

import logging
import sys
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import structlog
from rich.console import Console
from rich.logging import RichHandler


# Module-level console for rich output
_console = Console(stderr=True)

# Default log directory
DEFAULT_LOG_DIR = Path(".tmp/logs")


def _add_timestamp(
    logger: logging.Logger,
    method_name: str,
    event_dict: dict[str, Any],
) -> dict[str, Any]:
    """Add ISO timestamp to log event."""
    event_dict["timestamp"] = datetime.now(timezone.utc).isoformat()
    return event_dict


def _add_caller_info(
    logger: logging.Logger,
    method_name: str,
    event_dict: dict[str, Any],
) -> dict[str, Any]:
    """Add caller module and function name."""
    # structlog provides this via CallsiteParameter processor
    return event_dict


def configure_logging(
    level: str = "INFO",
    log_dir: Path | None = None,
    json_output: bool = True,
    console_output: bool = True,
    app_name: str = "dafx-execution",
) -> None:
    """Configure the logging system.
    
    Sets up both console (human-readable) and file (JSON) logging.
    
    Args:
        level: Logging level (DEBUG, INFO, WARNING, ERROR, CRITICAL).
        log_dir: Directory for log files. Created if doesn't exist.
        json_output: Whether to write JSON logs to file.
        console_output: Whether to write to console.
        app_name: Application name for log context.
    
    Example:
        >>> configure_logging(level="DEBUG", log_dir=Path("logs"))
        >>> logger = get_logger("my_module")
        >>> logger.info("Started processing", file="audio.wav")
    """
    log_dir = log_dir or DEFAULT_LOG_DIR
    log_dir.mkdir(parents=True, exist_ok=True)
    
    # Configure standard logging
    handlers: list[logging.Handler] = []
    
    if console_output:
        # Rich console handler for beautiful output
        rich_handler = RichHandler(
            console=_console,
            show_time=True,
            show_path=False,
            rich_tracebacks=True,
            tracebacks_show_locals=True,
        )
        rich_handler.setLevel(level)
        handlers.append(rich_handler)
    
    if json_output:
        # JSON file handler for machine parsing
        log_file = log_dir / f"{app_name}_{datetime.now():%Y%m%d}.jsonl"
        file_handler = logging.FileHandler(log_file, encoding="utf-8")
        file_handler.setLevel(level)
        handlers.append(file_handler)
    
    # Root logger configuration
    logging.basicConfig(
        level=level,
        handlers=handlers,
        force=True,
    )
    
    # Configure structlog
    shared_processors: list[structlog.types.Processor] = [
        structlog.contextvars.merge_contextvars,
        structlog.stdlib.add_log_level,
        structlog.stdlib.add_logger_name,
        structlog.stdlib.PositionalArgumentsFormatter(),
        structlog.processors.TimeStamper(fmt="iso"),
        structlog.processors.StackInfoRenderer(),
        structlog.processors.UnicodeDecoder(),
    ]
    
    if json_output:
        # JSON renderer for file output
        structlog.configure(
            processors=[
                *shared_processors,
                structlog.stdlib.ProcessorFormatter.wrap_for_formatter,
            ],
            wrapper_class=structlog.stdlib.BoundLogger,
            context_class=dict,
            logger_factory=structlog.stdlib.LoggerFactory(),
            cache_logger_on_first_use=True,
        )
    else:
        # Console renderer
        structlog.configure(
            processors=[
                *shared_processors,
                structlog.dev.ConsoleRenderer(colors=True),
            ],
            wrapper_class=structlog.stdlib.BoundLogger,
            context_class=dict,
            logger_factory=structlog.stdlib.LoggerFactory(),
            cache_logger_on_first_use=True,
        )
    
    # Log configuration event
    logger = get_logger(__name__)
    logger.debug(
        "Logging configured",
        level=level,
        log_dir=str(log_dir),
        json_output=json_output,
        console_output=console_output,
    )


def get_logger(name: str) -> structlog.stdlib.BoundLogger:
    """Get a logger instance with the given name.
    
    Args:
        name: Logger name, typically __name__ of the calling module.
    
    Returns:
        Configured structlog BoundLogger instance.
    
    Example:
        >>> logger = get_logger(__name__)
        >>> logger.info("Processing started", input_file="test.wav")
        >>> logger.error("Processing failed", error="Invalid format")
    """
    return structlog.get_logger(name)


def bind_context(**kwargs: Any) -> None:
    """Bind context variables that will be included in all subsequent logs.
    
    Use this to add request IDs, user info, or other cross-cutting data.
    
    Args:
        **kwargs: Key-value pairs to bind to logging context.
    
    Example:
        >>> bind_context(request_id="abc123", user="admin")
        >>> logger.info("Action taken")  # Will include request_id and user
    """
    structlog.contextvars.bind_contextvars(**kwargs)


def clear_context() -> None:
    """Clear all bound context variables."""
    structlog.contextvars.clear_contextvars()


class LoggerMixin:
    """Mixin class that provides a logger property.
    
    Classes inheriting from this mixin get a preconfigured logger
    accessible via self.logger.
    
    Example:
        >>> class MyProcessor(LoggerMixin):
        ...     def process(self):
        ...         self.logger.info("Processing started")
    """
    
    @property
    def logger(self) -> structlog.stdlib.BoundLogger:
        """Get logger for this class instance."""
        return get_logger(self.__class__.__module__)
