"""Validation exception classes."""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Any, Dict, List, Optional, Sequence


@dataclass
class ValidationErrorDetail:
    """Detailed information about a single validation error."""

    message: str
    code: str
    field: Optional[str] = None
    path: Optional[str] = None
    value: Any = None
    context: Dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary representation."""
        return {
            "message": self.message,
            "code": self.code,
            "field": self.field,
            "path": self.path,
            "value": repr(self.value) if self.value is not None else None,
            "context": self.context,
        }
    
    def __str__(self) -> str:
        if self.field:
            return f"[{self.field}] {self.message}"
        return self.message


class ValidationError(Exception):
    """Base validation exception."""

    def __init__(
        self,
        message: str,
        code: str = "validation_error",
        field: Optional[str] = None,
        path: Optional[str] = None,
        value: Any = None,
        details: Optional[List[ValidationErrorDetail]] = None,
        context: Optional[Dict[str, Any]] = None,
    ):
        super().__init__(message)
        self.message = message
        self.code = code
        self.field = field
        self.path = path
        self.value = value
        self.details = details or []
        self.context = context or {}
    
    def add_detail(self, detail: ValidationErrorDetail) -> None:
        """Add a detail to this error."""
        self.details.append(detail)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary representation."""
        return {
            "message": self.message,
            "code": self.code,
            "field": self.field,
            "path": self.path,
            "value": repr(self.value) if self.value is not None else None,
            "details": [d.to_dict() for d in self.details],
            "context": self.context,
        }
    
    @property
    def error_count(self) -> int:
        """Get total number of errors including details."""
        return 1 + len(self.details)
    
    def __str__(self) -> str:
        if self.field:
            base = f"[{self.field}] {self.message}"
        else:
            base = self.message
        
        if self.details:
            detail_strs = [f"  - {d}" for d in self.details]
            return f"{base}\nDetails:\n" + "\n".join(detail_strs)
        
        return base


class ValidationErrorCollection(Exception):
    """Collection of multiple validation errors."""

    def __init__(
        self,
        errors: Optional[Sequence[ValidationError]] = None,
        message: str = "Multiple validation errors occurred",
    ):
        super().__init__(message)
        self.message = message
        self._errors: List[ValidationError] = list(errors) if errors else []
    
    def add(self, error: ValidationError) -> None:
        """Add an error to the collection."""
        self._errors.append(error)
    
    def extend(self, errors: Sequence[ValidationError]) -> None:
        """Add multiple errors to the collection."""
        self._errors.extend(errors)
    
    @property
    def errors(self) -> List[ValidationError]:
        """Get all errors."""
        return self._errors.copy()
    
    @property
    def error_count(self) -> int:
        """Get total number of errors."""
        return sum(e.error_count for e in self._errors)
    
    def is_empty(self) -> bool:
        """Check if collection has no errors."""
        return len(self._errors) == 0
    
    def by_field(self) -> Dict[Optional[str], List[ValidationError]]:
        """Group errors by field."""
        result: Dict[Optional[str], List[ValidationError]] = {}
        for error in self._errors:
            if error.field not in result:
                result[error.field] = []
            result[error.field].append(error)
        return result
    
    def by_code(self) -> Dict[str, List[ValidationError]]:
        """Group errors by code."""
        result: Dict[str, List[ValidationError]] = {}
        for error in self._errors:
            if error.code not in result:
                result[error.code] = []
            result[error.code].append(error)
        return result
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary representation."""
        return {
            "message": self.message,
            "error_count": self.error_count,
            "errors": [e.to_dict() for e in self._errors],
        }
    
    def raise_if_errors(self) -> None:
        """Raise this exception if there are any errors."""
        if not self.is_empty():
            raise self
    
    def __len__(self) -> int:
        return len(self._errors)
    
    def __iter__(self):
        return iter(self._errors)
    
    def __bool__(self) -> bool:
        return not self.is_empty()
    
    def __str__(self) -> str:
        if not self._errors:
            return "No validation errors"
        
        lines = [f"{self.message} ({self.error_count} total):"]
        for i, error in enumerate(self._errors, 1):
            lines.append(f"  {i}. {error}")
        
        return "\n".join(lines)


class AggregatedValidationError(ValidationErrorCollection):
    """
    Aggregated validation errors with support for nested paths.
    
    Provides hierarchical error organization and detailed reporting.
    """

    def __init__(
        self,
        errors: Optional[Sequence[ValidationError]] = None,
        message: str = "Validation failed",
        source: Optional[str] = None,
    ):
        super().__init__(errors, message)
        self.source = source
        self._warnings: List[ValidationError] = []
    
    def add_warning(self, warning: ValidationError) -> None:
        """Add a warning (non-fatal validation issue)."""
        self._warnings.append(warning)
    
    @property
    def warnings(self) -> List[ValidationError]:
        """Get all warnings."""
        return self._warnings.copy()
    
    @property
    def has_warnings(self) -> bool:
        """Check if there are warnings."""
        return len(self._warnings) > 0
    
    def by_path(self) -> Dict[str, List[ValidationError]]:
        """Group errors by path."""
        result: Dict[str, List[ValidationError]] = {}
        for error in self._errors:
            path = error.path or "<root>"
            if path not in result:
                result[path] = []
            result[path].append(error)
        return result
    
    def get_nested_structure(self) -> Dict[str, Any]:
        """Get errors organized in nested structure matching field paths."""
        result: Dict[str, Any] = {}
        
        for error in self._errors:
            path = error.path or ""
            parts = path.split(".") if path else []
            
            current = result
            for part in parts[:-1]:
                if part not in current:
                    current[part] = {}
                current = current[part]
            
            if parts:
                key = parts[-1]
            else:
                key = "_root"
            
            if key not in current:
                current[key] = []
            current[key].append(error.to_dict())
        
        return result
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert to dictionary representation."""
        base = super().to_dict()
        base["source"] = self.source
        base["warnings"] = [w.to_dict() for w in self._warnings]
        base["warning_count"] = len(self._warnings)
        return base
    
    def merge(self, other: AggregatedValidationError) -> AggregatedValidationError:
        """Merge another aggregated error into this one."""
        self.extend(other.errors)
        self._warnings.extend(other.warnings)
        return self
    
    def __str__(self) -> str:
        lines = [f"{self.message}"]
        
        if self.source:
            lines[0] += f" (source: {self.source})"
        
        if self._errors:
            lines.append(f"\nErrors ({len(self._errors)}):")
            for error in self._errors:
                lines.append(f"  • {error}")
        
        if self._warnings:
            lines.append(f"\nWarnings ({len(self._warnings)}):")
            for warning in self._warnings:
                lines.append(f"  ⚠ {warning}")
        
        return "\n".join(lines)


