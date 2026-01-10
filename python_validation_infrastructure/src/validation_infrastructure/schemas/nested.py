"""Nested object validation with recursive depth handling."""

from __future__ import annotations

from typing import Any, Callable, Dict, Generic, List, Optional, Set, Type, TypeVar, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
    ValidationSeverity,
)

T = TypeVar("T")


class NestedValidator(BaseValidator[T], Generic[T]):
    """Validates nested objects with field-level validators."""

    def __init__(
        self,
        field_validators: Dict[str, BaseValidator[Any]],
        required_fields: Optional[Set[str]] = None,
        allow_extra: bool = False,
        max_depth: int = 50,
        fail_fast: bool = False,
    ):
        """
        Initialize nested validator.
        
        Args:
            field_validators: Dict mapping field names to their validators
            required_fields: Set of required field names (defaults to all)
            allow_extra: Whether to allow fields not in field_validators
            max_depth: Maximum nesting depth for recursive validation
            fail_fast: Stop at first error
        """
        super().__init__(name="NestedValidator")
        self.field_validators = field_validators
        self.required_fields = required_fields or set(field_validators.keys())
        self.allow_extra = allow_extra
        self.max_depth = max_depth
        self.fail_fast = fail_fast
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        ctx.max_depth = self.max_depth
        
        # Check for dict-like object
        if not isinstance(value, dict):
            if hasattr(value, "__dict__"):
                value = vars(value)
            else:
                return ValidationResult.from_error(
                    f"Expected dict or object, got {type(value).__name__}",
                    code="type_error",
                    value=value,
                )
        
        return self._validate_nested(value, ctx)
    
    def _validate_nested(
        self,
        data: Dict[str, Any],
        ctx: ValidationContext,
    ) -> ValidationResult[T]:
        """Validate nested data structure."""
        result: ValidationResult[Any] = ValidationResult.success({})
        validated_data: Dict[str, Any] = {}
        
        # Check circular reference
        if ctx.has_seen(data):
            result.add_issue(
                ValidationIssue(
                    message="Circular reference detected",
                    code="circular_reference",
                    path=ctx.current_path,
                    severity=ValidationSeverity.ERROR,
                )
            )
            return result  # type: ignore
        
        # Check required fields
        missing_fields = self.required_fields - set(data.keys())
        for field_name in missing_fields:
            result.add_issue(
                self._create_issue(
                    f"Missing required field: {field_name}",
                    ctx,
                    code="missing_field",
                    field=field_name,
                )
            )
            if self.fail_fast:
                return result  # type: ignore
        
        # Validate each field
        for field_name, field_value in data.items():
            if field_name not in self.field_validators:
                if not self.allow_extra:
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
                    validated_data[field_name] = field_value
                continue
            
            # Create child context
            field_ctx = ctx.child(field_name)
            
            # Validate field
            validator = self.field_validators[field_name]
            field_result = validator.validate(field_value, field_ctx)
            
            if field_result.is_valid:
                validated_data[field_name] = field_result.value
            else:
                result.merge(field_result)
                if self.fail_fast:
                    break
                # Still include original value
                validated_data[field_name] = field_value
            
            # Include warnings
            result.warnings.extend(field_result.warnings)
        
        result.value = validated_data
        return result  # type: ignore


