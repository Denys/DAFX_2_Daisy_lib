"""Custom logging handlers for validation audit trails."""

from __future__ import annotations

import json
import logging
import os
import threading
from datetime import datetime
from logging.handlers import RotatingFileHandler, TimedRotatingFileHandler
from pathlib import Path
from queue import Queue
from typing import Any, Dict, List, Optional


class ValidationLogHandler(logging.Handler):
    """Base handler for validation logs."""

    def __init__(self, level: int = logging.INFO):
        super().__init__(level)
        self._events: List[Dict[str, Any]] = []
        self._max_events = 10000
        self._lock = threading.Lock()

    def emit(self, record: logging.LogRecord) -> None:
        """Handle a log record."""
        try:
            event = getattr(record, "audit_event", None)
            if event:
                with self._lock:
                    self._events.append(event)
                    if len(self._events) > self._max_events:
                        self._events = self._events[-self._max_events:]
        except Exception:
            self.handleError(record)

    def get_events(
        self,
        start_time: Optional[datetime] = None,
        end_time: Optional[datetime] = None,
        event_types: Optional[List[str]] = None,
        success_only: Optional[bool] = None,
    ) -> List[Dict[str, Any]]:
        """Get filtered events."""
        with self._lock:
            events = self._events.copy()

        filtered = []
        for event in events:
            # Time filters
            if start_time:
                event_time = datetime.fromisoformat(event.get("timestamp", ""))
                if event_time < start_time:
                    continue
            if end_time:
                event_time = datetime.fromisoformat(event.get("timestamp", ""))
                if event_time > end_time:
                    continue

            # Event type filter
            if event_types and event.get("event_type") not in event_types:
                continue

            # Success filter
            if success_only is not None and event.get("success") != success_only:
                continue

            filtered.append(event)

        return filtered

    def clear(self) -> None:
        """Clear stored events."""
        with self._lock:
            self._events.clear()


class FileAuditHandler(logging.FileHandler):
    """File handler for audit logs with structured output."""

    def __init__(
        self,
        filename: str,
        mode: str = "a",
        encoding: str = "utf-8",
        delay: bool = False,
    ):
        # Ensure directory exists
        Path(filename).parent.mkdir(parents=True, exist_ok=True)
        super().__init__(filename, mode, encoding, delay)

    def emit(self, record: logging.LogRecord) -> None:
        """Emit a record with audit event data."""
        try:
            event = getattr(record, "audit_event", None)
            if event:
                # Write JSON line
                msg = json.dumps(event, default=str) + "\n"
                self.stream.write(msg)
                self.flush()
            else:
                super().emit(record)
        except Exception:
            self.handleError(record)


class JSONLogHandler(logging.Handler):
    """Handler that outputs JSON-formatted logs."""

    def __init__(
        self,
        stream: Optional[Any] = None,
        level: int = logging.INFO,
        indent: Optional[int] = None,
    ):
        super().__init__(level)
        self.stream = stream or os.sys.stdout
        self.indent = indent
        self._lock = threading.Lock()

    def emit(self, record: logging.LogRecord) -> None:
        """Emit a JSON-formatted record."""
        try:
            event = getattr(record, "audit_event", None)

            log_data = {
                "timestamp": datetime.utcnow().isoformat(),
                "level": record.levelname,
                "logger": record.name,
                "message": record.getMessage(),
            }

            if event:
                log_data["audit_event"] = event

            # Add extra fields
            for key in ["correlation_id", "user_id", "request_id"]:
                if hasattr(record, key):
                    log_data[key] = getattr(record, key)

            with self._lock:
                self.stream.write(
                    json.dumps(log_data, indent=self.indent, default=str) + "\n"
                )
                self.stream.flush()

        except Exception:
            self.handleError(record)


