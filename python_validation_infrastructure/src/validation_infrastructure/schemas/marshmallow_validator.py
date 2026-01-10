"""Marshmallow schema validation integration."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Type, TypeVar, Union

from validation_infrastructure.core.base import ValidationContext
from validation_infrastructure.schemas.base import SchemaValidationResult, SchemaValidator

T = TypeVar("T")


class MarshmallowSchemaValidator(SchemaValidator[T]):
    """Validator using Marshmallow schemas."""

    def __init__(
        self,
        schema: Any,
        strict: bool = False,
        partial: bool = False,
        many: bool = False,
        unknown: str = "RAISE",
    ):
        """
        Initialize Marshmallow validator.
        
        Args:
            schema: Marshmallow Schema class or instance
            strict: Not used (Marshmallow 3+ is always strict)
            partial: Allow partial data (missing fields OK)
            many: Whether to validate a list of objects
            unknown: How to handle unknown fields: "RAISE", "EXCLUDE", or "INCLUDE"
        """
        super().__init__(schema=schema, strict=strict, partial=partial, many=many)
        
        # Handle both class and instance
        if isinstance(schema, type):
            self._schema_instance = schema()
        else:
            self._schema_instance = schema
        
        self.unknown = unknown
        self.name = f"MarshmallowValidator[{type(self._schema_instance).__name__}]"
    
    def validate_schema(
        self,
        data: Any,
        context: Optional[ValidationContext] = None,
    ) -> SchemaValidationResult[T]:
        """Validate data against Marshmallow schema."""
        try:
            from marshmallow import ValidationError as MarshmallowValidationError
            from marshmallow import EXCLUDE, INCLUDE, RAISE
        except ImportError:
            return SchemaValidationResult.failure([{
                "message": "Marshmallow is not installed",
                "code": "dependency_error",
            }])
        
        # Map unknown setting
        unknown_map = {
            "RAISE": RAISE,
            "EXCLUDE": EXCLUDE,
            "INCLUDE": INCLUDE,
        }
        unknown_setting = unknown_map.get(self.unknown.upper(), RAISE)
        
        try:
            # Configure schema
            self._schema_instance.unknown = unknown_setting
            
            # Load (validate and deserialize)
            result = self._schema_instance.load(
                data,
                partial=self.partial,
                many=self.many,
            )
            
            return SchemaValidationResult.success(result)
        
        except MarshmallowValidationError as e:
            errors = self._convert_marshmallow_errors(e.messages)
            return SchemaValidationResult.failure(errors, raw_errors=e)
        except Exception as e:
            return SchemaValidationResult.failure([{
                "message": f"Validation error: {str(e)}",
                "code": "validation_error",
            }])
    
    def _convert_marshmallow_errors(
        self,
        messages: Union[Dict[str, Any], List[Any]],
        prefix: str = "",
    ) -> List[Dict[str, Any]]:
        """Convert Marshmallow validation errors to our format."""
        errors: List[Dict[str, Any]] = []
        
        if isinstance(messages, dict):
            for field, field_errors in messages.items():
                path = f"{prefix}.{field}" if prefix else field
                
                if isinstance(field_errors, dict):
                    # Nested errors
                    errors.extend(self._convert_marshmallow_errors(field_errors, path))
                elif isinstance(field_errors, list):
                    for error in field_errors:
                        if isinstance(error, dict):
                            errors.extend(self._convert_marshmallow_errors(error, path))
                        else:
                            errors.append({
                                "field": field,
                                "path": path,
                                "message": str(error),
                                "code": "validation_error",
                            })
                else:
                    errors.append({
                        "field": field,
                        "path": path,
                        "message": str(field_errors),
                        "code": "validation_error",
                    })
        elif isinstance(messages, list):
            for i, error in enumerate(messages):
                if isinstance(error, dict):
                    errors.extend(self._convert_marshmallow_errors(error, f"{prefix}[{i}]"))
                else:
                    errors.append({
                        "field": prefix or None,
                        "path": prefix or None,
                        "message": str(error),
                        "code": "validation_error",
                    })
        
        return errors
    
    def dump(self, obj: Any) -> Dict[str, Any]:
        """Serialize an object using the schema."""
        return self._schema_instance.dump(obj, many=self.many)
    
    def get_schema_dict(self) -> Dict[str, Any]:
        """Get JSON Schema representation (best effort)."""
        try:
            from marshmallow_jsonschema import JSONSchema
            json_schema = JSONSchema()
            return json_schema.dump(self._schema_instance)
        except ImportError:
            # Fallback - manually construct basic schema
            return self._build_basic_schema()
    
    def _build_basic_schema(self) -> Dict[str, Any]:
        """Build basic JSON Schema from Marshmallow fields."""
        properties: Dict[str, Any] = {}
        required: List[str] = []
        
        try:
            from marshmallow import fields
            
            type_map = {
                fields.String: "string",
                fields.Integer: "integer",
                fields.Float: "number",
                fields.Boolean: "boolean",
                fields.List: "array",
                fields.Dict: "object",
                fields.Nested: "object",
                fields.DateTime: "string",
                fields.Date: "string",
                fields.Time: "string",
                fields.UUID: "string",
                fields.Email: "string",
                fields.URL: "string",
            }
            
            for field_name, field_obj in self._schema_instance.fields.items():
                prop: Dict[str, Any] = {}
                
                # Get type
                for field_cls, json_type in type_map.items():
                    if isinstance(field_obj, field_cls):
                        prop["type"] = json_type
                        break
                else:
                    prop["type"] = "string"
                
                # Check required
                if field_obj.required:
                    required.append(field_name)
                
                # Add metadata
                if hasattr(field_obj, "metadata"):
                    if "description" in field_obj.metadata:
                        prop["description"] = field_obj.metadata["description"]
                
                properties[field_name] = prop
        except Exception:
            pass
        
        return {
            "type": "object",
            "properties": properties,
            "required": required,
        }
    
    def get_field_info(self, field_name: str) -> Optional[Dict[str, Any]]:
        """Get field information from Marshmallow schema."""
        try:
            field_obj = self._schema_instance.fields.get(field_name)
            if not field_obj:
                return None
            
            return {
                "name": field_name,
                "type": type(field_obj).__name__,
                "required": field_obj.required,
                "load_default": getattr(field_obj, "load_default", None),
                "dump_default": getattr(field_obj, "dump_default", None),
                "allow_none": field_obj.allow_none,
                "metadata": getattr(field_obj, "metadata", {}),
            }
        except Exception:
            return None
    
    @property
    def required_fields(self) -> List[str]:
        """Get required field names from Marshmallow schema."""
        try:
            return [
                name for name, field in self._schema_instance.fields.items()
                if field.required
            ]
        except Exception:
            return []
    
    @property
    def optional_fields(self) -> List[str]:
        """Get optional field names from Marshmallow schema."""
        try:
            return [
                name for name, field in self._schema_instance.fields.items()
                if not field.required
            ]
        except Exception:
            return []


def create_marshmallow_validator(
    schema: Any,
    partial: bool = False,
    many: bool = False,
    unknown: str = "RAISE",
) -> MarshmallowSchemaValidator[Any]:
    """
    Create a Marshmallow validator for a schema.
    
    Example:
        from marshmallow import Schema, fields
        
        class UserSchema(Schema):
            name = fields.String(required=True)
            age = fields.Integer()
        
        validator = create_marshmallow_validator(UserSchema)
        result = validator.validate({"name": "John", "age": 30})
    """
    return MarshmallowSchemaValidator(
        schema=schema,
        partial=partial,
        many=many,
        unknown=unknown,
    )


def validate_with_marshmallow(
    data: Any,
    schema: Any,
    partial: bool = False,
    many: bool = False,
    raise_on_error: bool = False,
) -> Union[Any, SchemaValidationResult[Any]]:
    """
    Validate data with Marshmallow and return result.
    
    Args:
        data: Data to validate
        schema: Marshmallow Schema class or instance
        partial: Allow partial data
        many: Whether to validate a list
        raise_on_error: Whether to raise on validation failure
    
    Returns:
        Deserialized data if valid, or SchemaValidationResult if not
    
    Example:
        user_data = validate_with_marshmallow(
            {"name": "John", "age": 30},
            UserSchema,
            raise_on_error=True,
        )
    """
    validator = MarshmallowSchemaValidator(
        schema=schema,
        partial=partial,
        many=many,
    )
    result = validator.validate_schema(data)
    
    if raise_on_error and not result.is_valid:
        from validation_infrastructure.errors import SchemaValidationError
        schema_name = (
            schema.__name__ if isinstance(schema, type) else type(schema).__name__
        )
        raise SchemaValidationError(
            schema_name=schema_name,
            message=f"Validation failed for {schema_name}",
            schema_errors=result.errors,
        )
    
    if result.is_valid:
        return result.data
    
    return result


class MarshmallowSchemaBuilder:
    """
    Builder for creating Marshmallow schemas dynamically.
    
    Example:
        schema = (
            MarshmallowSchemaBuilder("UserSchema")
            .add_string("name", required=True)
            .add_integer("age", validate=lambda x: 0 <= x <= 150)
            .add_email("email")
            .build()
        )
    """

    def __init__(self, name: str = "DynamicSchema"):
        self.name = name
        self._fields: Dict[str, Any] = {}
        self._validators: List[Any] = []
    
    def add_field(
        self,
        name: str,
        field_type: Any,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a field with any Marshmallow field type."""
        self._fields[name] = field_type(**kwargs)
        return self
    
    def add_string(
        self,
        name: str,
        required: bool = False,
        allow_none: bool = False,
        validate: Any = None,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a string field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.String(
                required=required,
                allow_none=allow_none,
                validate=validate,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_integer(
        self,
        name: str,
        required: bool = False,
        allow_none: bool = False,
        validate: Any = None,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add an integer field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.Integer(
                required=required,
                allow_none=allow_none,
                validate=validate,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_float(
        self,
        name: str,
        required: bool = False,
        allow_none: bool = False,
        validate: Any = None,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a float field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.Float(
                required=required,
                allow_none=allow_none,
                validate=validate,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_boolean(
        self,
        name: str,
        required: bool = False,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a boolean field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.Boolean(required=required, **kwargs)
        except ImportError:
            pass
        return self
    
    def add_email(
        self,
        name: str,
        required: bool = False,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add an email field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.Email(required=required, **kwargs)
        except ImportError:
            pass
        return self
    
    def add_url(
        self,
        name: str,
        required: bool = False,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a URL field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.URL(required=required, **kwargs)
        except ImportError:
            pass
        return self
    
    def add_datetime(
        self,
        name: str,
        required: bool = False,
        format: Optional[str] = None,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a datetime field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.DateTime(
                required=required,
                format=format,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_list(
        self,
        name: str,
        inner_field: Any,
        required: bool = False,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a list field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.List(
                inner_field,
                required=required,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_nested(
        self,
        name: str,
        nested_schema: Any,
        required: bool = False,
        many: bool = False,
        **kwargs: Any,
    ) -> MarshmallowSchemaBuilder:
        """Add a nested schema field."""
        try:
            from marshmallow import fields
            self._fields[name] = fields.Nested(
                nested_schema,
                required=required,
                many=many,
                **kwargs,
            )
        except ImportError:
            pass
        return self
    
    def add_validator(self, validator: Any) -> MarshmallowSchemaBuilder:
        """Add a schema-level validator."""
        self._validators.append(validator)
        return self
    
    def build(self) -> Any:
        """Build and return the Marshmallow schema class."""
        try:
            from marshmallow import Schema, validates_schema
        except ImportError:
            raise ImportError("Marshmallow is required for MarshmallowSchemaBuilder")
        
        # Create class dynamically
        schema_dict: Dict[str, Any] = dict(self._fields)
        
        # Add validators
        for i, validator in enumerate(self._validators):
            @validates_schema
            def validate_schema(self, data: Dict[str, Any], **kwargs: Any) -> None:
                validator(data)
            
            schema_dict[f"_validate_{i}"] = validate_schema
        
        return type(self.name, (Schema,), schema_dict)
