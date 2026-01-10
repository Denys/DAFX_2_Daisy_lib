"""Type validation and type checking utilities."""

from __future__ import annotations

import sys
from collections.abc import Callable, Mapping, Sequence
from datetime import date, datetime, time, timedelta
from decimal import Decimal
from enum import Enum
from pathlib import Path
from typing import (
    Any,
    Dict,
    ForwardRef,
    FrozenSet,
    Generic,
    List,
    Literal,
    Optional,
    Set,
    Tuple,
    Type,
    TypeVar,
    Union,
    get_args,
    get_origin,
)
from uuid import UUID

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
    ValidationSeverity,
)

T = TypeVar("T")

# Python version compatibility
if sys.version_info >= (3, 10):
    from types import NoneType, UnionType
else:
    NoneType = type(None)
    UnionType = None


def get_type_origin(tp: Any) -> Any:
    """Get the origin of a type, handling special cases."""
    origin = get_origin(tp)
    if origin is not None:
        return origin
    
    # Handle string annotations
    if isinstance(tp, str):
        return None
    
    # Handle forward references
    if isinstance(tp, ForwardRef):
        return None
    
    return None


def get_type_args(tp: Any) -> Tuple[Any, ...]:
    """Get type arguments, handling edge cases."""
    args = get_args(tp)
    return args if args else ()


def is_optional(tp: Any) -> bool:
    """Check if a type is Optional (Union with None)."""
    origin = get_type_origin(tp)
    
    if origin is Union:
        args = get_type_args(tp)
        return NoneType in args
    
    # Python 3.10+ X | None syntax
    if UnionType is not None and isinstance(tp, UnionType):
        args = get_type_args(tp)
        return NoneType in args
    
    return False


def unwrap_optional(tp: Any) -> Any:
    """Get the inner type of an Optional type."""
    if not is_optional(tp):
        return tp
    
    origin = get_type_origin(tp)
    if origin is Union or (UnionType is not None and isinstance(tp, UnionType)):
        args = get_type_args(tp)
        non_none_args = [arg for arg in args if arg is not NoneType]
        if len(non_none_args) == 1:
            return non_none_args[0]
        return Union[tuple(non_none_args)]
    
    return tp


def is_generic_type(tp: Any) -> bool:
    """Check if type is a generic type."""
    return get_type_origin(tp) is not None


def is_literal_type(tp: Any) -> bool:
    """Check if type is a Literal type."""
    return get_type_origin(tp) is Literal


def get_literal_values(tp: Any) -> Tuple[Any, ...]:
    """Get allowed values from a Literal type."""
    if is_literal_type(tp):
        return get_type_args(tp)
    return ()


# Type mapping for basic types
BASIC_TYPES: Dict[Type[Any], str] = {
    str: "string",
    int: "integer",
    float: "number",
    bool: "boolean",
    bytes: "bytes",
    NoneType: "null",
}

# Collection types with their expected item type position
COLLECTION_TYPES: Dict[Any, Tuple[str, int]] = {
    list: ("array", 0),
    List: ("array", 0),
    set: ("set", 0),
    Set: ("set", 0),
    frozenset: ("frozenset", 0),
    FrozenSet: ("frozenset", 0),
    tuple: ("tuple", -1),  # -1 means all args are item types
    Tuple: ("tuple", -1),
    dict: ("object", 1),  # value type at index 1
    Dict: ("object", 1),
    Mapping: ("mapping", 1),
    Sequence: ("sequence", 0),
}