class TypeValidationError(ValidationError):
    """Type validation specific error."""

    def __init__(
        self,
        expected_type: type,
        actual_type: type,
        value: Any = None,
        field: Optional[str] = None,
        path: Optional[str] = None,
    ):
        message = f"Expected {expected_type.__name__}, got {actual_type.__name__}"
        super().__init__(
            message=message,
            code="type_error",
            field=field,
            path=path,
            value=value,
            context={"expected_type": expected_type.__name__, "actual_type": actual_type.__name__},
        )
        self.expected_type = expected_type
        self.actual_type = actual_type


class SchemaValidationError(ValidationError):
    """Schema validation specific error."""

    def __init__(
        self,
        schema_name: str,
        message: str,
        field: Optional[str] = None,
        path: Optional[str] = None,
        value: Any = None,
        schema_errors: Optional[List[Dict[str, Any]]] = None,
    ):
        super().__init__(
            message=message,
            code="schema_error",
            field=field,
            path=path,
            value=value,
            context={"schema_name": schema_name, "schema_errors": schema_errors or []},
        )
        self.schema_name = schema_name
        self.schema_errors = schema_errors or []


class ConstraintViolationError(ValidationError):
    """Constraint violation specific error."""

    def __init__(
        self,
        constraint_name: str,
        message: str,
        field: Optional[str] = None,
        path: Optional[str] = None,
        value: Any = None,
        constraint_params: Optional[Dict[str, Any]] = None,
    ):
        super().__init__(
            message=message,
            code="constraint_violation",
            field=field,
            path=path,
            value=value,
            context={"constraint_name": constraint_name, "constraint_params": constraint_params or {}},
        )
        self.constraint_name = constraint_name
        self.constraint_params = constraint_params or {}


class ConfigurationError(ValidationError):
    """Configuration validation specific error."""

    def __init__(
        self,
        config_file: str,
        message: str,
        line: Optional[int] = None,
        column: Optional[int] = None,
        field: Optional[str] = None,
    ):
        super().__init__(
            message=message,
            code="config_error",
            field=field,
            context={
                "config_file": config_file,
                "line": line,
                "column": column,
            },
        )
        self.config_file = config_file
        self.line = line
        self.column = column
    
    def __str__(self) -> str:
        location = f"{self.config_file}"
        if self.line is not None:
            location += f":{self.line}"
            if self.column is not None:
                location += f":{self.column}"
        
        return f"[{location}] {self.message}"


class FileValidationError(ValidationError):
    """File validation specific error."""

    def __init__(
        self,
        filename: str,
        message: str,
        file_size: Optional[int] = None,
        content_type: Optional[str] = None,
    ):
        super().__init__(
            message=message,
            code="file_error",
            context={
                "filename": filename,
                "file_size": file_size,
                "content_type": content_type,
            },
        )
        self.filename = filename
        self.file_size = file_size
        self.content_type = content_type


class EnvironmentValidationError(ValidationError):
    """Environment variable validation specific error."""

    def __init__(
        self,
        var_name: str,
        message: str,
        expected_type: Optional[str] = None,
        required: bool = False,
    ):
        super().__init__(
            message=message,
            code="env_error",
            field=var_name,
            context={
                "var_name": var_name,
                "expected_type": expected_type,
                "required": required,
            },
        )
        self.var_name = var_name
        self.expected_type = expected_type
        self.required = required
