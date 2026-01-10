"""Base validation classes and core abstractions."""

from __future__ import annotations

import asyncio
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime
from enum import Enum
from typing import (
    TYPE_CHECKING,
    Any,
    Callable,
    Dict,
    Generic,
    List,
    Optional,
    Set,
    TypeVar,
    Union,
)
from uuid import UUID, uuid4

if TYPE_CHECKING:
    from validation_infrastructure.errors import ValidationError


class ValidationSeverity(Enum):
    """Severity levels for validation issues."""

    INFO = "info"
    WARNING = "warning"
    ERROR = "error"
    CRITICAL = "critical"


class ValidationStatus(Enum):
    """Status of a validation operation."""

    PENDING = "pending"
    RUNNING = "running"
    PASSED = "passed"
    FAILED = "failed"
    SKIPPED = "skipped"


T = TypeVar("T")
V = TypeVar("V")


@dataclass
class ValidationIssue:
    """Represents a single validation issue."""

    message: str
    field: Optional[str] = None
    code: Optional[str] = None
    severity: ValidationSeverity = ValidationSeverity.ERROR
    path: Optional[str] = None
    value: Any = None
    context: Dict[str, Any] = field(default_factory=dict)
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert issue to dictionary representation."""
        return {
            "message": self.message,
            "field": self.field,
            "code": self.code,
            "severity": self.severity.value,
            "path": self.path,
            "value": repr(self.value) if self.value is not None else None,
            "context": self.context,
        }


@dataclass
class ValidationResult(Generic[T]):
    """Result of a validation operation."""

    is_valid: bool
    value: Optional[T] = None
    issues: List[ValidationIssue] = field(default_factory=list)
    warnings: List[ValidationIssue] = field(default_factory=list)
    metadata: Dict[str, Any] = field(default_factory=dict)
    execution_time_ms: Optional[float] = None
    validator_name: Optional[str] = None
    timestamp: datetime = field(default_factory=datetime.utcnow)
    
    @classmethod
    def success(
        cls,
        value: T,
        warnings: Optional[List[ValidationIssue]] = None,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> ValidationResult[T]:
        """Create a successful validation result."""
        return cls(
            is_valid=True,
            value=value,
            warnings=warnings or [],
            metadata=metadata or {},
        )
    
    @classmethod
    def failure(
        cls,
        issues: List[ValidationIssue],
        value: Optional[T] = None,
        metadata: Optional[Dict[str, Any]] = None,
    ) -> ValidationResult[T]:
        """Create a failed validation result."""
        return cls(
            is_valid=False,
            value=value,
            issues=issues,
            metadata=metadata or {},
        )
    
    @classmethod
    def from_error(
        cls,
        message: str,
        field: Optional[str] = None,
        code: Optional[str] = None,
        value: Optional[T] = None,
    ) -> ValidationResult[T]:
        """Create a failed result from a single error."""
        issue = ValidationIssue(
            message=message,
            field=field,
            code=code,
            value=value,
        )
        return cls.failure([issue], value=value)
    
    def add_issue(self, issue: ValidationIssue) -> None:
        """Add an issue to this result."""
        if issue.severity == ValidationSeverity.WARNING:
            self.warnings.append(issue)
        else:
            self.issues.append(issue)
            self.is_valid = False
    
    def merge(self, other: ValidationResult[Any]) -> ValidationResult[T]:
        """Merge another result into this one."""
        self.issues.extend(other.issues)
        self.warnings.extend(other.warnings)
        self.metadata.update(other.metadata)
        if not other.is_valid:
            self.is_valid = False
        return self
    
    def to_dict(self) -> Dict[str, Any]:
        """Convert result to dictionary representation."""
        return {
            "is_valid": self.is_valid,
            "value": repr(self.value) if self.value is not None else None,
            "issues": [issue.to_dict() for issue in self.issues],
            "warnings": [warning.to_dict() for warning in self.warnings],
            "metadata": self.metadata,
            "execution_time_ms": self.execution_time_ms,
            "validator_name": self.validator_name,
            "timestamp": self.timestamp.isoformat(),
        }
    
    @property
    def error_messages(self) -> List[str]:
        """Get list of error messages."""
        return [issue.message for issue in self.issues]
    
    @property
    def warning_messages(self) -> List[str]:
        """Get list of warning messages."""
        return [warning.message for warning in self.warnings]


@dataclass
class ValidationContext:
    """Context for validation operations with shared state."""

    id: UUID = field(default_factory=uuid4)
    path: List[str] = field(default_factory=list)
    depth: int = 0
    max_depth: int = 50
    strict_mode: bool = False
    collect_all_errors: bool = True
    locale: str = "en"
    timezone: Optional[str] = None
    metadata: Dict[str, Any] = field(default_factory=dict)
    parent: Optional[ValidationContext] = None
    _seen_objects: Set[int] = field(default_factory=set)
    
    def child(
        self,
        field_name: str,
        increment_depth: bool = True,
    ) -> ValidationContext:
        """Create a child context for nested validation."""
        new_path = self.path + [field_name]
        new_depth = self.depth + 1 if increment_depth else self.depth
        
        if new_depth > self.max_depth:
            raise RecursionError(
                f"Maximum validation depth ({self.max_depth}) exceeded at path: "
                f"{'.'.join(new_path)}"
            )
        
        return ValidationContext(
            id=self.id,
            path=new_path,
            depth=new_depth,
            max_depth=self.max_depth,
            strict_mode=self.strict_mode,
            collect_all_errors=self.collect_all_errors,
            locale=self.locale,
            timezone=self.timezone,
            metadata=self.metadata.copy(),
            parent=self,
            _seen_objects=self._seen_objects,
        )
    
    @property
    def current_path(self) -> str:
        """Get current path as dot-separated string."""
        return ".".join(self.path) if self.path else "<root>"
    
    def has_seen(self, obj: Any) -> bool:
        """Check if object has been seen (for circular reference detection)."""
        obj_id = id(obj)
        if obj_id in self._seen_objects:
            return True
        self._seen_objects.add(obj_id)
        return False
    
    def get_metadata(self, key: str, default: Any = None) -> Any:
        """Get metadata value with optional default."""
        return self.metadata.get(key, default)
    
    def set_metadata(self, key: str, value: Any) -> None:
        """Set metadata value."""
        self.metadata[key] = value


class BaseValidator(ABC, Generic[T]):
    """Abstract base class for all validators."""

    def __init__(
        self,
        name: Optional[str] = None,
        error_code: Optional[str] = None,
        error_message: Optional[str] = None,
    ):
        self.name = name or self.__class__.__name__
        self.error_code = error_code
        self.error_message = error_message
        self._pre_hooks: List[Callable[[Any, ValidationContext], Any]] = []
        self._post_hooks: List[Callable[[ValidationResult[T]], ValidationResult[T]]] = []
    
    @abstractmethod
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Validate the given value synchronously."""
        ...
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Validate the given value asynchronously.
        
        Default implementation runs sync validation in executor.
        Override for true async validation.
        """
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(
            None,
            self.validate,
            value,
            context,
        )
    
    def __call__(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Make validator callable."""
        return self.validate(value, context)
    
    def add_pre_hook(
        self,
        hook: Callable[[Any, ValidationContext], Any],
    ) -> BaseValidator[T]:
        """Add a pre-validation hook."""
        self._pre_hooks.append(hook)
        return self
    
    def add_post_hook(
        self,
        hook: Callable[[ValidationResult[T]], ValidationResult[T]],
    ) -> BaseValidator[T]:
        """Add a post-validation hook."""
        self._post_hooks.append(hook)
        return self
    
    def _run_pre_hooks(
        self,
        value: Any,
        context: ValidationContext,
    ) -> Any:
        """Run all pre-validation hooks."""
        for hook in self._pre_hooks:
            value = hook(value, context)
        return value
    
    def _run_post_hooks(
        self,
        result: ValidationResult[T],
    ) -> ValidationResult[T]:
        """Run all post-validation hooks."""
        for hook in self._post_hooks:
            result = hook(result)
        return result
    
    def _create_context(
        self,
        context: Optional[ValidationContext] = None,
    ) -> ValidationContext:
        """Create or use existing context."""
        return context or ValidationContext()
    
    def _create_issue(
        self,
        message: str,
        context: ValidationContext,
        value: Any = None,
        code: Optional[str] = None,
        field: Optional[str] = None,
        severity: ValidationSeverity = ValidationSeverity.ERROR,
    ) -> ValidationIssue:
        """Create a validation issue with context."""
        return ValidationIssue(
            message=message,
            field=field or (context.path[-1] if context.path else None),
            code=code or self.error_code,
            severity=severity,
            path=context.current_path,
            value=value,
            context={"validator": self.name},
        )
    
    def and_then(self, other: BaseValidator[V]) -> ChainedValidator[V]:
        """Chain this validator with another."""
        return ChainedValidator([self, other])
    
    def or_else(self, other: BaseValidator[T]) -> UnionValidator[T]:
        """Create union of this validator with another."""
        return UnionValidator([self, other])
    
    def with_transform(
        self,
        transform: Callable[[T], V],
    ) -> TransformValidator[T, V]:
        """Apply transformation after successful validation."""
        return TransformValidator(self, transform)


