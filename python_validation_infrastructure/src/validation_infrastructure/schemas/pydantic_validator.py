"""Pydantic schema validation integration."""

from __future__ import annotations

from typing import Any, Dict, List, Optional, Type, TypeVar, Union

from validation_infrastructure.core.base import ValidationContext
from validation_infrastructure.schemas.base import SchemaValidationResult, SchemaValidator

T = TypeVar("T")


class PydanticSchemaValidator(SchemaValidator[T]):
    """Validator using Pydantic models."""

    def __init__(
        self,
        model: Type[T],
        strict: bool = False,
        from_attributes: bool = False,
        validate_default: bool = False,
    ):
        """
        Initialize Pydantic validator.
        
        Args:
            model: Pydantic model class (v2+ BaseModel)
            strict: Whether to use strict validation mode
            from_attributes: Whether to validate from object attributes
            validate_default: Whether to validate default values
        """
        super().__init__(schema=model, strict=strict)
        self.model = model
        self.from_attributes = from_attributes
        self.validate_default = validate_default
        self.name = f"PydanticValidator[{model.__name__}]"
    
    def validate_schema(
        self,
        data: Any,
        context: Optional[ValidationContext] = None,
    ) -> SchemaValidationResult[T]:
        """Validate data against Pydantic model."""
        try:
            from pydantic import ValidationError as PydanticValidationError
        except ImportError:
            return SchemaValidationResult.failure([{
                "message": "Pydantic is not installed",
                "code": "dependency_error",
            }])
        
        try:
            # Handle different input types
            if self.from_attributes and hasattr(data, "__dict__"):
                # Validate from object attributes
                validated = self.model.model_validate(
                    data,
                    strict=self.strict,
                    from_attributes=True,
                )
            elif isinstance(data, dict):
                validated = self.model.model_validate(
                    data,
                    strict=self.strict,
                )
            else:
                # Try to parse as the model type
                validated = self.model.model_validate(data, strict=self.strict)
            
            return SchemaValidationResult.success(validated)
        
        except PydanticValidationError as e:
            errors = self._convert_pydantic_errors(e)
            return SchemaValidationResult.failure(errors, raw_errors=e)
        except Exception as e:
            return SchemaValidationResult.failure([{
                "message": f"Validation error: {str(e)}",
                "code": "validation_error",
            }])
    
    def _convert_pydantic_errors(self, error: Any) -> List[Dict[str, Any]]:
        """Convert Pydantic validation errors to our format."""
        errors: List[Dict[str, Any]] = []
        
        for err in error.errors():
            loc = err.get("loc", ())
            field = ".".join(str(x) for x in loc) if loc else None
            
            errors.append({
                "field": field,
                "path": field,
                "message": err.get("msg", "Validation failed"),
                "code": err.get("type", "validation_error"),
                "input": err.get("input"),
                "ctx": err.get("ctx"),
            })
        
        return errors
    
    def get_schema_dict(self) -> Dict[str, Any]:
        """Get JSON Schema from Pydantic model."""
        try:
            return self.model.model_json_schema()
        except Exception:
            return {"type": "object"}
    
    def get_field_info(self, field_name: str) -> Optional[Dict[str, Any]]:
        """Get field information from Pydantic model."""
        try:
            fields = self.model.model_fields
            if field_name not in fields:
                return None
            
            field = fields[field_name]
            return {
                "name": field_name,
                "annotation": str(field.annotation),
                "required": field.is_required(),
                "default": field.default if not field.is_required() else None,
                "description": field.description,
            }
        except Exception:
            return None
    
    @property
    def required_fields(self) -> List[str]:
        """Get required field names from Pydantic model."""
        try:
            return [
                name for name, field in self.model.model_fields.items()
                if field.is_required()
            ]
        except Exception:
            return []
    
    @property
    def optional_fields(self) -> List[str]:
        """Get optional field names from Pydantic model."""
        try:
            return [
                name for name, field in self.model.model_fields.items()
                if not field.is_required()
            ]
        except Exception:
            return []