class RecursiveValidator(BaseValidator[T], Generic[T]):
    """
    Validates recursively nested structures with depth control.
    
    Useful for tree-like data structures, JSON objects with unknown nesting,
    or self-referential schemas.
    """

    def __init__(
        self,
        item_validator: Optional[BaseValidator[Any]] = None,
        max_depth: int = 50,
        allow_cycles: bool = False,
        depth_error_mode: str = "error",  # "error", "truncate", "warn"
    ):
        """
        Initialize recursive validator.
        
        Args:
            item_validator: Validator for leaf values
            max_depth: Maximum recursion depth
            allow_cycles: Whether to allow circular references
            depth_error_mode: How to handle max depth - "error", "truncate", or "warn"
        """
        super().__init__(name="RecursiveValidator")
        self.item_validator = item_validator
        self.max_depth = max_depth
        self.allow_cycles = allow_cycles
        self.depth_error_mode = depth_error_mode
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        ctx.max_depth = self.max_depth
        
        return self._validate_recursive(value, ctx, set())
    
    def _validate_recursive(
        self,
        value: Any,
        ctx: ValidationContext,
        seen_ids: Set[int],
    ) -> ValidationResult[T]:
        """Recursively validate a value."""
        
        # Check depth
        if ctx.depth > self.max_depth:
            if self.depth_error_mode == "error":
                return ValidationResult.from_error(
                    f"Maximum depth ({self.max_depth}) exceeded at {ctx.current_path}",
                    code="max_depth_exceeded",
                    value=value,
                )
            elif self.depth_error_mode == "truncate":
                result = ValidationResult.success(value)
                result.warnings.append(
                    ValidationIssue(
                        message=f"Value truncated at depth {ctx.depth}",
                        code="depth_truncated",
                        path=ctx.current_path,
                        severity=ValidationSeverity.WARNING,
                    )
                )
                return result  # type: ignore
            else:  # warn
                result = ValidationResult.success(value)
                result.warnings.append(
                    ValidationIssue(
                        message=f"Deep nesting at {ctx.current_path}",
                        code="deep_nesting",
                        path=ctx.current_path,
                        severity=ValidationSeverity.WARNING,
                    )
                )
                return result  # type: ignore
        
        # Check circular reference
        obj_id = id(value)
        if obj_id in seen_ids:
            if not self.allow_cycles:
                return ValidationResult.from_error(
                    f"Circular reference detected at {ctx.current_path}",
                    code="circular_reference",
                    value=value,
                )
            else:
                return ValidationResult.success(value)  # type: ignore
        
        # Track this object
        new_seen = seen_ids | {obj_id} if isinstance(value, (dict, list)) else seen_ids
        
        # Handle different types
        if isinstance(value, dict):
            return self._validate_dict(value, ctx, new_seen)
        elif isinstance(value, list):
            return self._validate_list(value, ctx, new_seen)
        else:
            # Leaf value - use item validator if provided
            if self.item_validator:
                return self.item_validator.validate(value, ctx)
            return ValidationResult.success(value)  # type: ignore
    
    def _validate_dict(
        self,
        data: Dict[str, Any],
        ctx: ValidationContext,
        seen_ids: Set[int],
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate a dictionary recursively."""
        result: ValidationResult[Dict[str, Any]] = ValidationResult.success({})
        validated: Dict[str, Any] = {}
        
        for key, value in data.items():
            child_ctx = ctx.child(str(key))
            child_result = self._validate_recursive(value, child_ctx, seen_ids)
            
            if child_result.is_valid:
                validated[key] = child_result.value
            else:
                result.merge(child_result)
                validated[key] = value
            
            result.warnings.extend(child_result.warnings)
        
        result.value = validated
        return result
    
    def _validate_list(
        self,
        items: List[Any],
        ctx: ValidationContext,
        seen_ids: Set[int],
    ) -> ValidationResult[List[Any]]:
        """Validate a list recursively."""
        result: ValidationResult[List[Any]] = ValidationResult.success([])
        validated: List[Any] = []
        
        for i, item in enumerate(items):
            child_ctx = ctx.child(str(i))
            child_result = self._validate_recursive(item, child_ctx, seen_ids)
            
            if child_result.is_valid:
                validated.append(child_result.value)
            else:
                result.merge(child_result)
                validated.append(item)
            
            result.warnings.extend(child_result.warnings)
        
        result.value = validated
        return result


def validate_nested(
    data: Any,
    field_validators: Dict[str, BaseValidator[Any]],
    required_fields: Optional[Set[str]] = None,
    allow_extra: bool = False,
    max_depth: int = 50,
    context: Optional[ValidationContext] = None,
) -> ValidationResult[Dict[str, Any]]:
    """
    Convenient function for nested validation.
    
    Example:
        result = validate_nested(
            {"name": "John", "age": 30, "address": {"city": "NYC"}},
            {
                "name": StringValidator(min_length=1),
                "age": NumericValidator(minimum=0),
                "address": NestedValidator({
                    "city": StringValidator(),
                }),
            },
        )
    """
    validator = NestedValidator(
        field_validators=field_validators,
        required_fields=required_fields,
        allow_extra=allow_extra,
        max_depth=max_depth,
    )
    return validator.validate(data, context)


class ObjectValidator(NestedValidator[T]):
    """
    Validates objects with schema-like definition.
    
    Provides a more declarative way to define nested validation.
    """

    @classmethod
    def from_spec(
        cls,
        spec: Dict[str, Any],
        strict: bool = True,
    ) -> ObjectValidator[Dict[str, Any]]:
        """
        Create validator from a specification dictionary.
        
        Spec format:
        {
            "field_name": {
                "type": "string" | "integer" | "float" | "boolean" | "object" | "array",
                "required": bool,
                "min": number,
                "max": number,
                "pattern": str,
                "items": spec,  # For arrays
                "properties": spec,  # For nested objects
            }
        }
        """
        from validation_infrastructure.core.types import (
            StringValidator,
            NumericValidator,
        )
        
        field_validators: Dict[str, BaseValidator[Any]] = {}
        required_fields: Set[str] = set()
        
        for field_name, field_spec in spec.items():
            validator = cls._create_field_validator(field_spec)
            if validator:
                field_validators[field_name] = validator
            
            if field_spec.get("required", True):
                required_fields.add(field_name)
        
        return cls(
            field_validators=field_validators,
            required_fields=required_fields,
            allow_extra=not strict,
        )
    
    @classmethod
    def _create_field_validator(cls, spec: Dict[str, Any]) -> Optional[BaseValidator[Any]]:
        """Create a validator from a field specification."""
        from validation_infrastructure.core.types import (
            StringValidator,
            NumericValidator,
            TypeValidator,
        )
        
        field_type = spec.get("type", "string")
        
        if field_type == "string":
            return StringValidator(
                min_length=spec.get("min_length"),
                max_length=spec.get("max_length"),
                pattern=spec.get("pattern"),
            )
        elif field_type in ("integer", "int"):
            return NumericValidator(
                numeric_type=int,
                minimum=spec.get("min"),
                maximum=spec.get("max"),
            )
        elif field_type in ("float", "number"):
            return NumericValidator(
                numeric_type=float,
                minimum=spec.get("min"),
                maximum=spec.get("max"),
            )
        elif field_type == "boolean":
            return TypeValidator(bool)
        elif field_type == "object":
            if "properties" in spec:
                return cls.from_spec(spec["properties"])
            return TypeValidator(dict)
        elif field_type == "array":
            if "items" in spec:
                item_validator = cls._create_field_validator(spec["items"])
                if item_validator:
                    return ArrayValidator(item_validator)
            return TypeValidator(list)
        
        return None


class ArrayValidator(BaseValidator[List[T]], Generic[T]):
    """Validates arrays/lists with item validation."""

    def __init__(
        self,
        item_validator: BaseValidator[T],
        min_items: Optional[int] = None,
        max_items: Optional[int] = None,
        unique_items: bool = False,
    ):
        super().__init__(name="ArrayValidator")
        self.item_validator = item_validator
        self.min_items = min_items
        self.max_items = max_items
        self.unique_items = unique_items
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[List[T]]:
        ctx = self._create_context(context)
        
        if not isinstance(value, (list, tuple)):
            return ValidationResult.from_error(
                f"Expected array, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        result: ValidationResult[List[T]] = ValidationResult.success([])
        validated_items: List[T] = []
        issues: List[ValidationIssue] = []
        
        # Length checks
        if self.min_items is not None and len(value) < self.min_items:
            issues.append(
                self._create_issue(
                    f"Array has {len(value)} items, minimum is {self.min_items}",
                    ctx,
                    code="array_too_short",
                    value=value,
                )
            )
        
        if self.max_items is not None and len(value) > self.max_items:
            issues.append(
                self._create_issue(
                    f"Array has {len(value)} items, maximum is {self.max_items}",
                    ctx,
                    code="array_too_long",
                    value=value,
                )
            )
        
        # Unique items check
        if self.unique_items:
            try:
                seen: Set[Any] = set()
                for item in value:
                    hashable = item if isinstance(item, (str, int, float, bool, type(None))) else str(item)
                    if hashable in seen:
                        issues.append(
                            self._create_issue(
                                f"Duplicate item found: {item!r}",
                                ctx,
                                code="duplicate_item",
                                value=item,
                            )
                        )
                        break
                    seen.add(hashable)
            except TypeError:
                pass  # Can't check uniqueness for unhashable items
        
        # Validate each item
        for i, item in enumerate(value):
            item_ctx = ctx.child(str(i))
            item_result = self.item_validator.validate(item, item_ctx)
            
            if item_result.is_valid:
                validated_items.append(item_result.value)
            else:
                issues.extend(item_result.issues)
                validated_items.append(item)  # type: ignore
            
            result.warnings.extend(item_result.warnings)
        
        if issues:
            for issue in issues:
                result.add_issue(issue)
        
        result.value = validated_items
        return result
