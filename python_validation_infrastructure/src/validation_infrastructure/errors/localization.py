"""Error message localization support."""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Union
import threading


@dataclass
class LocalizedMessage:
    """A message with localization support."""

    key: str
    default: str
    params: Dict[str, Any] = field(default_factory=dict)
    
    def format(self, locale: str, localizer: ErrorLocalizer) -> str:
        """Format the message for the given locale."""
        template = localizer.get_message(self.key, locale, self.default)
        try:
            return template.format(**self.params)
        except KeyError:
            return template


# Default error messages for English
DEFAULT_MESSAGES: Dict[str, str] = {
    # Type errors
    "type_error": "Expected {expected_type}, got {actual_type}",
    "type_error_null": "Value cannot be None",
    "type_coerced": "Value coerced from {from_type} to {to_type}",
    
    # String errors
    "string_too_short": "String length {actual} is less than minimum {minimum}",
    "string_too_long": "String length {actual} exceeds maximum {maximum}",
    "string_empty": "String cannot be empty",
    "pattern_mismatch": "String does not match required pattern: {pattern}",
    
    # Numeric errors
    "below_minimum": "Value {value} is less than minimum {minimum}",
    "above_maximum": "Value {value} exceeds maximum {maximum}",
    "below_exclusive_minimum": "Value {value} must be greater than {minimum}",
    "above_exclusive_maximum": "Value {value} must be less than {maximum}",
    "not_multiple_of": "Value {value} is not a multiple of {multiple_of}",
    "nan_not_allowed": "NaN values are not allowed",
    "infinity_not_allowed": "Infinite values are not allowed",
    
    # Collection errors
    "array_too_short": "Array has {actual} items, minimum is {minimum}",
    "array_too_long": "Array has {actual} items, maximum is {maximum}",
    "unique_items": "Array items must be unique",
    "item_validation_failed": "Item at index {index} failed validation",
    
    # Object/Dict errors
    "missing_field": "Missing required field: {field}",
    "extra_field": "Unknown field: {field}",
    "field_validation_failed": "Field '{field}' failed validation",
    
    # Date/Time errors
    "invalid_date_format": "Cannot parse date from string: {value}",
    "date_too_early": "Date must be after {minimum}",
    "date_too_late": "Date must be before {maximum}",
    "past_not_allowed": "Past dates are not allowed",
    "future_not_allowed": "Future dates are not allowed",
    
    # UUID errors
    "invalid_uuid": "Invalid UUID format: {value}",
    "invalid_uuid_version": "UUID version {version} not allowed",
    
    # Path errors
    "path_not_found": "Path does not exist: {path}",
    "not_a_file": "Path is not a file: {path}",
    "not_a_directory": "Path is not a directory: {path}",
    "invalid_extension": "File extension '{extension}' not allowed",
    
    # Enum errors
    "invalid_enum": "Invalid value: {value}. Valid values: {valid_values}",
    
    # Schema errors
    "schema_validation_failed": "Schema validation failed",
    "schema_error": "Schema error: {message}",
    
    # File errors
    "file_too_large": "File size {size} exceeds limit {limit}",
    "file_type_not_allowed": "File type '{type}' is not allowed",
    "file_content_invalid": "File content validation failed",
    
    # Environment errors
    "env_var_missing": "Required environment variable '{name}' is not set",
    "env_var_invalid": "Environment variable '{name}' has invalid value",
    
    # Config errors
    "config_parse_error": "Failed to parse configuration: {error}",
    "config_validation_failed": "Configuration validation failed",
    
    # General errors
    "validation_failed": "Validation failed",
    "constraint_violation": "Constraint '{constraint}' violated",
    "custom_validation_failed": "Custom validation failed: {message}",
}