class ChainedValidator(BaseValidator[T]):
    """Chains multiple validators in sequence."""

    def __init__(self, validators: List[BaseValidator[Any]]):
        super().__init__(name="ChainedValidator")
        self.validators = validators
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        current_value = value
        combined_result: ValidationResult[Any] = ValidationResult.success(value)
        
        for validator in self.validators:
            result = validator.validate(current_value, ctx)
            combined_result.merge(result)
            
            if not result.is_valid and not ctx.collect_all_errors:
                break
            
            if result.value is not None:
                current_value = result.value
        
        combined_result.value = current_value
        return combined_result  # type: ignore


class UnionValidator(BaseValidator[T]):
    """Validates if any of the validators pass."""

    def __init__(self, validators: List[BaseValidator[T]]):
        super().__init__(name="UnionValidator")
        self.validators = validators
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        all_issues: List[ValidationIssue] = []
        
        for validator in self.validators:
            result = validator.validate(value, ctx)
            if result.is_valid:
                return result
            all_issues.extend(result.issues)
        
        return ValidationResult.failure(
            issues=[
                self._create_issue(
                    f"Value failed all {len(self.validators)} validators",
                    ctx,
                    value=value,
                    code="union_validation_failed",
                )
            ] + all_issues,
            value=value,
        )