class TypeValidator(BaseValidator[T], Generic[T]):
    """Validates that a value matches a specific type."""

    def __init__(
        self,
        expected_type: Type[T],
        coerce: bool = False,
        strict: bool = False,
        allow_none: bool = False,
    ):
        super().__init__(
            name=f"TypeValidator[{getattr(expected_type, '__name__', str(expected_type))}]",
            error_code="type_error",
        )
        self.expected_type = expected_type
        self.coerce = coerce
        self.strict = strict
        self.allow_none = allow_none
        self._type_origin = get_type_origin(expected_type)
        self._type_args = get_type_args(expected_type)
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        value = self._run_pre_hooks(value, ctx)
        
        # Handle None values
        if value is None:
            if self.allow_none or is_optional(self.expected_type):
                result = ValidationResult.success(None)  # type: ignore
                return self._run_post_hooks(result)
            return self._run_post_hooks(
                ValidationResult.from_error(
                    "Value cannot be None",
                    code="null_not_allowed",
                    value=value,
                )
            )
        
        # Validate the type
        result = self._validate_type(value, self.expected_type, ctx)
        return self._run_post_hooks(result)
    
    def _validate_type(
        self,
        value: Any,
        expected: Any,
        ctx: ValidationContext,
    ) -> ValidationResult[T]:
        """Internal type validation with full type support."""
        
        # Handle Any type
        if expected is Any:
            return ValidationResult.success(value)
        
        # Handle None/Optional
        if value is None:
            if expected is NoneType or is_optional(expected):
                return ValidationResult.success(None)
            return ValidationResult.from_error(
                "Value cannot be None",
                code="null_not_allowed",
                value=value,
            )
        
        # Handle Optional types - unwrap and validate inner type
        if is_optional(expected):
            inner_type = unwrap_optional(expected)
            return self._validate_type(value, inner_type, ctx)
        
        # Handle Literal types
        if is_literal_type(expected):
            allowed = get_literal_values(expected)
            if value not in allowed:
                return ValidationResult.from_error(
                    f"Value must be one of {allowed}, got {value!r}",
                    code="literal_error",
                    value=value,
                )
            return ValidationResult.success(value)
        
        # Handle Union types
        origin = get_type_origin(expected)
        if origin is Union or (UnionType is not None and isinstance(expected, UnionType)):
            args = get_type_args(expected)
            for arg in args:
                result = self._validate_type(value, arg, ctx)
                if result.is_valid:
                    return result
            type_names = [getattr(t, "__name__", str(t)) for t in args]
            return ValidationResult.from_error(
                f"Value does not match any type in Union[{', '.join(type_names)}]",
                code="union_error",
                value=value,
            )
        
        # Handle generic types (List, Dict, etc.)
        if origin is not None:
            return self._validate_generic_type(value, expected, origin, ctx)
        
        # Handle basic types
        return self._validate_basic_type(value, expected, ctx)
    
    def _validate_basic_type(
        self,
        value: Any,
        expected: Type[Any],
        ctx: ValidationContext,
    ) -> ValidationResult[T]:
        """Validate basic (non-generic) types."""
        
        # Direct type check
        if isinstance(value, expected):
            return ValidationResult.success(value)
        
        # Strict mode - no coercion allowed
        if self.strict:
            return ValidationResult.from_error(
                f"Expected {expected.__name__}, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        # Handle numeric type coercion
        if expected is float and isinstance(value, int):
            if self.coerce:
                return ValidationResult.success(float(value))
            return ValidationResult.success(value)  # int is acceptable for float
        
        # Coercion attempts
        if self.coerce:
            try:
                coerced = expected(value)
                result = ValidationResult.success(coerced)
                result.warnings.append(
                    ValidationIssue(
                        message=f"Value coerced from {type(value).__name__} to {expected.__name__}",
                        code="type_coerced",
                        severity=ValidationSeverity.WARNING,
                        value=value,
                    )
                )
                return result
            except (ValueError, TypeError):
                pass
        
        return ValidationResult.from_error(
            f"Expected {expected.__name__}, got {type(value).__name__}",
            code="type_error",
            value=value,
        )
    
    def _validate_generic_type(
        self,
        value: Any,
        expected: Any,
        origin: Any,
        ctx: ValidationContext,
    ) -> ValidationResult[T]:
        """Validate generic types like List[int], Dict[str, int], etc."""
        args = get_type_args(expected)
        
        # List/Set/Sequence validation
        if origin in (list, set, frozenset) or origin is Sequence:
            expected_container = list if origin is Sequence else origin
            if not isinstance(value, (list, set, frozenset, tuple)):
                return ValidationResult.from_error(
                    f"Expected {origin.__name__}, got {type(value).__name__}",
                    code="type_error",
                    value=value,
                )
            
            if args:
                item_type = args[0]
                validated_items = []
                issues: List[ValidationIssue] = []
                
                for i, item in enumerate(value):
                    item_ctx = ctx.child(str(i))
                    item_result = self._validate_type(item, item_type, item_ctx)
                    if item_result.is_valid:
                        validated_items.append(item_result.value)
                    else:
                        issues.extend(item_result.issues)
                        if not ctx.collect_all_errors:
                            break
                        validated_items.append(item)
                
                if issues:
                    return ValidationResult.failure(issues, value=value)
                
                return ValidationResult.success(expected_container(validated_items))
            
            return ValidationResult.success(expected_container(value))
        
        # Dict/Mapping validation
        if origin in (dict, Mapping):
            if not isinstance(value, dict):
                return ValidationResult.from_error(
                    f"Expected dict, got {type(value).__name__}",
                    code="type_error",
                    value=value,
                )
            
            if len(args) >= 2:
                key_type, value_type = args[0], args[1]
                validated_dict: Dict[Any, Any] = {}
                issues = []
                
                for k, v in value.items():
                    key_ctx = ctx.child(f"key({k})")
                    key_result = self._validate_type(k, key_type, key_ctx)
                    
                    value_ctx = ctx.child(str(k))
                    value_result = self._validate_type(v, value_type, value_ctx)
                    
                    if key_result.is_valid and value_result.is_valid:
                        validated_dict[key_result.value] = value_result.value
                    else:
                        issues.extend(key_result.issues)
                        issues.extend(value_result.issues)
                        if not ctx.collect_all_errors:
                            break
                
                if issues:
                    return ValidationResult.failure(issues, value=value)
                
                return ValidationResult.success(validated_dict)
            
            return ValidationResult.success(dict(value))
        
        # Tuple validation
        if origin is tuple:
            if not isinstance(value, tuple):
                if self.coerce and isinstance(value, (list, set)):
                    value = tuple(value)
                else:
                    return ValidationResult.from_error(
                        f"Expected tuple, got {type(value).__name__}",
                        code="type_error",
                        value=value,
                    )
            
            if args:
                # Check for variadic tuple (Tuple[int, ...])
                if len(args) == 2 and args[1] is ...:
                    item_type = args[0]
                    validated_items = []
                    issues = []
                    
                    for i, item in enumerate(value):
                        item_ctx = ctx.child(str(i))
                        item_result = self._validate_type(item, item_type, item_ctx)
                        if item_result.is_valid:
                            validated_items.append(item_result.value)
                        else:
                            issues.extend(item_result.issues)
                            validated_items.append(item)
                    
                    if issues:
                        return ValidationResult.failure(issues, value=value)
                    return ValidationResult.success(tuple(validated_items))
                
                # Fixed-length tuple
                if len(value) != len(args):
                    return ValidationResult.from_error(
                        f"Expected tuple of length {len(args)}, got length {len(value)}",
                        code="tuple_length_error",
                        value=value,
                    )
                
                validated_items = []
                issues = []
                
                for i, (item, item_type) in enumerate(zip(value, args)):
                    item_ctx = ctx.child(str(i))
                    item_result = self._validate_type(item, item_type, item_ctx)
                    if item_result.is_valid:
                        validated_items.append(item_result.value)
                    else:
                        issues.extend(item_result.issues)
                        validated_items.append(item)
                
                if issues:
                    return ValidationResult.failure(issues, value=value)
                return ValidationResult.success(tuple(validated_items))
            
            return ValidationResult.success(value)
        
        # Callable validation (basic check)
        if origin is Callable:
            if not callable(value):
                return ValidationResult.from_error(
                    f"Expected callable, got {type(value).__name__}",
                    code="type_error",
                    value=value,
                )
            return ValidationResult.success(value)
        
        # Type[X] validation
        if origin is type:
            if not isinstance(value, type):
                return ValidationResult.from_error(
                    f"Expected type, got {type(value).__name__}",
                    code="type_error",
                    value=value,
                )
            if args and not issubclass(value, args[0]):
                return ValidationResult.from_error(
                    f"Expected subclass of {args[0].__name__}, got {value.__name__}",
                    code="type_error",
                    value=value,
                )
            return ValidationResult.success(value)
        
        # Fallback to isinstance check with origin
        if isinstance(value, origin):
            return ValidationResult.success(value)
        
        return ValidationResult.from_error(
            f"Expected {origin.__name__}, got {type(value).__name__}",
            code="type_error",
            value=value,
        )


def validate_type(
    value: Any,
    expected_type: Type[T],
    coerce: bool = False,
    strict: bool = False,
    allow_none: bool = False,
    context: Optional[ValidationContext] = None,
) -> ValidationResult[T]:
    """Convenience function for type validation."""
    validator = TypeValidator(
        expected_type=expected_type,
        coerce=coerce,
        strict=strict,
        allow_none=allow_none,
    )
    return validator.validate(value, context)


class StringValidator(BaseValidator[str]):
    """Validates string values with various constraints."""

    def __init__(
        self,
        min_length: Optional[int] = None,
        max_length: Optional[int] = None,
        pattern: Optional[str] = None,
        strip_whitespace: bool = False,
        to_lower: bool = False,
        to_upper: bool = False,
        allow_empty: bool = True,
    ):
        super().__init__(name="StringValidator", error_code="string_error")
        self.min_length = min_length
        self.max_length = max_length
        self.pattern = pattern
        self.strip_whitespace = strip_whitespace
        self.to_lower = to_lower
        self.to_upper = to_upper
        self.allow_empty = allow_empty
        
        if pattern:
            import re
            self._compiled_pattern = re.compile(pattern)
        else:
            self._compiled_pattern = None
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        result_value = value
        
        # Apply transformations
        if self.strip_whitespace:
            result_value = result_value.strip()
        if self.to_lower:
            result_value = result_value.lower()
        if self.to_upper:
            result_value = result_value.upper()
        
        issues: List[ValidationIssue] = []
        
        # Empty check
        if not self.allow_empty and not result_value:
            issues.append(
                self._create_issue(
                    "String cannot be empty",
                    ctx,
                    value=result_value,
                    code="empty_string",
                )
            )
        
        # Length checks
        if self.min_length is not None and len(result_value) < self.min_length:
            issues.append(
                self._create_issue(
                    f"String length {len(result_value)} is less than minimum {self.min_length}",
                    ctx,
                    value=result_value,
                    code="string_too_short",
                )
            )
        
        if self.max_length is not None and len(result_value) > self.max_length:
            issues.append(
                self._create_issue(
                    f"String length {len(result_value)} exceeds maximum {self.max_length}",
                    ctx,
                    value=result_value,
                    code="string_too_long",
                )
            )
        
        # Pattern check
        if self._compiled_pattern and not self._compiled_pattern.match(result_value):
            issues.append(
                self._create_issue(
                    f"String does not match pattern: {self.pattern}",
                    ctx,
                    value=result_value,
                    code="pattern_mismatch",
                )
            )
        
        if issues:
            return ValidationResult.failure(issues, value=result_value)
        
        return ValidationResult.success(result_value)


class NumericValidator(BaseValidator[T], Generic[T]):
    """Validates numeric values with constraints."""

    def __init__(
        self,
        numeric_type: Type[T] = float,  # type: ignore
        minimum: Optional[float] = None,
        maximum: Optional[float] = None,
        exclusive_minimum: Optional[float] = None,
        exclusive_maximum: Optional[float] = None,
        multiple_of: Optional[float] = None,
        allow_nan: bool = False,
        allow_infinity: bool = False,
    ):
        super().__init__(
            name=f"NumericValidator[{numeric_type.__name__}]",
            error_code="numeric_error",
        )
        self.numeric_type = numeric_type
        self.minimum = minimum
        self.maximum = maximum
        self.exclusive_minimum = exclusive_minimum
        self.exclusive_maximum = exclusive_maximum
        self.multiple_of = multiple_of
        self.allow_nan = allow_nan
        self.allow_infinity = allow_infinity
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        
        if not isinstance(value, (int, float, Decimal)):
            return ValidationResult.from_error(
                f"Expected numeric type, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        # Convert to target type
        try:
            numeric_value = self.numeric_type(value)
        except (ValueError, TypeError) as e:
            return ValidationResult.from_error(
                f"Cannot convert to {self.numeric_type.__name__}: {e}",
                code="conversion_error",
                value=value,
            )
        
        issues: List[ValidationIssue] = []
        
        # Check for NaN
        if isinstance(numeric_value, float):
            import math
            if math.isnan(numeric_value) and not self.allow_nan:
                issues.append(
                    self._create_issue(
                        "NaN values are not allowed",
                        ctx,
                        value=numeric_value,
                        code="nan_not_allowed",
                    )
                )
            
            if math.isinf(numeric_value) and not self.allow_infinity:
                issues.append(
                    self._create_issue(
                        "Infinite values are not allowed",
                        ctx,
                        value=numeric_value,
                        code="infinity_not_allowed",
                    )
                )
        
        # Range checks
        if self.minimum is not None and numeric_value < self.minimum:
            issues.append(
                self._create_issue(
                    f"Value {numeric_value} is less than minimum {self.minimum}",
                    ctx,
                    value=numeric_value,
                    code="below_minimum",
                )
            )
        
        if self.maximum is not None and numeric_value > self.maximum:
            issues.append(
                self._create_issue(
                    f"Value {numeric_value} exceeds maximum {self.maximum}",
                    ctx,
                    value=numeric_value,
                    code="above_maximum",
                )
            )
        
        if self.exclusive_minimum is not None and numeric_value <= self.exclusive_minimum:
            issues.append(
                self._create_issue(
                    f"Value {numeric_value} must be greater than {self.exclusive_minimum}",
                    ctx,
                    value=numeric_value,
                    code="below_exclusive_minimum",
                )
            )
        
        if self.exclusive_maximum is not None and numeric_value >= self.exclusive_maximum:
            issues.append(
                self._create_issue(
                    f"Value {numeric_value} must be less than {self.exclusive_maximum}",
                    ctx,
                    value=numeric_value,
                    code="above_exclusive_maximum",
                )
            )
        
        # Multiple of check
        if self.multiple_of is not None:
            if numeric_value % self.multiple_of != 0:
                issues.append(
                    self._create_issue(
                        f"Value {numeric_value} is not a multiple of {self.multiple_of}",
                        ctx,
                        value=numeric_value,
                        code="not_multiple_of",
                    )
                )
        
        if issues:
            return ValidationResult.failure(issues, value=numeric_value)
        
        return ValidationResult.success(numeric_value)


class EnumValidator(BaseValidator[T], Generic[T]):
    """Validates enum values."""

    def __init__(
        self,
        enum_class: Type[T],
        allow_value: bool = True,
        allow_name: bool = True,
    ):
        super().__init__(
            name=f"EnumValidator[{enum_class.__name__}]",
            error_code="enum_error",
        )
        self.enum_class = enum_class
        self.allow_value = allow_value
        self.allow_name = allow_name
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        
        # Already an enum instance
        if isinstance(value, self.enum_class):
            return ValidationResult.success(value)
        
        # Try by value
        if self.allow_value:
            for member in self.enum_class:  # type: ignore
                if member.value == value:
                    return ValidationResult.success(member)
        
        # Try by name
        if self.allow_name and isinstance(value, str):
            try:
                return ValidationResult.success(self.enum_class[value])  # type: ignore
            except KeyError:
                pass
        
        valid_values = [f"{m.name}={m.value!r}" for m in self.enum_class]  # type: ignore
        return ValidationResult.from_error(
            f"Invalid enum value: {value!r}. Valid values: {', '.join(valid_values)}",
            code="invalid_enum",
            value=value,
        )


class DateTimeValidator(BaseValidator[datetime]):
    """Validates datetime values."""

    def __init__(
        self,
        min_value: Optional[datetime] = None,
        max_value: Optional[datetime] = None,
        allow_past: bool = True,
        allow_future: bool = True,
        formats: Optional[List[str]] = None,
    ):
        super().__init__(name="DateTimeValidator", error_code="datetime_error")
        self.min_value = min_value
        self.max_value = max_value
        self.allow_past = allow_past
        self.allow_future = allow_future
        self.formats = formats or [
            "%Y-%m-%dT%H:%M:%S",
            "%Y-%m-%dT%H:%M:%S.%f",
            "%Y-%m-%d %H:%M:%S",
            "%Y-%m-%d",
        ]
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[datetime]:
        ctx = self._create_context(context)
        
        dt_value: datetime
        
        if isinstance(value, datetime):
            dt_value = value
        elif isinstance(value, date):
            dt_value = datetime.combine(value, time())
        elif isinstance(value, str):
            dt_value = None  # type: ignore
            for fmt in self.formats:
                try:
                    dt_value = datetime.strptime(value, fmt)
                    break
                except ValueError:
                    continue
            
            if dt_value is None:
                return ValidationResult.from_error(
                    f"Cannot parse datetime from string: {value!r}",
                    code="parse_error",
                    value=value,
                )
        else:
            return ValidationResult.from_error(
                f"Expected datetime, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        issues: List[ValidationIssue] = []
        now = datetime.utcnow()
        
        if not self.allow_past and dt_value < now:
            issues.append(
                self._create_issue(
                    "Past dates are not allowed",
                    ctx,
                    value=dt_value,
                    code="past_not_allowed",
                )
            )
        
        if not self.allow_future and dt_value > now:
            issues.append(
                self._create_issue(
                    "Future dates are not allowed",
                    ctx,
                    value=dt_value,
                    code="future_not_allowed",
                )
            )
        
        if self.min_value and dt_value < self.min_value:
            issues.append(
                self._create_issue(
                    f"Date must be after {self.min_value}",
                    ctx,
                    value=dt_value,
                    code="before_minimum",
                )
            )
        
        if self.max_value and dt_value > self.max_value:
            issues.append(
                self._create_issue(
                    f"Date must be before {self.max_value}",
                    ctx,
                    value=dt_value,
                    code="after_maximum",
                )
            )
        
        if issues:
            return ValidationResult.failure(issues, value=dt_value)
        
        return ValidationResult.success(dt_value)


class UUIDValidator(BaseValidator[UUID]):
    """Validates UUID values."""

    def __init__(
        self,
        versions: Optional[List[int]] = None,
    ):
        super().__init__(name="UUIDValidator", error_code="uuid_error")
        self.versions = versions
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[UUID]:
        ctx = self._create_context(context)
        
        uuid_value: UUID
        
        if isinstance(value, UUID):
            uuid_value = value
        elif isinstance(value, str):
            try:
                uuid_value = UUID(value)
            except ValueError:
                return ValidationResult.from_error(
                    f"Invalid UUID string: {value!r}",
                    code="invalid_uuid",
                    value=value,
                )
        elif isinstance(value, bytes):
            try:
                uuid_value = UUID(bytes=value)
            except ValueError:
                return ValidationResult.from_error(
                    f"Invalid UUID bytes",
                    code="invalid_uuid",
                    value=value,
                )
        else:
            return ValidationResult.from_error(
                f"Expected UUID, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        if self.versions and uuid_value.version not in self.versions:
            return ValidationResult.from_error(
                f"UUID version {uuid_value.version} not in allowed versions {self.versions}",
                code="invalid_uuid_version",
                value=uuid_value,
            )
        
        return ValidationResult.success(uuid_value)


class PathValidator(BaseValidator[Path]):
    """Validates file system paths."""

    def __init__(
        self,
        must_exist: bool = False,
        must_be_file: bool = False,
        must_be_dir: bool = False,
        must_be_absolute: bool = False,
        allowed_extensions: Optional[List[str]] = None,
    ):
        super().__init__(name="PathValidator", error_code="path_error")
        self.must_exist = must_exist
        self.must_be_file = must_be_file
        self.must_be_dir = must_be_dir
        self.must_be_absolute = must_be_absolute
        self.allowed_extensions = allowed_extensions
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Path]:
        ctx = self._create_context(context)
        
        if isinstance(value, Path):
            path_value = value
        elif isinstance(value, str):
            path_value = Path(value)
        else:
            return ValidationResult.from_error(
                f"Expected path, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        issues: List[ValidationIssue] = []
        
        if self.must_be_absolute and not path_value.is_absolute():
            issues.append(
                self._create_issue(
                    "Path must be absolute",
                    ctx,
                    value=path_value,
                    code="not_absolute",
                )
            )
        
        if self.must_exist and not path_value.exists():
            issues.append(
                self._create_issue(
                    f"Path does not exist: {path_value}",
                    ctx,
                    value=path_value,
                    code="path_not_found",
                )
            )
        elif path_value.exists():
            if self.must_be_file and not path_value.is_file():
                issues.append(
                    self._create_issue(
                        f"Path is not a file: {path_value}",
                        ctx,
                        value=path_value,
                        code="not_a_file",
                    )
                )
            
            if self.must_be_dir and not path_value.is_dir():
                issues.append(
                    self._create_issue(
                        f"Path is not a directory: {path_value}",
                        ctx,
                        value=path_value,
                        code="not_a_directory",
                    )
                )
        
        if self.allowed_extensions:
            ext = path_value.suffix.lower()
            if ext not in [e.lower() for e in self.allowed_extensions]:
                issues.append(
                    self._create_issue(
                        f"File extension '{ext}' not allowed. Allowed: {self.allowed_extensions}",
                        ctx,
                        value=path_value,
                        code="invalid_extension",
                    )
                )
        
        if issues:
            return ValidationResult.failure(issues, value=path_value)
        
        return ValidationResult.success(path_value)