class ErrorLocalizer:
    """
    Manages error message localization.
    
    Thread-safe singleton pattern for global access.
    """

    _instance: Optional[ErrorLocalizer] = None
    _lock = threading.Lock()
    
    def __new__(cls) -> ErrorLocalizer:
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance
    
    def __init__(self):
        if self._initialized:
            return
        
        self._messages: Dict[str, Dict[str, str]] = {"en": DEFAULT_MESSAGES.copy()}
        self._current_locale = "en"
        self._fallback_locale = "en"
        self._message_formatters: Dict[str, Callable[[str, Dict[str, Any]], str]] = {}
        self._initialized = True
    
    @property
    def current_locale(self) -> str:
        """Get the current locale."""
        return self._current_locale
    
    @current_locale.setter
    def current_locale(self, locale: str) -> None:
        """Set the current locale."""
        self._current_locale = locale
    
    @property
    def available_locales(self) -> List[str]:
        """Get list of available locales."""
        return list(self._messages.keys())
    
    def load_messages(
        self,
        locale: str,
        messages: Dict[str, str],
        merge: bool = True,
    ) -> None:
        """Load messages for a locale."""
        if merge and locale in self._messages:
            self._messages[locale].update(messages)
        else:
            self._messages[locale] = messages.copy()
    
    def load_from_file(self, filepath: Union[str, Path], locale: Optional[str] = None) -> None:
        """Load messages from a JSON file."""
        path = Path(filepath)
        
        if not path.exists():
            raise FileNotFoundError(f"Locale file not found: {path}")
        
        with open(path, "r", encoding="utf-8") as f:
            data = json.load(f)
        
        if locale is None:
            # Assume filename is locale code (e.g., 'de.json')
            locale = path.stem
        
        self.load_messages(locale, data)
    
    def load_from_directory(self, dirpath: Union[str, Path]) -> None:
        """Load all locale files from a directory."""
        path = Path(dirpath)
        
        if not path.is_dir():
            raise NotADirectoryError(f"Locale directory not found: {path}")
        
        for file in path.glob("*.json"):
            self.load_from_file(file)
    
    def get_message(
        self,
        key: str,
        locale: Optional[str] = None,
        default: Optional[str] = None,
    ) -> str:
        """Get a message by key for the specified locale."""
        locale = locale or self._current_locale
        
        # Try requested locale
        if locale in self._messages and key in self._messages[locale]:
            return self._messages[locale][key]
        
        # Try fallback locale
        if locale != self._fallback_locale:
            if self._fallback_locale in self._messages:
                if key in self._messages[self._fallback_locale]:
                    return self._messages[self._fallback_locale][key]
        
        # Return default or key
        return default or key
    
    def format_message(
        self,
        key: str,
        params: Optional[Dict[str, Any]] = None,
        locale: Optional[str] = None,
    ) -> str:
        """Get and format a message with parameters."""
        template = self.get_message(key, locale)
        params = params or {}
        
        # Check for custom formatter
        if key in self._message_formatters:
            return self._message_formatters[key](template, params)
        
        try:
            return template.format(**params)
        except KeyError:
            # Return template with unfilled placeholders
            return template
    
    def register_formatter(
        self,
        key: str,
        formatter: Callable[[str, Dict[str, Any]], str],
    ) -> None:
        """Register a custom formatter for a message key."""
        self._message_formatters[key] = formatter
    
    def create_message(
        self,
        key: str,
        params: Optional[Dict[str, Any]] = None,
        default: Optional[str] = None,
    ) -> LocalizedMessage:
        """Create a LocalizedMessage object."""
        return LocalizedMessage(
            key=key,
            default=default or self.get_message(key),
            params=params or {},
        )


# Global functions for convenience
def get_localizer() -> ErrorLocalizer:
    """Get the global error localizer instance."""
    return ErrorLocalizer()


def set_locale(locale: str) -> None:
    """Set the current locale globally."""
    get_localizer().current_locale = locale


def localize(
    key: str,
    params: Optional[Dict[str, Any]] = None,
    locale: Optional[str] = None,
) -> str:
    """Convenience function to get a localized message."""
    return get_localizer().format_message(key, params, locale)


def load_locale_file(filepath: Union[str, Path], locale: Optional[str] = None) -> None:
    """Convenience function to load a locale file."""
    get_localizer().load_from_file(filepath, locale)


def load_locale_directory(dirpath: Union[str, Path]) -> None:
    """Convenience function to load all locale files from a directory."""
    get_localizer().load_from_directory(dirpath)


# Additional messages for common languages
GERMAN_MESSAGES: Dict[str, str] = {
    "type_error": "Erwartet {expected_type}, erhalten {actual_type}",
    "type_error_null": "Wert darf nicht None sein",
    "string_too_short": "Zeichenkettenlänge {actual} ist kleiner als Minimum {minimum}",
    "string_too_long": "Zeichenkettenlänge {actual} überschreitet Maximum {maximum}",
    "string_empty": "Zeichenkette darf nicht leer sein",
    "below_minimum": "Wert {value} ist kleiner als Minimum {minimum}",
    "above_maximum": "Wert {value} überschreitet Maximum {maximum}",
    "missing_field": "Pflichtfeld fehlt: {field}",
    "validation_failed": "Validierung fehlgeschlagen",
}

FRENCH_MESSAGES: Dict[str, str] = {
    "type_error": "Attendu {expected_type}, reçu {actual_type}",
    "type_error_null": "La valeur ne peut pas être None",
    "string_too_short": "La longueur {actual} est inférieure au minimum {minimum}",
    "string_too_long": "La longueur {actual} dépasse le maximum {maximum}",
    "string_empty": "La chaîne ne peut pas être vide",
    "below_minimum": "La valeur {value} est inférieure au minimum {minimum}",
    "above_maximum": "La valeur {value} dépasse le maximum {maximum}",
    "missing_field": "Champ obligatoire manquant: {field}",
    "validation_failed": "Échec de la validation",
}

SPANISH_MESSAGES: Dict[str, str] = {
    "type_error": "Esperado {expected_type}, recibido {actual_type}",
    "type_error_null": "El valor no puede ser None",
    "string_too_short": "La longitud {actual} es menor que el mínimo {minimum}",
    "string_too_long": "La longitud {actual} excede el máximo {maximum}",
    "string_empty": "La cadena no puede estar vacía",
    "below_minimum": "El valor {value} es menor que el mínimo {minimum}",
    "above_maximum": "El valor {value} excede el máximo {maximum}",
    "missing_field": "Campo requerido faltante: {field}",
    "validation_failed": "Validación fallida",
}


def register_default_languages() -> None:
    """Register default language packs."""
    localizer = get_localizer()
    localizer.load_messages("de", GERMAN_MESSAGES)
    localizer.load_messages("fr", FRENCH_MESSAGES)
    localizer.load_messages("es", SPANISH_MESSAGES)
