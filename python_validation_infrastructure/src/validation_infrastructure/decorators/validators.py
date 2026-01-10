"""Decorator-based validators for functions and methods."""

from __future__ import annotations

import asyncio
import functools
import inspect
from typing import (
    Any,
    Callable,
    Dict,
    Generic,
    List,
    Optional,
    Type,
    TypeVar,
    Union,
    get_type_hints,
    overload,
)

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationResult,
)
from validation_infrastructure.core.types import TypeValidator, validate_type
from validation_infrastructure.errors.exceptions import (
    ValidationError,
    ValidationErrorCollection,
)

F = TypeVar("F", bound=Callable[..., Any])
T = TypeVar("T")


class ParameterValidator:
    """Validates function parameters based on type hints and custom validators."""

    def __init__(
        self,
        validators: Optional[Dict[str, BaseValidator[Any]]] = None,
        validate_types: bool = True,
        coerce_types: bool = False,
        strict: bool = False,
        raise_on_error: bool = True,
    ):
        self.validators = validators or {}
        self.validate_types = validate_types
        self.coerce_types = coerce_types
        self.strict = strict
        self.raise_on_error = raise_on_error
    
    def validate_arguments(
        self,
        func: Callable[..., Any],
        args: tuple,
        kwargs: Dict[str, Any],
    ) -> tuple[tuple, Dict[str, Any], ValidationErrorCollection]:
        """Validate function arguments and return validated/coerced values."""
        sig = inspect.signature(func)
        
        try:
            type_hints = get_type_hints(func)
        except Exception:
            type_hints = {}
        
        # Bind arguments to parameters
        try:
            bound = sig.bind(*args, **kwargs)
            bound.apply_defaults()
        except TypeError as e:
            errors = ValidationErrorCollection()
            errors.add(ValidationError(str(e), code="argument_error"))
            return args, kwargs, errors
        
        errors = ValidationErrorCollection()
        validated_args: Dict[str, Any] = {}
        context = ValidationContext()
        
        for param_name, param_value in bound.arguments.items():
            param_ctx = context.child(param_name)
            validated_value = param_value
            
            # Type validation
            if self.validate_types and param_name in type_hints:
                expected_type = type_hints[param_name]
                type_result = validate_type(
                    param_value,
                    expected_type,
                    coerce=self.coerce_types,
                    strict=self.strict,
                    context=param_ctx,
                )
                
                if not type_result.is_valid:
                    for issue in type_result.issues:
                        errors.add(
                            ValidationError(
                                issue.message,
                                code=issue.code or "type_error",
                                field=param_name,
                                path=issue.path,
                                value=param_value,
                            )
                        )
                elif type_result.value is not None:
                    validated_value = type_result.value
            
            # Custom validator
            if param_name in self.validators:
                validator = self.validators[param_name]
                result = validator.validate(validated_value, param_ctx)
                
                if not result.is_valid:
                    for issue in result.issues:
                        errors.add(
                            ValidationError(
                                issue.message,
                                code=issue.code or "validation_error",
                                field=param_name,
                                path=issue.path,
                                value=validated_value,
                            )
                        )
                elif result.value is not None:
                    validated_value = result.value
            
            validated_args[param_name] = validated_value
        
        # Convert back to args and kwargs
        new_args: List[Any] = []
        new_kwargs: Dict[str, Any] = {}
        
        for param_name, param in sig.parameters.items():
            if param_name in validated_args:
                if param.kind in (
                    inspect.Parameter.POSITIONAL_ONLY,
                    inspect.Parameter.POSITIONAL_OR_KEYWORD,
                ):
                    if len(new_args) < len(args):
                        new_args.append(validated_args[param_name])
                    else:
                        new_kwargs[param_name] = validated_args[param_name]
                elif param.kind == inspect.Parameter.VAR_POSITIONAL:
                    new_args.extend(validated_args[param_name])
                elif param.kind == inspect.Parameter.VAR_KEYWORD:
                    new_kwargs.update(validated_args[param_name])
                else:
                    new_kwargs[param_name] = validated_args[param_name]
        
        return tuple(new_args), new_kwargs, errors


