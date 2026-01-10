"""Custom log formatters for validation audit trails."""

from __future__ import annotations

import json
import logging
from datetime import datetime
from typing import Any, Dict, Optional


class AuditFormatter(logging.Formatter):
    """Standard formatter for audit logs."""

    DEFAULT_FORMAT = (
        "%(asctime)s | %(levelname)-8s | %(name)s | %(message)s"
    )

    DEFAULT_DATE_FORMAT = "%Y-%m-%d %H:%M:%S"

    def __init__(
        self,
        fmt: Optional[str] = None,
        datefmt: Optional[str] = None,
        include_event_details: bool = True,
    ):
        super().__init__(
            fmt=fmt or self.DEFAULT_FORMAT,
            datefmt=datefmt or self.DEFAULT_DATE_FORMAT,
        )
        self.include_event_details = include_event_details

    def format(self, record: logging.LogRecord) -> str:
        """Format the log record."""
        # Get base formatted message
        message = super().format(record)

        # Append audit event details if present
        if self.include_event_details:
            event = getattr(record, "audit_event", None)
            if event:
                details = self._format_event_details(event)
                if details:
                    message = f"{message}\n    {details}"

        return message

    def _format_event_details(self, event: Dict[str, Any]) -> str:
        """Format event details for display."""
        parts = []

        if event.get("validator_name"):
            parts.append(f"validator={event['validator_name']}")

        if event.get("duration_ms") is not None:
            parts.append(f"duration={event['duration_ms']:.2f}ms")

        if event.get("issues_count"):
            parts.append(f"issues={event['issues_count']}")

        if event.get("field_path"):
            parts.append(f"field={event['field_path']}")

        if event.get("correlation_id"):
            parts.append(f"correlation_id={event['correlation_id']}")

        return " | ".join(parts)


class JSONAuditFormatter(logging.Formatter):
    """JSON formatter for structured audit logs."""

    def __init__(
        self,
        indent: Optional[int] = None,
        ensure_ascii: bool = False,
        include_extra_fields: bool = True,
    ):
        super().__init__()
        self.indent = indent
        self.ensure_ascii = ensure_ascii
        self.include_extra_fields = include_extra_fields

    def format(self, record: logging.LogRecord) -> str:
        """Format the record as JSON."""
        log_data: Dict[str, Any] = {
            "timestamp": datetime.utcnow().isoformat() + "Z",
            "level": record.levelname,
            "logger": record.name,
            "message": record.getMessage(),
        }

        # Add location info
        log_data["location"] = {
            "file": record.filename,
            "line": record.lineno,
            "function": record.funcName,
        }

        # Add audit event if present
        event = getattr(record, "audit_event", None)
        if event:
            log_data["audit_event"] = event

        # Add extra fields
        if self.include_extra_fields:
            for key in ["correlation_id", "user_id", "request_id", "ip_address"]:
                if hasattr(record, key):
                    log_data[key] = getattr(record, key)

        # Add exception info
        if record.exc_info:
            log_data["exception"] = {
                "type": record.exc_info[0].__name__ if record.exc_info[0] else None,
                "message": str(record.exc_info[1]) if record.exc_info[1] else None,
            }

        return json.dumps(
            log_data,
            indent=self.indent,
            ensure_ascii=self.ensure_ascii,
            default=str,
        )


class DetailedAuditFormatter(logging.Formatter):
    """Detailed multi-line formatter for audit logs."""

    SEPARATOR = "-" * 80

    def __init__(
        self,
        include_separator: bool = True,
        include_input_summary: bool = True,
        include_stack_trace: bool = True,
    ):
        super().__init__()
        self.include_separator = include_separator
        self.include_input_summary = include_input_summary
        self.include_stack_trace = include_stack_trace

    def format(self, record: logging.LogRecord) -> str:
        """Format the record with detailed information."""
        lines = []

        if self.include_separator:
            lines.append(self.SEPARATOR)

        # Header
        timestamp = datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S.%f")[:-3]
        lines.append(f"[{timestamp}] {record.levelname} - {record.name}")

        # Main message
        lines.append(f"Message: {record.getMessage()}")

        # Audit event details
        event = getattr(record, "audit_event", None)
        if event:
            lines.append("")
            lines.append("Audit Event:")
            lines.append(f"  Event ID:      {event.get('event_id', 'N/A')}")
            lines.append(f"  Event Type:    {event.get('event_type', 'N/A')}")
            lines.append(f"  Validator:     {event.get('validator_name', 'N/A')}")
            lines.append(f"  Success:       {event.get('success', 'N/A')}")
            lines.append(f"  Duration:      {event.get('duration_ms', 0):.2f}ms")

            if event.get("field_path"):
                lines.append(f"  Field Path:    {event['field_path']}")

            if event.get("issues_count"):
                lines.append(f"  Issues Count:  {event['issues_count']}")

            if event.get("error_message"):
                lines.append(f"  Error:         {event['error_message']}")

            if event.get("error_code"):
                lines.append(f"  Error Code:    {event['error_code']}")

            if self.include_input_summary and event.get("input_summary"):
                lines.append(f"  Input:         {event['input_summary']}")

            if event.get("correlation_id"):
                lines.append(f"  Correlation:   {event['correlation_id']}")

            if event.get("metadata"):
                lines.append("  Metadata:")
                for key, value in event["metadata"].items():
                    lines.append(f"    {key}: {value}")

        # Exception info
        if self.include_stack_trace and record.exc_info:
            lines.append("")
            lines.append("Exception:")
            import traceback
            lines.extend(
                "  " + line
                for line in traceback.format_exception(*record.exc_info)
            )

        if self.include_separator:
            lines.append(self.SEPARATOR)

        return "\n".join(lines)


