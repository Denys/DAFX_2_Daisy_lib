"""Base schema validation classes."""

from __future__ import annotations

from abc import abstractmethod
from dataclasses import dataclass, field
from typing import Any, Dict, Generic, List, Optional, Type, TypeVar

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
)

T = TypeVar("T")


@dataclass
class SchemaValidationResult(Generic[T]):
    """Result of schema validation with parsed data."""

    is_valid: bool
    data: Optional[T] = None
    errors: List[Dict[str, Any]] = field(default_factory=list)
    warnings: List[Dict[str, Any]] = field(default_factory=list)
    raw_errors: Any = None  # Original errors from schema library
    
    @classmethod
    def success(cls, data: T) -> SchemaValidationResult[T]:
        """Create a successful result."""
        return cls(is_valid=True, data=data)
    
    @classmethod
    def failure(
        cls,
        errors: List[Dict[str, Any]],
        raw_errors: Any = None,
    ) -> SchemaValidationResult[T]:
        """Create a failed result."""
        return cls(is_valid=False, errors=errors, raw_errors=raw_errors)
    
    def to_validation_result(self) -> ValidationResult[T]:
        """Convert to a standard ValidationResult."""
        if self.is_valid:
            result = ValidationResult.success(self.data)
        else:
            issues = [
                ValidationIssue(
                    message=e.get("message", str(e)),
                    field=e.get("field"),
                    code=e.get("code", "schema_error"),
                    path=e.get("path"),
                )
                for e in self.errors
            ]
            result = ValidationResult.failure(issues, value=self.data)
        
        for warning in self.warnings:
            result.warnings.append(
                ValidationIssue(
                    message=warning.get("message", str(warning)),
                    field=warning.get("field"),
                    code=warning.get("code", "schema_warning"),
                )
            )
        
        return result


