"""
Validation Infrastructure v3.1 - Comprehensive Python Validation System.

A robust validation framework providing type checking, schema validation,
custom decorators, data sanitization, and extensive validation tools.
"""

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationResult,
)
from validation_infrastructure.core.types import (
    TypeValidator,
    validate_type,
)
from validation_infrastructure.decorators import (
    validate_params,
    validate_return,
    validated,
)
from validation_infrastructure.errors import (
    ValidationError,
    ValidationErrorCollection,
    AggregatedValidationError,
)
from validation_infrastructure.schemas import (
    SchemaValidator,
    PydanticSchemaValidator,
    MarshmallowSchemaValidator,
)

__version__ = "3.1.0"
__author__ = "Validation Infrastructure Team"

__all__ = [
    # Core
    "BaseValidator",
    "ValidationContext",
    "ValidationResult",
    # Types
    "TypeValidator",
    "validate_type",
    # Decorators
    "validate_params",
    "validate_return",
    "validated",
    # Errors
    "ValidationError",
    "ValidationErrorCollection",
    "AggregatedValidationError",
    # Schemas
    "SchemaValidator",
    "PydanticSchemaValidator",
    "MarshmallowSchemaValidator",
    # Version
    "__version__",
]
