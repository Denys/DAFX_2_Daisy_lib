"""Error formatters for various output formats."""

from __future__ import annotations

import json
from abc import ABC, abstractmethod
from dataclasses import dataclass
from typing import Any, Dict, List, Optional, Sequence, Union

from validation_infrastructure.errors.exceptions import (
    ValidationError,
    ValidationErrorCollection,
    AggregatedValidationError,
)


@dataclass
class FormatterOptions:
    """Options for error formatting."""

    include_value: bool = False
    include_context: bool = False
    include_path: bool = True
    include_code: bool = True
    max_value_length: int = 100
    indent: int = 2
    color_output: bool = True


class ErrorFormatter(ABC):
    """Abstract base class for error formatters."""

    def __init__(self, options: Optional[FormatterOptions] = None):
        self.options = options or FormatterOptions()
    
    @abstractmethod
    def format_error(self, error: ValidationError) -> str:
        """Format a single validation error."""
        ...
    
    @abstractmethod
    def format_collection(self, collection: ValidationErrorCollection) -> str:
        """Format a collection of validation errors."""
        ...
    
    def format(
        self,
        errors: Union[ValidationError, ValidationErrorCollection, Sequence[ValidationError]],
    ) -> str:
        """Format errors (single, collection, or sequence)."""
        if isinstance(errors, ValidationErrorCollection):
            return self.format_collection(errors)
        elif isinstance(errors, ValidationError):
            return self.format_error(errors)
        else:
            collection = ValidationErrorCollection(list(errors))
            return self.format_collection(collection)
    
    def _truncate_value(self, value: Any) -> str:
        """Truncate value representation if too long."""
        value_str = repr(value)
        if len(value_str) > self.options.max_value_length:
            return value_str[: self.options.max_value_length - 3] + "..."
        return value_str


class JSONErrorFormatter(ErrorFormatter):
    """Formats errors as JSON."""

    def format_error(self, error: ValidationError) -> str:
        """Format a single error as JSON."""
        data = self._error_to_dict(error)
        return json.dumps(data, indent=self.options.indent, default=str)
    
    def format_collection(self, collection: ValidationErrorCollection) -> str:
        """Format a collection of errors as JSON."""
        if isinstance(collection, AggregatedValidationError):
            data = {
                "success": False,
                "message": collection.message,
                "source": collection.source,
                "error_count": collection.error_count,
                "errors": [self._error_to_dict(e) for e in collection.errors],
                "warning_count": len(collection.warnings),
                "warnings": [self._error_to_dict(w) for w in collection.warnings],
            }
        else:
            data = {
                "success": False,
                "message": collection.message,
                "error_count": collection.error_count,
                "errors": [self._error_to_dict(e) for e in collection.errors],
            }
        
        return json.dumps(data, indent=self.options.indent, default=str)
    
    def _error_to_dict(self, error: ValidationError) -> Dict[str, Any]:
        """Convert error to dictionary for JSON serialization."""
        data: Dict[str, Any] = {
            "message": error.message,
        }
        
        if self.options.include_code and error.code:
            data["code"] = error.code
        
        if error.field:
            data["field"] = error.field
        
        if self.options.include_path and error.path:
            data["path"] = error.path
        
        if self.options.include_value and error.value is not None:
            data["value"] = self._truncate_value(error.value)
        
        if self.options.include_context and error.context:
            data["context"] = error.context
        
        if error.details:
            data["details"] = [
                {
                    "message": d.message,
                    "code": d.code,
                    "field": d.field,
                    "path": d.path,
                }
                for d in error.details
            ]
        
        return data