class RotatingAuditHandler(RotatingFileHandler):
    """Rotating file handler for audit logs."""

    def __init__(
        self,
        filename: str,
        mode: str = "a",
        maxBytes: int = 10 * 1024 * 1024,  # 10 MB
        backupCount: int = 5,
        encoding: str = "utf-8",
        delay: bool = False,
    ):
        # Ensure directory exists
        Path(filename).parent.mkdir(parents=True, exist_ok=True)
        super().__init__(filename, mode, maxBytes, backupCount, encoding, delay)

    def emit(self, record: logging.LogRecord) -> None:
        """Emit a record with JSON formatting for audit events."""
        try:
            event = getattr(record, "audit_event", None)
            if event:
                # Create a copy of record with JSON message
                record.msg = json.dumps(event, default=str)
                record.args = ()
            super().emit(record)
        except Exception:
            self.handleError(record)


class TimedAuditHandler(TimedRotatingFileHandler):
    """Time-based rotating handler for audit logs."""

    def __init__(
        self,
        filename: str,
        when: str = "midnight",
        interval: int = 1,
        backupCount: int = 30,
        encoding: str = "utf-8",
        delay: bool = False,
        utc: bool = True,
    ):
        # Ensure directory exists
        Path(filename).parent.mkdir(parents=True, exist_ok=True)
        super().__init__(filename, when, interval, backupCount, encoding, delay, utc)

    def emit(self, record: logging.LogRecord) -> None:
        """Emit a record with JSON formatting."""
        try:
            event = getattr(record, "audit_event", None)
            if event:
                record.msg = json.dumps(event, default=str)
                record.args = ()
            super().emit(record)
        except Exception:
            self.handleError(record)


class AsyncAuditHandler(logging.Handler):
    """Asynchronous handler that queues events for background processing."""

    def __init__(
        self,
        handler: logging.Handler,
        queue_size: int = 10000,
    ):
        super().__init__()
        self._handler = handler
        self._queue: Queue[Optional[logging.LogRecord]] = Queue(maxsize=queue_size)
        self._thread: Optional[threading.Thread] = None
        self._shutdown = False
        self._start_thread()

    def _start_thread(self) -> None:
        """Start the background processing thread."""
        self._thread = threading.Thread(target=self._process_queue, daemon=True)
        self._thread.start()

    def _process_queue(self) -> None:
        """Process queued records."""
        while not self._shutdown:
            try:
                record = self._queue.get(timeout=1.0)
                if record is None:
                    break
                self._handler.emit(record)
                self._queue.task_done()
            except Exception:
                pass

    def emit(self, record: logging.LogRecord) -> None:
        """Queue a record for async processing."""
        try:
            self._queue.put_nowait(record)
        except Exception:
            # Queue is full, drop the record
            pass

    def close(self) -> None:
        """Close the handler and stop the processing thread."""
        self._shutdown = True
        self._queue.put(None)  # Signal thread to stop
        if self._thread:
            self._thread.join(timeout=5.0)
        self._handler.close()
        super().close()


class BufferedAuditHandler(logging.Handler):
    """Handler that buffers events and flushes periodically."""

    def __init__(
        self,
        handler: logging.Handler,
        capacity: int = 100,
        flush_interval: float = 5.0,
    ):
        super().__init__()
        self._handler = handler
        self._capacity = capacity
        self._flush_interval = flush_interval
        self._buffer: List[logging.LogRecord] = []
        self._lock = threading.Lock()
        self._timer: Optional[threading.Timer] = None
        self._start_timer()

    def _start_timer(self) -> None:
        """Start the flush timer."""
        self._timer = threading.Timer(self._flush_interval, self._flush_timer)
        self._timer.daemon = True
        self._timer.start()

    def _flush_timer(self) -> None:
        """Timer callback to flush buffer."""
        self.flush()
        self._start_timer()

    def emit(self, record: logging.LogRecord) -> None:
        """Buffer a record."""
        with self._lock:
            self._buffer.append(record)
            if len(self._buffer) >= self._capacity:
                self._flush_buffer()

    def _flush_buffer(self) -> None:
        """Flush buffered records to the underlying handler."""
        for record in self._buffer:
            self._handler.emit(record)
        self._buffer.clear()

    def flush(self) -> None:
        """Flush the buffer."""
        with self._lock:
            self._flush_buffer()
        self._handler.flush()

    def close(self) -> None:
        """Close the handler."""
        if self._timer:
            self._timer.cancel()
        self.flush()
        self._handler.close()
        super().close()