class TransformValidator(BaseValidator[V], Generic[T, V]):
    """Applies transformation after validation."""

    def __init__(
        self,
        validator: BaseValidator[T],
        transform: Callable[[T], V],
    ):
        super().__init__(name="TransformValidator")
        self.validator = validator
        self.transform = transform
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[V]:
        ctx = self._create_context(context)
        result = self.validator.validate(value, ctx)
        
        if not result.is_valid:
            return ValidationResult.failure(result.issues)
        
        try:
            transformed = self.transform(result.value)
            return ValidationResult.success(transformed, warnings=result.warnings)
        except Exception as e:
            return ValidationResult.from_error(
                f"Transformation failed: {str(e)}",
                code="transform_error",
                value=value,
            )


class CompositeValidator(BaseValidator[Dict[str, Any]]):
    """Validates multiple fields with different validators."""

    def __init__(
        self,
        field_validators: Dict[str, BaseValidator[Any]],
        allow_extra: bool = False,
        require_all: bool = True,
    ):
        super().__init__(name="CompositeValidator")
        self.field_validators = field_validators
        self.allow_extra = allow_extra
        self.require_all = require_all
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Dict[str, Any]]:
        ctx = self._create_context(context)
        
        if not isinstance(value, dict):
            return ValidationResult.from_error(
                f"Expected dict, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        result: ValidationResult[Dict[str, Any]] = ValidationResult.success({})
        validated_data: Dict[str, Any] = {}
        
        # Check required fields
        if self.require_all:
            missing_fields = set(self.field_validators.keys()) - set(value.keys())
            for field_name in missing_fields:
                result.add_issue(
                    self._create_issue(
                        f"Missing required field: {field_name}",
                        ctx,
                        code="missing_field",
                        field=field_name,
                    )
                )
        
        # Validate each field
        for field_name, validator in self.field_validators.items():
            if field_name not in value:
                continue
            
            field_ctx = ctx.child(field_name)
            field_result = validator.validate(value[field_name], field_ctx)
            result.merge(field_result)
            
            if field_result.is_valid:
                validated_data[field_name] = field_result.value
        
        # Check for extra fields
        if not self.allow_extra:
            extra_fields = set(value.keys()) - set(self.field_validators.keys())
            for field_name in extra_fields:
                result.add_issue(
                    self._create_issue(
                        f"Unknown field: {field_name}",
                        ctx,
                        code="extra_field",
                        field=field_name,
                        severity=ValidationSeverity.WARNING,
                    )
                )
        else:
            # Include extra fields in output
            for field_name in value:
                if field_name not in self.field_validators:
                    validated_data[field_name] = value[field_name]
        
        result.value = validated_data
        return result


class ConditionalValidator(BaseValidator[T]):
    """Validates based on a condition."""

    def __init__(
        self,
        condition: Callable[[Any, ValidationContext], bool],
        true_validator: BaseValidator[T],
        false_validator: Optional[BaseValidator[T]] = None,
    ):
        super().__init__(name="ConditionalValidator")
        self.condition = condition
        self.true_validator = true_validator
        self.false_validator = false_validator
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        
        if self.condition(value, ctx):
            return self.true_validator.validate(value, ctx)
        elif self.false_validator:
            return self.false_validator.validate(value, ctx)
        else:
            return ValidationResult.success(value)


class CachedValidator(BaseValidator[T]):
    """Caches validation results for repeated values."""

    def __init__(
        self,
        validator: BaseValidator[T],
        maxsize: int = 1000,
        ttl_seconds: Optional[float] = None,
    ):
        super().__init__(name="CachedValidator")
        self.validator = validator
        self.maxsize = maxsize
        self.ttl_seconds = ttl_seconds
        self._cache: Dict[int, tuple[ValidationResult[T], float]] = {}
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        import time
        
        ctx = self._create_context(context)
        
        try:
            cache_key = hash(value)
        except TypeError:
            # Unhashable value, skip caching
            return self.validator.validate(value, ctx)
        
        current_time = time.time()
        
        if cache_key in self._cache:
            cached_result, cached_time = self._cache[cache_key]
            if self.ttl_seconds is None or (current_time - cached_time) < self.ttl_seconds:
                return cached_result
        
        # Evict old entries if cache is full
        if len(self._cache) >= self.maxsize:
            oldest_key = min(self._cache.keys(), key=lambda k: self._cache[k][1])
            del self._cache[oldest_key]
        
        result = self.validator.validate(value, ctx)
        self._cache[cache_key] = (result, current_time)
        return result
    
    def clear_cache(self) -> None:
        """Clear the validation cache."""
        self._cache.clear()


class LazyValidator(BaseValidator[T]):
    """Lazily creates validator on first use."""

    def __init__(self, factory: Callable[[], BaseValidator[T]]):
        super().__init__(name="LazyValidator")
        self.factory = factory
        self._validator: Optional[BaseValidator[T]] = None
    
    @property
    def validator(self) -> BaseValidator[T]:
        """Get or create the underlying validator."""
        if self._validator is None:
            self._validator = self.factory()
        return self._validator
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        return self.validator.validate(value, context)