class TextErrorFormatter(ErrorFormatter):
    """Formats errors as human-readable text."""

    def __init__(self, options: Optional[FormatterOptions] = None):
        super().__init__(options)
        self._colors = {
            "red": "\033[91m",
            "yellow": "\033[93m",
            "green": "\033[92m",
            "blue": "\033[94m",
            "bold": "\033[1m",
            "reset": "\033[0m",
        }
    
    def _color(self, text: str, color: str) -> str:
        """Apply color to text if color output is enabled."""
        if not self.options.color_output:
            return text
        return f"{self._colors.get(color, '')}{text}{self._colors['reset']}"
    
    def format_error(self, error: ValidationError) -> str:
        """Format a single error as text."""
        lines: List[str] = []
        
        # Main error message
        prefix = ""
        if error.field:
            prefix = f"[{self._color(error.field, 'blue')}] "
        
        message = f"{prefix}{self._color(error.message, 'red')}"
        lines.append(message)
        
        # Additional info
        if self.options.include_code and error.code:
            lines.append(f"  Code: {error.code}")
        
        if self.options.include_path and error.path:
            lines.append(f"  Path: {error.path}")
        
        if self.options.include_value and error.value is not None:
            lines.append(f"  Value: {self._truncate_value(error.value)}")
        
        # Details
        if error.details:
            lines.append("  Details:")
            for detail in error.details:
                detail_line = f"    - {detail.message}"
                if detail.field:
                    detail_line = f"    - [{detail.field}] {detail.message}"
                lines.append(detail_line)
        
        return "\n".join(lines)
    
    def format_collection(self, collection: ValidationErrorCollection) -> str:
        """Format a collection of errors as text."""
        lines: List[str] = []
        
        # Header
        header = self._color(f"✗ {collection.message}", "bold")
        lines.append(header)
        lines.append(f"  Found {collection.error_count} error(s)")
        lines.append("")
        
        # Errors
        for i, error in enumerate(collection.errors, 1):
            lines.append(f"  {i}. {self.format_error(error)}")
            lines.append("")
        
        # Warnings for aggregated errors
        if isinstance(collection, AggregatedValidationError) and collection.warnings:
            lines.append(self._color("Warnings:", "yellow"))
            for warning in collection.warnings:
                lines.append(f"  ⚠ {warning.message}")
        
        return "\n".join(lines)


class HTMLErrorFormatter(ErrorFormatter):
    """Formats errors as HTML."""

    def format_error(self, error: ValidationError) -> str:
        """Format a single error as HTML."""
        html_parts: List[str] = []
        
        html_parts.append('<div class="validation-error">')
        
        # Field indicator
        if error.field:
            html_parts.append(f'  <span class="error-field">[{self._escape(error.field)}]</span>')
        
        # Message
        html_parts.append(f'  <span class="error-message">{self._escape(error.message)}</span>')
        
        # Metadata
        metadata: List[str] = []
        if self.options.include_code and error.code:
            metadata.append(f'<span class="error-code">Code: {self._escape(error.code)}</span>')
        if self.options.include_path and error.path:
            metadata.append(f'<span class="error-path">Path: {self._escape(error.path)}</span>')
        if self.options.include_value and error.value is not None:
            metadata.append(
                f'<span class="error-value">Value: {self._escape(self._truncate_value(error.value))}</span>'
            )
        
        if metadata:
            html_parts.append(f'  <div class="error-metadata">{" | ".join(metadata)}</div>')
        
        # Details
        if error.details:
            html_parts.append('  <ul class="error-details">')
            for detail in error.details:
                html_parts.append(f'    <li>{self._escape(str(detail))}</li>')
            html_parts.append('  </ul>')
        
        html_parts.append('</div>')
        
        return "\n".join(html_parts)
    
    def format_collection(self, collection: ValidationErrorCollection) -> str:
        """Format a collection of errors as HTML."""
        html_parts: List[str] = []
        
        html_parts.append('<div class="validation-errors">')
        html_parts.append(f'  <h3 class="error-header">{self._escape(collection.message)}</h3>')
        html_parts.append(f'  <p class="error-count">Found {collection.error_count} error(s)</p>')
        
        html_parts.append('  <ol class="error-list">')
        for error in collection.errors:
            html_parts.append('    <li>')
            html_parts.append(self.format_error(error))
            html_parts.append('    </li>')
        html_parts.append('  </ol>')
        
        # Warnings
        if isinstance(collection, AggregatedValidationError) and collection.warnings:
            html_parts.append('  <div class="validation-warnings">')
            html_parts.append('    <h4>Warnings</h4>')
            html_parts.append('    <ul>')
            for warning in collection.warnings:
                html_parts.append(f'      <li class="warning">{self._escape(warning.message)}</li>')
            html_parts.append('    </ul>')
            html_parts.append('  </div>')
        
        html_parts.append('</div>')
        
        return "\n".join(html_parts)
    
    def get_css(self) -> str:
        """Get CSS styles for formatted HTML."""
        return """
<style>
.validation-errors {
    font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
    background: #fff5f5;
    border: 1px solid #fc8181;
    border-radius: 8px;
    padding: 16px;
    margin: 16px 0;
}

.error-header {
    color: #c53030;
    margin: 0 0 8px 0;
}

.error-count {
    color: #718096;
    font-size: 14px;
    margin: 0 0 16px 0;
}

.error-list {
    margin: 0;
    padding-left: 24px;
}

.validation-error {
    margin: 8px 0;
    padding: 8px;
    background: #fff;
    border-left: 3px solid #fc8181;
}

.error-field {
    color: #4299e1;
    font-weight: bold;
    margin-right: 8px;
}

.error-message {
    color: #c53030;
}

.error-metadata {
    font-size: 12px;
    color: #718096;
    margin-top: 4px;
}

.error-details {
    margin: 8px 0 0 0;
    padding-left: 20px;
    font-size: 13px;
}

.validation-warnings {
    margin-top: 16px;
    padding-top: 16px;
    border-top: 1px solid #ecc94b;
}

.validation-warnings h4 {
    color: #d69e2e;
    margin: 0 0 8px 0;
}

.warning {
    color: #744210;
}
</style>
"""
    
    def _escape(self, text: str) -> str:
        """Escape HTML special characters."""
        return (
            text.replace("&", "&amp;")
            .replace("<", "&lt;")
            .replace(">", "&gt;")
            .replace('"', "&quot;")
            .replace("'", "&#x27;")
        )