class ColoredAuditFormatter(logging.Formatter):
    """Colored formatter for terminal output."""

    COLORS = {
        "DEBUG": "\033[36m",     # Cyan
        "INFO": "\033[32m",      # Green
        "WARNING": "\033[33m",   # Yellow
        "ERROR": "\033[31m",     # Red
        "CRITICAL": "\033[35m",  # Magenta
    }
    RESET = "\033[0m"
    BOLD = "\033[1m"

    def __init__(
        self,
        fmt: Optional[str] = None,
        datefmt: Optional[str] = None,
        use_colors: bool = True,
    ):
        super().__init__(fmt=fmt, datefmt=datefmt)
        self.use_colors = use_colors

    def format(self, record: logging.LogRecord) -> str:
        """Format with colors."""
        # Get the color for this level
        color = self.COLORS.get(record.levelname, "")
        reset = self.RESET if color else ""

        # Format the timestamp
        timestamp = datetime.utcnow().strftime("%H:%M:%S")

        # Build the message
        level = record.levelname[:4]
        if self.use_colors:
            level = f"{color}{level}{reset}"

        # Get audit event info
        event = getattr(record, "audit_event", None)
        event_info = ""
        if event:
            success = event.get("success", True)
            status = "✓" if success else "✗"
            if self.use_colors:
                status_color = "\033[32m" if success else "\033[31m"
                status = f"{status_color}{status}{reset}"
            
            duration = event.get("duration_ms", 0)
            validator = event.get("validator_name", "?")
            event_info = f" [{status}] {validator} ({duration:.1f}ms)"

        return f"{timestamp} {level} {record.getMessage()}{event_info}"


class CSVAuditFormatter(logging.Formatter):
    """CSV formatter for audit logs (useful for analysis)."""

    FIELDS = [
        "timestamp",
        "level",
        "event_type",
        "validator_name",
        "success",
        "duration_ms",
        "issues_count",
        "error_message",
        "correlation_id",
        "field_path",
    ]

    def __init__(self, delimiter: str = ",", include_header: bool = False):
        super().__init__()
        self.delimiter = delimiter
        self.include_header = include_header
        self._header_written = False

    def format(self, record: logging.LogRecord) -> str:
        """Format the record as CSV."""
        event = getattr(record, "audit_event", None)

        values = {
            "timestamp": datetime.utcnow().isoformat(),
            "level": record.levelname,
        }

        if event:
            values.update({
                "event_type": event.get("event_type", ""),
                "validator_name": event.get("validator_name", ""),
                "success": str(event.get("success", "")),
                "duration_ms": str(event.get("duration_ms", "")),
                "issues_count": str(event.get("issues_count", "")),
                "error_message": self._escape_csv(event.get("error_message", "")),
                "correlation_id": event.get("correlation_id", ""),
                "field_path": event.get("field_path", ""),
            })

        row = self.delimiter.join(
            values.get(field, "") for field in self.FIELDS
        )

        if self.include_header and not self._header_written:
            header = self.delimiter.join(self.FIELDS)
            self._header_written = True
            return f"{header}\n{row}"

        return row

    def _escape_csv(self, value: str) -> str:
        """Escape a value for CSV."""
        if not value:
            return ""
        if self.delimiter in value or '"' in value or "\n" in value:
            return '"' + value.replace('"', '""') + '"'
        return value
