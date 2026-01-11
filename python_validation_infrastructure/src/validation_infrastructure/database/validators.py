"""Database field validators for SQLAlchemy and Django ORM.

Provides validators that integrate with ORM models for
constraint enforcement and data integrity.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any, Callable, Generic, TypeVar

from validation_infrastructure.core.base import BaseValidator, ValidationResult
from validation_infrastructure.errors.exceptions import ValidationError


T = TypeVar("T")


# ============================================================================
# Base Database Validator
# ============================================================================

@dataclass
class FieldConstraint:
    """Represents a database field constraint.
    
    Attributes:
        name: Constraint name.
        column: Column name.
        check: Validation function.
        message: Error message template.
    """
    name: str
    column: str
    check: Callable[[Any], bool]
    message: str = "Constraint {name} violated for {column}"
    
    def validate(self, value: Any) -> bool:
        """Check if value satisfies constraint."""
        return self.check(value)
    
    def get_error_message(self) -> str:
        """Get formatted error message."""
        return self.message.format(name=self.name, column=self.column)


class DatabaseValidator(ABC, Generic[T]):
    """Base class for database validators."""
    
    @abstractmethod
    def validate_model(self, instance: T) -> ValidationResult:
        """Validate a model instance."""
        ...
    
    @abstractmethod
    def validate_field(self, field_name: str, value: Any) -> ValidationResult:
        """Validate a single field value."""
        ...


# ============================================================================
# SQLAlchemy Integration
# ============================================================================

class SQLAlchemyValidator(DatabaseValidator[Any]):
    """Validator for SQLAlchemy models.
    
    Validates model instances against column constraints,
    unique constraints, and custom validation rules.
    
    Usage:
        from sqlalchemy import Column, Integer, String
        from sqlalchemy.orm import declarative_base
        
        Base = declarative_base()
        
        class User(Base):
            __tablename__ = "users"
            id = Column(Integer, primary_key=True)
            name = Column(String(100), nullable=False)
            email = Column(String(255), unique=True, nullable=False)
        
        validator = SQLAlchemyValidator(User)
        result = validator.validate_model(user_instance)
    """
    
    def __init__(self, model_class: type) -> None:
        """Initialize validator for a model class.
        
        Args:
            model_class: SQLAlchemy model class.
        """
        self.model_class = model_class
        self._constraints: list[FieldConstraint] = []
        self._custom_validators: dict[str, list[Callable[[Any], bool]]] = {}
        
        # Extract constraints from model
        self._extract_constraints()
    
    def _extract_constraints(self) -> None:
        """Extract constraints from SQLAlchemy model."""
        try:
            from sqlalchemy import inspect
            from sqlalchemy.orm import class_mapper
        except ImportError:
            return
        
        try:
            mapper = class_mapper(self.model_class)
        except Exception:
            return
        
        for column in mapper.columns:
            # Nullable constraint
            if not column.nullable and not column.primary_key:
                self._constraints.append(FieldConstraint(
                    name="not_null",
                    column=column.name,
                    check=lambda v: v is not None,
                    message=f"Field '{column.name}' cannot be null",
                ))
            
            # String length constraint
            if hasattr(column.type, "length") and column.type.length:
                max_len = column.type.length
                self._constraints.append(FieldConstraint(
                    name="max_length",
                    column=column.name,
                    check=lambda v, ml=max_len: v is None or len(str(v)) <= ml,
                    message=f"Field '{column.name}' exceeds max length {max_len}",
                ))
    
    def add_validator(
        self,
        field_name: str,
        validator: Callable[[Any], bool],
    ) -> "SQLAlchemyValidator":
        """Add a custom validator for a field.
        
        Args:
            field_name: Name of the field to validate.
            validator: Validation function returning bool.
        
        Returns:
            Self for chaining.
        """
        if field_name not in self._custom_validators:
            self._custom_validators[field_name] = []
        self._custom_validators[field_name].append(validator)
        return self
    
    def validate_model(self, instance: Any) -> ValidationResult:
        """Validate a model instance.
        
        Args:
            instance: SQLAlchemy model instance.
        
        Returns:
            ValidationResult with errors.
        """
        errors: list[str] = []
        
        # Check constraints
        for constraint in self._constraints:
            value = getattr(instance, constraint.column, None)
            if not constraint.validate(value):
                errors.append(constraint.get_error_message())
        
        # Check custom validators
        for field_name, validators in self._custom_validators.items():
            value = getattr(instance, field_name, None)
            for validator in validators:
                if not validator(value):
                    errors.append(f"Custom validation failed for '{field_name}'")
        
        return ValidationResult(
            is_valid=len(errors) == 0,
            errors=errors,
        )
    
    def validate_field(self, field_name: str, value: Any) -> ValidationResult:
        """Validate a single field value.
        
        Args:
            field_name: Name of the field.
            value: Value to validate.
        
        Returns:
            ValidationResult with errors.
        """
        errors: list[str] = []
        
        # Check constraints for this field
        for constraint in self._constraints:
            if constraint.column == field_name:
                if not constraint.validate(value):
                    errors.append(constraint.get_error_message())
        
        # Check custom validators
        for validator in self._custom_validators.get(field_name, []):
            if not validator(value):
                errors.append(f"Custom validation failed for '{field_name}'")
        
        return ValidationResult(
            is_valid=len(errors) == 0,
            errors=errors,
        )
    
    def validate_unique(
        self,
        session: Any,
        instance: Any,
        field_name: str,
    ) -> ValidationResult:
        """Validate uniqueness constraint against database.
        
        Args:
            session: SQLAlchemy session.
            instance: Model instance to validate.
            field_name: Field to check for uniqueness.
        
        Returns:
            ValidationResult.
        """
        value = getattr(instance, field_name, None)
        if value is None:
            return ValidationResult(is_valid=True)
        
        # Check if another record exists with this value
        query = session.query(self.model_class).filter(
            getattr(self.model_class, field_name) == value
        )
        
        # Exclude current instance if it has an ID
        pk = getattr(instance, "id", None)
        if pk is not None:
            query = query.filter(self.model_class.id != pk)
        
        exists = query.first() is not None
        
        if exists:
            return ValidationResult(
                is_valid=False,
                errors=[f"Value '{value}' already exists for '{field_name}'"],
            )
        
        return ValidationResult(is_valid=True)


# ============================================================================
# Django ORM Integration
# ============================================================================

class DjangoValidator(DatabaseValidator[Any]):
    """Validator for Django models.
    
    Integrates with Django's model validation system while
    providing additional validation capabilities.
    
    Usage:
        from django.db import models
        
        class User(models.Model):
            name = models.CharField(max_length=100)
            email = models.EmailField(unique=True)
        
        validator = DjangoValidator(User)
        result = validator.validate_model(user_instance)
    """
    
    def __init__(self, model_class: type) -> None:
        """Initialize validator for a Django model class.
        
        Args:
            model_class: Django model class.
        """
        self.model_class = model_class
        self._custom_validators: dict[str, list[Callable[[Any], bool]]] = {}
    
    def add_validator(
        self,
        field_name: str,
        validator: Callable[[Any], bool],
    ) -> "DjangoValidator":
        """Add a custom validator for a field.
        
        Args:
            field_name: Name of the field.
            validator: Validation function.
        
        Returns:
            Self for chaining.
        """
        if field_name not in self._custom_validators:
            self._custom_validators[field_name] = []
        self._custom_validators[field_name].append(validator)
        return self
    
    def validate_model(self, instance: Any) -> ValidationResult:
        """Validate a Django model instance.
        
        Uses Django's built-in full_clean() plus custom validators.
        
        Args:
            instance: Django model instance.
        
        Returns:
            ValidationResult with errors.
        """
        errors: list[str] = []
        
        # Use Django's validation
        try:
            instance.full_clean()
        except Exception as e:
            # Django ValidationError has a message_dict or messages
            if hasattr(e, "message_dict"):
                for field, messages in e.message_dict.items():
                    for msg in messages:
                        errors.append(f"{field}: {msg}")
            elif hasattr(e, "messages"):
                errors.extend(e.messages)
            else:
                errors.append(str(e))
        
        # Custom validators
        for field_name, validators in self._custom_validators.items():
            value = getattr(instance, field_name, None)
            for validator in validators:
                if not validator(value):
                    errors.append(f"Custom validation failed for '{field_name}'")
        
        return ValidationResult(
            is_valid=len(errors) == 0,
            errors=errors,
        )
    
    def validate_field(self, field_name: str, value: Any) -> ValidationResult:
        """Validate a single field value.
        
        Args:
            field_name: Name of the field.
            value: Value to validate.
        
        Returns:
            ValidationResult.
        """
        errors: list[str] = []
        
        # Get field from model
        try:
            model_field = self.model_class._meta.get_field(field_name)
            
            # Run field validators
            for validator in model_field.validators:
                try:
                    validator(value)
                except Exception as e:
                    errors.append(str(e))
            
            # Check null constraint
            if not model_field.null and value is None:
                errors.append(f"Field '{field_name}' cannot be null")
            
            # Check max_length for char fields
            if hasattr(model_field, "max_length") and model_field.max_length:
                if value and len(str(value)) > model_field.max_length:
                    errors.append(
                        f"Field '{field_name}' exceeds max length {model_field.max_length}"
                    )
        
        except Exception:
            pass  # Field not found or other error
        
        # Custom validators
        for validator in self._custom_validators.get(field_name, []):
            if not validator(value):
                errors.append(f"Custom validation failed for '{field_name}'")
        
        return ValidationResult(
            is_valid=len(errors) == 0,
            errors=errors,
        )


# ============================================================================
# Generic Database Validators
# ============================================================================

class UniqueValidator(BaseValidator[tuple[Any, str, Any]]):
    """Validates uniqueness against a database.
    
    Generic validator that works with any database connection.
    """
    
    def __init__(
        self,
        query_fn: Callable[[str, Any], bool],
    ) -> None:
        """Initialize validator.
        
        Args:
            query_fn: Function that checks if value exists.
                      Takes (field_name, value) and returns bool.
        """
        self.query_fn = query_fn
    
    def validate(
        self,
        value: tuple[Any, str, Any],
        context: dict[str, Any] | None = None,
    ) -> ValidationResult:
        """Validate uniqueness.
        
        Args:
            value: Tuple of (instance, field_name, value).
            context: Optional context with 'exclude_id'.
        
        Returns:
            ValidationResult.
        """
        _, field_name, field_value = value
        
        if self.query_fn(field_name, field_value):
            return ValidationResult(
                is_valid=False,
                errors=[f"Value already exists for '{field_name}'"],
            )
        
        return ValidationResult(is_valid=True)


class ForeignKeyValidator(BaseValidator[tuple[str, Any]]):
    """Validates foreign key references exist."""
    
    def __init__(
        self,
        exists_fn: Callable[[str, Any], bool],
    ) -> None:
        """Initialize validator.
        
        Args:
            exists_fn: Function that checks if referenced record exists.
                       Takes (table_name, id) and returns bool.
        """
        self.exists_fn = exists_fn
    
    def validate(
        self,
        value: tuple[str, Any],
        context: dict[str, Any] | None = None,
    ) -> ValidationResult:
        """Validate foreign key exists.
        
        Args:
            value: Tuple of (table_name, foreign_key_id).
            context: Optional validation context.
        
        Returns:
            ValidationResult.
        """
        table_name, fk_id = value
        
        if fk_id is None:
            return ValidationResult(is_valid=True)
        
        if not self.exists_fn(table_name, fk_id):
            return ValidationResult(
                is_valid=False,
                errors=[f"Referenced {table_name} with id={fk_id} does not exist"],
            )
        
        return ValidationResult(is_valid=True)
