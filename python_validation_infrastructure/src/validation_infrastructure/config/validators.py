"""Configuration file validators for various formats."""

from __future__ import annotations

import json
from abc import ABC, abstractmethod
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Type, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
)
from validation_infrastructure.errors.exceptions import ConfigurationError
from validation_infrastructure.schemas.base import SchemaValidator


class ConfigValidator(BaseValidator[Dict[str, Any]], ABC):
    """Base class for configuration file validators."""

    def __init__(
        self,
        schema: Optional[Union[Dict[str, Any], SchemaValidator[Any], Type[Any]]] = None,
        strict: bool = False,
        allow_unknown_keys: bool = True,
    ):
        super().__init__(name="ConfigValidator")
        self.schema = schema
        self.strict = strict
        self.allow_unknown_keys = allow_unknown_keys
    
    @abstractmethod
    def parse(self, content: str) -> Dict[str, Any]:
        """Parse configuration content string."""
        ...
    
    @abstractmethod
    def parse_file(self, path: Union[str, Path]) -> Dict[str, Any]:
        """Parse configuration file."""
        ...
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate configuration data."""
        ctx = self._create_context(context)
        
        # If value is a path, load it
        if isinstance(value, (str, Path)):
            path = Path(value)
            if path.exists():
                try:
                    value = self.parse_file(path)
                except Exception as e:
                    return ValidationResult.from_error(
                        f"Failed to parse config file: {e}",
                        code="parse_error",
                        value=str(path),
                    )
        
        if not isinstance(value, dict):
            return ValidationResult.from_error(
                f"Expected dict, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        # Validate against schema if provided
        if self.schema:
            return self._validate_against_schema(value, ctx)
        
        return ValidationResult.success(value)
    
    def _validate_against_schema(
        self,
        data: Dict[str, Any],
        ctx: ValidationContext,
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate config data against schema."""
        
        # Handle different schema types
        if isinstance(self.schema, SchemaValidator):
            return self.schema.validate(data, ctx)
        
        if isinstance(self.schema, dict):
            return self._validate_dict_schema(data, self.schema, ctx)
        
        # Pydantic model
        if hasattr(self.schema, "model_validate"):
            try:
                validated = self.schema.model_validate(data)
                return ValidationResult.success(validated.model_dump())
            except Exception as e:
                return ValidationResult.from_error(
                    f"Schema validation failed: {e}",
                    code="schema_error",
                    value=data,
                )
        
        return ValidationResult.success(data)
    
    def _validate_dict_schema(
        self,
        data: Dict[str, Any],
        schema: Dict[str, Any],
        ctx: ValidationContext,
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate against a dict-based schema definition."""
        result: ValidationResult[Dict[str, Any]] = ValidationResult.success(data)
        
        # Check required keys
        required = schema.get("required", [])
        for key in required:
            if key not in data:
                result.add_issue(
                    ValidationIssue(
                        message=f"Missing required config key: {key}",
                        field=key,
                        code="missing_key",
                        path=ctx.current_path,
                    )
                )
        
        # Check types and constraints
        properties = schema.get("properties", {})
        for key, prop_schema in properties.items():
            if key in data:
                field_result = self._validate_property(key, data[key], prop_schema, ctx)
                result.merge(field_result)
        
        # Check unknown keys
        if not self.allow_unknown_keys:
            known_keys = set(properties.keys())
            for key in data:
                if key not in known_keys:
                    result.add_issue(
                        ValidationIssue(
                            message=f"Unknown config key: {key}",
                            field=key,
                            code="unknown_key",
                            path=ctx.current_path,
                        )
                    )
        
        return result
    
    def _validate_property(
        self,
        key: str,
        value: Any,
        schema: Dict[str, Any],
        ctx: ValidationContext,
    ) -> ValidationResult[Any]:
        """Validate a single property against its schema."""
        field_ctx = ctx.child(key)
        
        # Type check
        expected_type = schema.get("type")
        if expected_type:
            type_map = {
                "string": str,
                "integer": int,
                "number": (int, float),
                "boolean": bool,
                "array": list,
                "object": dict,
            }
            python_type = type_map.get(expected_type)
            if python_type and not isinstance(value, python_type):
                return ValidationResult.from_error(
                    f"Expected {expected_type}, got {type(value).__name__}",
                    code="type_error",
                    field=key,
                    value=value,
                )
        
        # Enum check
        enum_values = schema.get("enum")
        if enum_values and value not in enum_values:
            return ValidationResult.from_error(
                f"Value must be one of: {enum_values}",
                code="enum_error",
                field=key,
                value=value,
            )
        
        # Range checks
        minimum = schema.get("minimum")
        maximum = schema.get("maximum")
        if minimum is not None and isinstance(value, (int, float)) and value < minimum:
            return ValidationResult.from_error(
                f"Value {value} is below minimum {minimum}",
                code="below_minimum",
                field=key,
                value=value,
            )
        if maximum is not None and isinstance(value, (int, float)) and value > maximum:
            return ValidationResult.from_error(
                f"Value {value} exceeds maximum {maximum}",
                code="above_maximum",
                field=key,
                value=value,
            )
        
        return ValidationResult.success(value)


class JSONValidator(ConfigValidator):
    """Validates JSON configuration files."""

    def __init__(
        self,
        schema: Optional[Union[Dict[str, Any], SchemaValidator[Any], Type[Any]]] = None,
        strict: bool = False,
        allow_comments: bool = False,
        allow_trailing_commas: bool = False,
    ):
        super().__init__(schema=schema, strict=strict)
        self.name = "JSONValidator"
        self.allow_comments = allow_comments
        self.allow_trailing_commas = allow_trailing_commas
    
    def parse(self, content: str) -> Dict[str, Any]:
        """Parse JSON content."""
        if self.allow_comments:
            # Remove single-line comments
            import re
            content = re.sub(r"//.*$", "", content, flags=re.MULTILINE)
            # Remove multi-line comments
            content = re.sub(r"/\*.*?\*/", "", content, flags=re.DOTALL)
        
        if self.allow_trailing_commas:
            import re
            # Remove trailing commas before ] or }
            content = re.sub(r",(\s*[}\]])", r"\1", content)
        
        return json.loads(content)
    
    def parse_file(self, path: Union[str, Path]) -> Dict[str, Any]:
        """Parse JSON file."""
        with open(path, "r", encoding="utf-8") as f:
            return self.parse(f.read())


class YAMLValidator(ConfigValidator):
    """Validates YAML configuration files."""

    def __init__(
        self,
        schema: Optional[Union[Dict[str, Any], SchemaValidator[Any], Type[Any]]] = None,
        strict: bool = False,
        safe_load: bool = True,
        allow_duplicate_keys: bool = False,
    ):
        super().__init__(schema=schema, strict=strict)
        self.name = "YAMLValidator"
        self.safe_load = safe_load
        self.allow_duplicate_keys = allow_duplicate_keys
    
    def parse(self, content: str) -> Dict[str, Any]:
        """Parse YAML content."""
        try:
            import yaml
        except ImportError:
            raise ImportError("PyYAML is required for YAML validation")
        
        if self.safe_load:
            loader = yaml.SafeLoader
        else:
            loader = yaml.FullLoader
        
        result = yaml.load(content, Loader=loader)
        
        if result is None:
            return {}
        
        if not isinstance(result, dict):
            raise ValueError(f"Expected YAML dict, got {type(result).__name__}")
        
        return result
    
    def parse_file(self, path: Union[str, Path]) -> Dict[str, Any]:
        """Parse YAML file."""
        with open(path, "r", encoding="utf-8") as f:
            return self.parse(f.read())
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate YAML with additional checks."""
        ctx = self._create_context(context)
        
        # Check for duplicate keys if parsing from string
        if isinstance(value, str) and not self.allow_duplicate_keys:
            duplicates = self._find_duplicate_keys(value)
            if duplicates:
                return ValidationResult.from_error(
                    f"Duplicate keys found: {duplicates}",
                    code="duplicate_keys",
                    value=value,
                )
        
        return super().validate(value, ctx)
    
    def _find_duplicate_keys(self, content: str) -> List[str]:
        """Find duplicate keys in YAML content."""
        try:
            import yaml
            
            class DuplicateKeyChecker(yaml.SafeLoader):
                pass
            
            duplicates: List[str] = []
            
            def check_duplicate_key(loader, node, deep=False):
                mapping = {}
                for key_node, value_node in node.value:
                    key = loader.construct_object(key_node, deep=deep)
                    if key in mapping:
                        duplicates.append(str(key))
                    mapping[key] = loader.construct_object(value_node, deep=deep)
                return mapping
            
            DuplicateKeyChecker.add_constructor(
                yaml.resolver.BaseResolver.DEFAULT_MAPPING_TAG,
                check_duplicate_key,
            )
            
            yaml.load(content, Loader=DuplicateKeyChecker)
            return duplicates
        except Exception:
            return []


class TOMLValidator(ConfigValidator):
    """Validates TOML configuration files."""

    def __init__(
        self,
        schema: Optional[Union[Dict[str, Any], SchemaValidator[Any], Type[Any]]] = None,
        strict: bool = False,
    ):
        super().__init__(schema=schema, strict=strict)
        self.name = "TOMLValidator"
    
    def parse(self, content: str) -> Dict[str, Any]:
        """Parse TOML content."""
        try:
            import tomllib
        except ImportError:
            try:
                import tomli as tomllib
            except ImportError:
                raise ImportError("tomli or Python 3.11+ is required for TOML validation")
        
        return tomllib.loads(content)
    
    def parse_file(self, path: Union[str, Path]) -> Dict[str, Any]:
        """Parse TOML file."""
        try:
            import tomllib
        except ImportError:
            try:
                import tomli as tomllib
            except ImportError:
                raise ImportError("tomli or Python 3.11+ is required for TOML validation")
        
        with open(path, "rb") as f:
            return tomllib.load(f)


def validate_config_file(
    path: Union[str, Path],
    schema: Optional[Union[Dict[str, Any], SchemaValidator[Any], Type[Any]]] = None,
    format: Optional[str] = None,
    strict: bool = False,
) -> ValidationResult[Dict[str, Any]]:
    """
    Validate a configuration file.
    
    Args:
        path: Path to configuration file
        schema: Optional validation schema
        format: File format (auto-detected if not specified)
        strict: Enable strict validation
    
    Returns:
        ValidationResult with parsed config data
    
    Example:
        result = validate_config_file("config.yaml", schema={
            "required": ["database", "server"],
            "properties": {
                "database": {"type": "object"},
                "server": {"type": "object"},
            }
        })
    """
    path = Path(path)
    
    if not path.exists():
        return ValidationResult.from_error(
            f"Config file not found: {path}",
            code="file_not_found",
            value=str(path),
        )
    
    # Auto-detect format
    if format is None:
        suffix = path.suffix.lower()
        format_map = {
            ".json": "json",
            ".yaml": "yaml",
            ".yml": "yaml",
            ".toml": "toml",
        }
        format = format_map.get(suffix)
        if format is None:
            return ValidationResult.from_error(
                f"Unknown config file format: {suffix}",
                code="unknown_format",
                value=str(path),
            )
    
    # Select validator
    validators = {
        "json": JSONValidator,
        "yaml": YAMLValidator,
        "toml": TOMLValidator,
    }
    
    validator_class = validators.get(format.lower())
    if validator_class is None:
        return ValidationResult.from_error(
            f"Unsupported format: {format}",
            code="unsupported_format",
            value=format,
        )
    
    validator = validator_class(schema=schema, strict=strict)
    return validator.validate(path)


class ConfigSchemaBuilder:
    """Builder for creating config validation schemas."""

    def __init__(self):
        self._properties: Dict[str, Dict[str, Any]] = {}
        self._required: List[str] = []
    
    def add_string(
        self,
        name: str,
        required: bool = False,
        default: Optional[str] = None,
        pattern: Optional[str] = None,
        enum: Optional[List[str]] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add a string property."""
        prop: Dict[str, Any] = {"type": "string"}
        if default is not None:
            prop["default"] = default
        if pattern:
            prop["pattern"] = pattern
        if enum:
            prop["enum"] = enum
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def add_integer(
        self,
        name: str,
        required: bool = False,
        default: Optional[int] = None,
        minimum: Optional[int] = None,
        maximum: Optional[int] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add an integer property."""
        prop: Dict[str, Any] = {"type": "integer"}
        if default is not None:
            prop["default"] = default
        if minimum is not None:
            prop["minimum"] = minimum
        if maximum is not None:
            prop["maximum"] = maximum
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def add_number(
        self,
        name: str,
        required: bool = False,
        default: Optional[float] = None,
        minimum: Optional[float] = None,
        maximum: Optional[float] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add a number property."""
        prop: Dict[str, Any] = {"type": "number"}
        if default is not None:
            prop["default"] = default
        if minimum is not None:
            prop["minimum"] = minimum
        if maximum is not None:
            prop["maximum"] = maximum
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def add_boolean(
        self,
        name: str,
        required: bool = False,
        default: Optional[bool] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add a boolean property."""
        prop: Dict[str, Any] = {"type": "boolean"}
        if default is not None:
            prop["default"] = default
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def add_object(
        self,
        name: str,
        required: bool = False,
        properties: Optional[Dict[str, Any]] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add an object property."""
        prop: Dict[str, Any] = {"type": "object"}
        if properties:
            prop["properties"] = properties
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def add_array(
        self,
        name: str,
        items_type: str = "string",
        required: bool = False,
        min_items: Optional[int] = None,
        max_items: Optional[int] = None,
        description: Optional[str] = None,
    ) -> ConfigSchemaBuilder:
        """Add an array property."""
        prop: Dict[str, Any] = {
            "type": "array",
            "items": {"type": items_type},
        }
        if min_items is not None:
            prop["minItems"] = min_items
        if max_items is not None:
            prop["maxItems"] = max_items
        if description:
            prop["description"] = description
        
        self._properties[name] = prop
        if required:
            self._required.append(name)
        
        return self
    
    def build(self) -> Dict[str, Any]:
        """Build and return the schema."""
        return {
            "type": "object",
            "properties": self._properties,
            "required": self._required,
        }