class MarkdownErrorFormatter(ErrorFormatter):
    """Formats errors as Markdown."""

    def format_error(self, error: ValidationError) -> str:
        """Format a single error as Markdown."""
        lines: List[str] = []
        
        # Main message
        if error.field:
            lines.append(f"**[{error.field}]** {error.message}")
        else:
            lines.append(f"**Error:** {error.message}")
        
        # Metadata
        if self.options.include_code and error.code:
            lines.append(f"- Code: `{error.code}`")
        if self.options.include_path and error.path:
            lines.append(f"- Path: `{error.path}`")
        if self.options.include_value and error.value is not None:
            lines.append(f"- Value: `{self._truncate_value(error.value)}`")
        
        # Details
        if error.details:
            lines.append("- Details:")
            for detail in error.details:
                lines.append(f"  - {detail}")
        
        return "\n".join(lines)
    
    def format_collection(self, collection: ValidationErrorCollection) -> str:
        """Format a collection of errors as Markdown."""
        lines: List[str] = []
        
        lines.append(f"## ❌ {collection.message}")
        lines.append(f"*Found {collection.error_count} error(s)*")
        lines.append("")
        
        for i, error in enumerate(collection.errors, 1):
            lines.append(f"### {i}. ")
            lines.append(self.format_error(error))
            lines.append("")
        
        if isinstance(collection, AggregatedValidationError) and collection.warnings:
            lines.append("## ⚠️ Warnings")
            for warning in collection.warnings:
                lines.append(f"- {warning.message}")
        
        return "\n".join(lines)


def get_formatter(
    format_type: str = "text",
    options: Optional[FormatterOptions] = None,
) -> ErrorFormatter:
    """Get a formatter by type name."""
    formatters = {
        "json": JSONErrorFormatter,
        "text": TextErrorFormatter,
        "html": HTMLErrorFormatter,
        "markdown": MarkdownErrorFormatter,
        "md": MarkdownErrorFormatter,
    }
    
    formatter_class = formatters.get(format_type.lower())
    if formatter_class is None:
        raise ValueError(f"Unknown formatter type: {format_type}. Available: {list(formatters.keys())}")
    
    return formatter_class(options)