class ReturnValidator:
    """Validates function return values."""

    def __init__(
        self,
        validator: Optional[BaseValidator[Any]] = None,
        validate_type: bool = True,
        coerce_type: bool = False,
        strict: bool = False,
    ):
        self.validator = validator
        self.validate_type = validate_type
        self.coerce_type = coerce_type
        self.strict = strict
    
    def validate_return(
        self,
        func: Callable[..., Any],
        value: Any,
    ) -> tuple[Any, ValidationErrorCollection]:
        """Validate return value."""
        errors = ValidationErrorCollection()
        validated_value = value
        context = ValidationContext()
        
        try:
            type_hints = get_type_hints(func)
        except Exception:
            type_hints = {}
        
        return_type = type_hints.get("return")
        
        # Type validation
        if self.validate_type and return_type is not None:
            from validation_infrastructure.core.types import validate_type as vt
            
            type_result = vt(
                value,
                return_type,
                coerce=self.coerce_type,
                strict=self.strict,
                context=context,
            )
            
            if not type_result.is_valid:
                for issue in type_result.issues:
                    errors.add(
                        ValidationError(
                            issue.message,
                            code=issue.code or "return_type_error",
                            field="return",
                            value=value,
                        )
                    )
            elif type_result.value is not None:
                validated_value = type_result.value
        
        # Custom validator
        if self.validator:
            result = self.validator.validate(validated_value, context)
            
            if not result.is_valid:
                for issue in result.issues:
                    errors.add(
                        ValidationError(
                            issue.message,
                            code=issue.code or "return_validation_error",
                            field="return",
                            value=validated_value,
                        )
                    )
            elif result.value is not None:
                validated_value = result.value
        
        return validated_value, errors


def validate_params(
    validators: Optional[Dict[str, BaseValidator[Any]]] = None,
    *,
    validate_types: bool = True,
    coerce_types: bool = False,
    strict: bool = False,
    raise_on_error: bool = True,
) -> Callable[[F], F]:
    """
    Decorator to validate function parameters.
    
    Args:
        validators: Dict mapping parameter names to validators
        validate_types: Whether to validate against type hints
        coerce_types: Whether to attempt type coercion
        strict: Whether to use strict type checking
        raise_on_error: Whether to raise exception on validation failure
    
    Example:
        @validate_params(
            validators={"age": NumericValidator(minimum=0, maximum=150)},
            validate_types=True,
        )
        def create_user(name: str, age: int) -> User:
            ...
    """
    param_validator = ParameterValidator(
        validators=validators,
        validate_types=validate_types,
        coerce_types=coerce_types,
        strict=strict,
        raise_on_error=raise_on_error,
    )
    
    def decorator(func: F) -> F:
        if asyncio.iscoroutinefunction(func):
            @functools.wraps(func)
            async def async_wrapper(*args: Any, **kwargs: Any) -> Any:
                new_args, new_kwargs, errors = param_validator.validate_arguments(
                    func, args, kwargs
                )
                if errors and raise_on_error:
                    raise errors
                return await func(*new_args, **new_kwargs)
            
            return async_wrapper  # type: ignore
        else:
            @functools.wraps(func)
            def sync_wrapper(*args: Any, **kwargs: Any) -> Any:
                new_args, new_kwargs, errors = param_validator.validate_arguments(
                    func, args, kwargs
                )
                if errors and raise_on_error:
                    raise errors
                return func(*new_args, **new_kwargs)
            
            return sync_wrapper  # type: ignore
    
    return decorator


def validate_return(
    validator: Optional[BaseValidator[Any]] = None,
    *,
    validate_type: bool = True,
    coerce_type: bool = False,
    strict: bool = False,
    raise_on_error: bool = True,
) -> Callable[[F], F]:
    """
    Decorator to validate function return value.
    
    Args:
        validator: Custom validator for return value
        validate_type: Whether to validate against return type hint
        coerce_type: Whether to attempt type coercion
        strict: Whether to use strict type checking
        raise_on_error: Whether to raise exception on validation failure
    
    Example:
        @validate_return(validate_type=True)
        def get_user_count() -> int:
            ...
    """
    return_validator = ReturnValidator(
        validator=validator,
        validate_type=validate_type,
        coerce_type=coerce_type,
        strict=strict,
    )
    
    def decorator(func: F) -> F:
        if asyncio.iscoroutinefunction(func):
            @functools.wraps(func)
            async def async_wrapper(*args: Any, **kwargs: Any) -> Any:
                result = await func(*args, **kwargs)
                validated_result, errors = return_validator.validate_return(func, result)
                if errors and raise_on_error:
                    raise errors
                return validated_result
            
            return async_wrapper  # type: ignore
        else:
            @functools.wraps(func)
            def sync_wrapper(*args: Any, **kwargs: Any) -> Any:
                result = func(*args, **kwargs)
                validated_result, errors = return_validator.validate_return(func, result)
                if errors and raise_on_error:
                    raise errors
                return validated_result
            
            return sync_wrapper  # type: ignore
    
    return decorator


