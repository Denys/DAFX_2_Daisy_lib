"""Validation error handling and aggregation with localization support."""

from validation_infrastructure.errors.exceptions import (
    ValidationError,
    ValidationErrorCollection,
    AggregatedValidationError,
    SchemaValidationError,
    TypeValidationError,
    ConstraintViolationError,
    ConfigurationError,
    FileValidationError,
    EnvironmentValidationError,
)
from validation_infrastructure.errors.localization import (
    ErrorLocalizer,
    LocalizedMessage,
    get_localizer,
    set_locale,
)
from validation_infrastructure.errors.formatters import (
    ErrorFormatter,
    JSONErrorFormatter,
    TextErrorFormatter,
    HTMLErrorFormatter,
)

__all__ = [
    # Exceptions
    "ValidationError",
    "ValidationErrorCollection",
    "AggregatedValidationError",
    "SchemaValidationError",
    "TypeValidationError",
    "ConstraintViolationError",
    "ConfigurationError",
    "FileValidationError",
    "EnvironmentValidationError",
    # Localization
    "ErrorLocalizer",
    "LocalizedMessage",
    "get_localizer",
    "set_locale",
    # Formatters
    "ErrorFormatter",
    "JSONErrorFormatter",
    "TextErrorFormatter",
    "HTMLErrorFormatter",
]