def create_pydantic_validator(
    model: Type[T],
    strict: bool = False,
    from_attributes: bool = False,
) -> PydanticSchemaValidator[T]:
    """
    Create a Pydantic validator for a model.
    
    Example:
        from pydantic import BaseModel
        
        class User(BaseModel):
            name: str
            age: int
        
        validator = create_pydantic_validator(User)
        result = validator.validate({"name": "John", "age": 30})
    """
    return PydanticSchemaValidator(
        model=model,
        strict=strict,
        from_attributes=from_attributes,
    )


def validate_with_pydantic(
    data: Any,
    model: Type[T],
    strict: bool = False,
    raise_on_error: bool = False,
) -> Union[T, SchemaValidationResult[T]]:
    """
    Validate data with Pydantic and return model instance or result.
    
    Args:
        data: Data to validate
        model: Pydantic model class
        strict: Whether to use strict validation
        raise_on_error: Whether to raise on validation failure
    
    Returns:
        Model instance if valid, or SchemaValidationResult if not
    
    Example:
        user = validate_with_pydantic(
            {"name": "John", "age": 30},
            User,
            raise_on_error=True,
        )
    """
    validator = PydanticSchemaValidator(model=model, strict=strict)
    result = validator.validate_schema(data)
    
    if raise_on_error and not result.is_valid:
        from validation_infrastructure.errors import SchemaValidationError
        raise SchemaValidationError(
            schema_name=model.__name__,
            message=f"Validation failed for {model.__name__}",
            schema_errors=result.errors,
        )
    
    if result.is_valid:
        return result.data  # type: ignore
    
    return result


class PydanticModelBuilder:
    """
    Builder for creating Pydantic models dynamically.
    
    Example:
        model = (
            PydanticModelBuilder("User")
            .add_field("name", str, required=True)
            .add_field("age", int, default=0)
            .add_field("email", str, pattern=r".*@.*")
            .build()
        )
    """

    def __init__(self, name: str):
        self.name = name
        self._fields: Dict[str, Any] = {}
        self._validators: Dict[str, Any] = {}
        self._config: Dict[str, Any] = {}
    
    def add_field(
        self,
        name: str,
        field_type: type,
        required: bool = True,
        default: Any = ...,
        description: Optional[str] = None,
        min_length: Optional[int] = None,
        max_length: Optional[int] = None,
        pattern: Optional[str] = None,
        ge: Optional[float] = None,
        le: Optional[float] = None,
        gt: Optional[float] = None,
        lt: Optional[float] = None,
    ) -> PydanticModelBuilder:
        """Add a field to the model."""
        try:
            from pydantic import Field
        except ImportError:
            raise ImportError("Pydantic is required for PydanticModelBuilder")
        
        field_kwargs: Dict[str, Any] = {}
        
        if not required:
            if default is ...:
                default = None
            field_kwargs["default"] = default
        
        if description:
            field_kwargs["description"] = description
        if min_length is not None:
            field_kwargs["min_length"] = min_length
        if max_length is not None:
            field_kwargs["max_length"] = max_length
        if pattern is not None:
            field_kwargs["pattern"] = pattern
        if ge is not None:
            field_kwargs["ge"] = ge
        if le is not None:
            field_kwargs["le"] = le
        if gt is not None:
            field_kwargs["gt"] = gt
        if lt is not None:
            field_kwargs["lt"] = lt
        
        self._fields[name] = (field_type, Field(**field_kwargs) if field_kwargs else ...)
        return self
    
    def add_validator(
        self,
        field_name: str,
        validator_func: Any,
        mode: str = "after",
    ) -> PydanticModelBuilder:
        """Add a field validator."""
        self._validators[field_name] = (validator_func, mode)
        return self
    
    def configure(self, **options: Any) -> PydanticModelBuilder:
        """Configure model options."""
        self._config.update(options)
        return self
    
    def build(self) -> type:
        """Build and return the Pydantic model class."""
        try:
            from pydantic import create_model, field_validator
        except ImportError:
            raise ImportError("Pydantic is required for PydanticModelBuilder")
        
        # Create the model
        model = create_model(self.name, **self._fields)
        
        # Add validators
        for field_name, (func, mode) in self._validators.items():
            validator = field_validator(field_name, mode=mode)(func)
            setattr(model, f"validate_{field_name}", validator)
        
        return model
