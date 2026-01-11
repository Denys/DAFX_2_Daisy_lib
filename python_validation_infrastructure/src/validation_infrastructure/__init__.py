"""
Validation Infrastructure v3.2 - Comprehensive Python Validation System.

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

# New in v3.2
from validation_infrastructure import api
from validation_infrastructure import database
from validation_infrastructure import testing
from validation_infrastructure import benchmarking

__version__ = "3.2.0"
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
    # Modules (new in v3.2)
    "api",
    "database",
    "testing",
    "benchmarking",
    # Version
    "__version__",
]

