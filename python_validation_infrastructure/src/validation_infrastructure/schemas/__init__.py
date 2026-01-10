"""Schema validation using Pydantic and Marshmallow."""

from validation_infrastructure.schemas.base import (
    SchemaValidator,
    SchemaValidationResult,
)
from validation_infrastructure.schemas.pydantic_validator import (
    PydanticSchemaValidator,
    create_pydantic_validator,
    validate_with_pydantic,
)
from validation_infrastructure.schemas.marshmallow_validator import (
    MarshmallowSchemaValidator,
    create_marshmallow_validator,
    validate_with_marshmallow,
)
from validation_infrastructure.schemas.nested import (
    NestedValidator,
    RecursiveValidator,
    validate_nested,
)

__all__ = [
    # Base
    "SchemaValidator",
    "SchemaValidationResult",
    # Pydantic
    "PydanticSchemaValidator",
    "create_pydantic_validator",
    "validate_with_pydantic",
    # Marshmallow
    "MarshmallowSchemaValidator",
    "create_marshmallow_validator",
    "validate_with_marshmallow",
    # Nested
    "NestedValidator",
    "RecursiveValidator",
    "validate_nested",
]