def validated(
    param_validators: Optional[Dict[str, BaseValidator[Any]]] = None,
    return_validator: Optional[BaseValidator[Any]] = None,
    *,
    validate_types: bool = True,
    coerce_types: bool = False,
    strict: bool = False,
    raise_on_error: bool = True,
) -> Callable[[F], F]:
    """
    Combined decorator for parameter and return validation.
    
    Example:
        @validated(
            param_validators={"email": EmailValidator()},
            return_validator=UserValidator(),
        )
        def create_user(email: str, name: str) -> User:
            ...
    """
    def decorator(func: F) -> F:
        # Apply return validation first (inner), then param validation (outer)
        wrapped = func
        
        if return_validator is not None or validate_types:
            wrapped = validate_return(
                return_validator,
                validate_type=validate_types,
                coerce_type=coerce_types,
                strict=strict,
                raise_on_error=raise_on_error,
            )(wrapped)
        
        wrapped = validate_params(
            param_validators,
            validate_types=validate_types,
            coerce_types=coerce_types,
            strict=strict,
            raise_on_error=raise_on_error,
        )(wrapped)
        
        return wrapped  # type: ignore
    
    return decorator


def validate_call(func: F) -> F:
    """
    Simple decorator that validates all parameters against their type hints.
    
    Example:
        @validate_call
        def greet(name: str, age: int) -> str:
            return f"Hello {name}, you are {age} years old"
    """
    return validate_params(validate_types=True, raise_on_error=True)(func)


class ValidatorDecorator:
    """Base class for creating custom validator decorators."""

    def __init__(
        self,
        field: Optional[str] = None,
        mode: str = "after",
        check_fields: bool = True,
    ):
        self.field = field
        self.mode = mode  # "before", "after", "wrap"
        self.check_fields = check_fields
        self._func: Optional[Callable[..., Any]] = None
    
    def __call__(self, func: Callable[..., Any]) -> ValidatorDecorator:
        self._func = func
        functools.update_wrapper(self, func)
        return self
    
    def validate(self, value: Any, info: Optional[Dict[str, Any]] = None) -> Any:
        """Run the validation function."""
        if self._func is None:
            raise RuntimeError("Validator function not set")
        
        sig = inspect.signature(self._func)
        params = list(sig.parameters.keys())
        
        # Determine how to call the function
        if len(params) == 1:
            return self._func(value)
        elif len(params) == 2:
            return self._func(value, info or {})
        else:
            return self._func(value)


def validator(
    field: Optional[str] = None,
    *,
    mode: str = "after",
    check_fields: bool = True,
) -> Callable[[Callable[..., Any]], ValidatorDecorator]:
    """
    Decorator to create a field validator.
    
    Args:
        field: Field name to validate (None for all fields)
        mode: When to run - "before", "after", or "wrap"
        check_fields: Whether to check if field exists
    
    Example:
        class UserModel:
            @validator("email")
            def validate_email(cls, v):
                if "@" not in v:
                    raise ValueError("Invalid email")
                return v.lower()
    """
    def decorator(func: Callable[..., Any]) -> ValidatorDecorator:
        return ValidatorDecorator(field=field, mode=mode, check_fields=check_fields)(func)
    
    return decorator


def field_validator(
    *fields: str,
    mode: str = "after",
    check_fields: bool = True,
) -> Callable[[Callable[..., Any]], ValidatorDecorator]:
    """
    Decorator to create a validator for specific fields.
    
    Example:
        class UserModel:
            @field_validator("username", "email")
            def lowercase(cls, v):
                return v.lower()
    """
    def decorator(func: Callable[..., Any]) -> ValidatorDecorator:
        v = ValidatorDecorator(mode=mode, check_fields=check_fields)(func)
        v._fields = fields
        return v
    
    return decorator


def model_validator(
    *,
    mode: str = "after",
) -> Callable[[Callable[..., Any]], ValidatorDecorator]:
    """
    Decorator to create a model-level validator.
    
    Example:
        class UserModel:
            @model_validator(mode="after")
            def validate_model(cls, values):
                if values.get("password") != values.get("confirm_password"):
                    raise ValueError("Passwords must match")
                return values
    """
    def decorator(func: Callable[..., Any]) -> ValidatorDecorator:
        v = ValidatorDecorator(mode=mode)(func)
        v._is_model_validator = True
        return v
    
    return decorator


class ValidationContext:
    """Context object passed to validators with field information."""
    
    def __init__(
        self,
        field_name: Optional[str] = None,
        data: Optional[Dict[str, Any]] = None,
        config: Optional[Dict[str, Any]] = None,
    ):
        self.field_name = field_name
        self.data = data or {}
        self.config = config or {}