class SchemaValidator(BaseValidator[T], Generic[T]):
    """Base class for schema-based validators."""

    def __init__(
        self,
        schema: Any,
        strict: bool = False,
        partial: bool = False,
        many: bool = False,
    ):
        super().__init__(name="SchemaValidator")
        self.schema = schema
        self.strict = strict
        self.partial = partial
        self.many = many
    
    @abstractmethod
    def validate_schema(
        self,
        data: Any,
        context: Optional[ValidationContext] = None,
    ) -> SchemaValidationResult[T]:
        """Validate data against the schema."""
        ...
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Validate and return standard ValidationResult."""
        schema_result = self.validate_schema(value, context)
        return schema_result.to_validation_result()
    
    @abstractmethod
    def get_schema_dict(self) -> Dict[str, Any]:
        """Get schema as dictionary (JSON Schema format)."""
        ...
    
    @abstractmethod
    def get_field_info(self, field_name: str) -> Optional[Dict[str, Any]]:
        """Get information about a specific field."""
        ...
    
    @property
    @abstractmethod
    def required_fields(self) -> List[str]:
        """Get list of required field names."""
        ...
    
    @property
    @abstractmethod
    def optional_fields(self) -> List[str]:
        """Get list of optional field names."""
        ...


class DynamicSchemaValidator(SchemaValidator[Dict[str, Any]]):
    """Validator for dynamically defined schemas."""

    def __init__(
        self,
        field_definitions: Dict[str, Dict[str, Any]],
        strict: bool = False,
        allow_extra: bool = False,
    ):
        """
        Initialize with field definitions.
        
        Args:
            field_definitions: Dict mapping field names to their specs:
                {
                    "field_name": {
                        "type": str,  # "string", "integer", "float", "boolean", etc.
                        "required": bool,
                        "default": Any,
                        "min": number,
                        "max": number,
                        "pattern": str,
                        "choices": list,
                        "validator": Callable,
                    }
                }
            strict: Whether to use strict type checking
            allow_extra: Whether to allow extra fields not in definition
        """
        super().__init__(schema=field_definitions, strict=strict)
        self.field_definitions = field_definitions
        self.allow_extra = allow_extra
        self._type_map = {
            "string": str,
            "str": str,
            "integer": int,
            "int": int,
            "float": float,
            "number": (int, float),
            "boolean": bool,
            "bool": bool,
            "list": list,
            "array": list,
            "dict": dict,
            "object": dict,
        }
    
    def validate_schema(
        self,
        data: Any,
        context: Optional[ValidationContext] = None,
    ) -> SchemaValidationResult[Dict[str, Any]]:
        ctx = context or ValidationContext()
        
        if not isinstance(data, dict):
            return SchemaValidationResult.failure([
                {
                    "message": f"Expected dict, got {type(data).__name__}",
                    "code": "type_error",
                }
            ])
        
        errors: List[Dict[str, Any]] = []
        validated: Dict[str, Any] = {}
        
        # Check required fields
        for field_name, field_spec in self.field_definitions.items():
            if field_spec.get("required", False) and field_name not in data:
                if "default" not in field_spec:
                    errors.append({
                        "field": field_name,
                        "message": f"Missing required field: {field_name}",
                        "code": "missing_field",
                    })
        
        # Validate each field
        for field_name, value in data.items():
            if field_name not in self.field_definitions:
                if not self.allow_extra:
                    errors.append({
                        "field": field_name,
                        "message": f"Unknown field: {field_name}",
                        "code": "extra_field",
                    })
                else:
                    validated[field_name] = value
                continue
            
            field_spec = self.field_definitions[field_name]
            field_errors = self._validate_field(field_name, value, field_spec)
            
            if field_errors:
                errors.extend(field_errors)
            else:
                validated[field_name] = value
        
        # Apply defaults for missing optional fields
        for field_name, field_spec in self.field_definitions.items():
            if field_name not in validated and "default" in field_spec:
                default = field_spec["default"]
                validated[field_name] = default() if callable(default) else default
        
        if errors:
            return SchemaValidationResult.failure(errors)
        
        return SchemaValidationResult.success(validated)
    
    def _validate_field(
        self,
        field_name: str,
        value: Any,
        spec: Dict[str, Any],
    ) -> List[Dict[str, Any]]:
        """Validate a single field against its specification."""
        errors: List[Dict[str, Any]] = []
        
        # Type check
        if "type" in spec:
            expected_type = self._type_map.get(spec["type"])
            if expected_type and not isinstance(value, expected_type):
                errors.append({
                    "field": field_name,
                    "message": f"Expected {spec['type']}, got {type(value).__name__}",
                    "code": "type_error",
                })
                return errors  # Skip other checks if type is wrong
        
        # Numeric constraints
        if isinstance(value, (int, float)):
            if "min" in spec and value < spec["min"]:
                errors.append({
                    "field": field_name,
                    "message": f"Value {value} is below minimum {spec['min']}",
                    "code": "below_minimum",
                })
            if "max" in spec and value > spec["max"]:
                errors.append({
                    "field": field_name,
                    "message": f"Value {value} exceeds maximum {spec['max']}",
                    "code": "above_maximum",
                })
        
        # String constraints
        if isinstance(value, str):
            if "min_length" in spec and len(value) < spec["min_length"]:
                errors.append({
                    "field": field_name,
                    "message": f"String too short (min: {spec['min_length']})",
                    "code": "string_too_short",
                })
            if "max_length" in spec and len(value) > spec["max_length"]:
                errors.append({
                    "field": field_name,
                    "message": f"String too long (max: {spec['max_length']})",
                    "code": "string_too_long",
                })
            if "pattern" in spec:
                import re
                if not re.match(spec["pattern"], value):
                    errors.append({
                        "field": field_name,
                        "message": f"String does not match pattern: {spec['pattern']}",
                        "code": "pattern_mismatch",
                    })
        
        # Choices constraint
        if "choices" in spec and value not in spec["choices"]:
            errors.append({
                "field": field_name,
                "message": f"Value must be one of: {spec['choices']}",
                "code": "invalid_choice",
            })
        
        # Custom validator
        if "validator" in spec:
            try:
                result = spec["validator"](value)
                if result is False:
                    errors.append({
                        "field": field_name,
                        "message": "Custom validation failed",
                        "code": "custom_validation_failed",
                    })
                elif isinstance(result, str):
                    errors.append({
                        "field": field_name,
                        "message": result,
                        "code": "custom_validation_failed",
                    })
            except Exception as e:
                errors.append({
                    "field": field_name,
                    "message": f"Validator error: {e}",
                    "code": "validator_error",
                })
        
        return errors
    
    def get_schema_dict(self) -> Dict[str, Any]:
        """Get JSON Schema representation."""
        properties: Dict[str, Any] = {}
        required: List[str] = []
        
        for field_name, spec in self.field_definitions.items():
            prop: Dict[str, Any] = {}
            
            # Map type
            type_name = spec.get("type", "string")
            json_type_map = {
                "string": "string",
                "str": "string",
                "integer": "integer",
                "int": "integer",
                "float": "number",
                "number": "number",
                "boolean": "boolean",
                "bool": "boolean",
                "list": "array",
                "array": "array",
                "dict": "object",
                "object": "object",
            }
            prop["type"] = json_type_map.get(type_name, "string")
            
            # Add constraints
            if "min" in spec:
                prop["minimum"] = spec["min"]
            if "max" in spec:
                prop["maximum"] = spec["max"]
            if "min_length" in spec:
                prop["minLength"] = spec["min_length"]
            if "max_length" in spec:
                prop["maxLength"] = spec["max_length"]
            if "pattern" in spec:
                prop["pattern"] = spec["pattern"]
            if "choices" in spec:
                prop["enum"] = spec["choices"]
            if "default" in spec:
                default = spec["default"]
                prop["default"] = default() if callable(default) else default
            
            properties[field_name] = prop
            
            if spec.get("required", False):
                required.append(field_name)
        
        return {
            "type": "object",
            "properties": properties,
            "required": required,
            "additionalProperties": self.allow_extra,
        }
    
    def get_field_info(self, field_name: str) -> Optional[Dict[str, Any]]:
        """Get information about a field."""
        return self.field_definitions.get(field_name)
    
    @property
    def required_fields(self) -> List[str]:
        """Get required field names."""
        return [
            name for name, spec in self.field_definitions.items()
            if spec.get("required", False)
        ]
    
    @property
    def optional_fields(self) -> List[str]:
        """Get optional field names."""
        return [
            name for name, spec in self.field_definitions.items()
            if not spec.get("required", False)
        ]
