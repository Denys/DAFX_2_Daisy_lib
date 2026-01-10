"""Environment variable validation."""

from __future__ import annotations

import os
from dataclasses import dataclass, field
from typing import Any, Callable, Dict, Generic, List, Optional, Type, TypeVar, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
)
from validation_infrastructure.errors.exceptions import EnvironmentValidationError

T = TypeVar("T")


@dataclass
class EnvVarSpec(Generic[T]):
    """Specification for an environment variable."""

    name: str
    type: Type[T]
    required: bool = True
    default: Optional[T] = None
    description: str = ""
    choices: Optional[List[T]] = None
    validator: Optional[Callable[[T], bool]] = None
    transformer: Optional[Callable[[str], T]] = None
    secret: bool = False
    deprecated: bool = False
    deprecated_message: str = ""
    alias: Optional[str] = None


def env_var(
    name: str,
    type: Type[T] = str,  # type: ignore
    required: bool = True,
    default: Optional[T] = None,
    description: str = "",
    choices: Optional[List[T]] = None,
    validator: Optional[Callable[[T], bool]] = None,
    transformer: Optional[Callable[[str], T]] = None,
    secret: bool = False,
    deprecated: bool = False,
    deprecated_message: str = "",
    alias: Optional[str] = None,
) -> EnvVarSpec[T]:
    """
    Define an environment variable specification.
    
    Example:
        DATABASE_URL = env_var("DATABASE_URL", str, required=True)
        DEBUG = env_var("DEBUG", bool, default=False)
        LOG_LEVEL = env_var("LOG_LEVEL", str, choices=["DEBUG", "INFO", "ERROR"])
    """
    return EnvVarSpec(
        name=name,
        type=type,
        required=required,
        default=default,
        description=description,
        choices=choices,
        validator=validator,
        transformer=transformer,
        secret=secret,
        deprecated=deprecated,
        deprecated_message=deprecated_message,
        alias=alias,
    )


class EnvSchema:
    """
    Schema for environment variable validation.
    
    Example:
        class AppConfig(EnvSchema):
            DATABASE_URL = env_var("DATABASE_URL", str, required=True)
            DEBUG = env_var("DEBUG", bool, default=False)
            PORT = env_var("PORT", int, default=8000)
            LOG_LEVEL = env_var("LOG_LEVEL", str, choices=["DEBUG", "INFO", "ERROR"], default="INFO")
        
        config = AppConfig.validate()
        print(config.DATABASE_URL)
    """

    def __init__(self, **values: Any):
        for key, value in values.items():
            setattr(self, key, value)
    
    @classmethod
    def get_specs(cls) -> Dict[str, EnvVarSpec[Any]]:
        """Get all environment variable specifications from the schema."""
        specs: Dict[str, EnvVarSpec[Any]] = {}
        
        for name in dir(cls):
            if name.startswith("_"):
                continue
            
            attr = getattr(cls, name)
            if isinstance(attr, EnvVarSpec):
                specs[name] = attr
        
        return specs
    
    @classmethod
    def validate(
        cls,
        env: Optional[Dict[str, str]] = None,
        raise_on_error: bool = True,
    ) -> EnvSchema:
        """
        Validate environment variables against this schema.
        
        Args:
            env: Environment dict (defaults to os.environ)
            raise_on_error: Whether to raise exception on validation failure
        
        Returns:
            Instance of schema class with validated values
        """
        if env is None:
            env = dict(os.environ)
        
        validator = EnvValidator(cls)
        result = validator.validate(env)
        
        if not result.is_valid and raise_on_error:
            errors = [issue.message for issue in result.issues]
            raise EnvironmentValidationError(
                var_name="<multiple>",
                message=f"Environment validation failed: {'; '.join(errors)}",
            )
        
        return cls(**(result.value or {}))
    
    @classmethod
    def generate_dotenv_template(cls, include_defaults: bool = True) -> str:
        """Generate a .env template file content."""
        lines: List[str] = []
        lines.append("# Environment Variables")
        lines.append("")
        
        for name, spec in cls.get_specs().items():
            # Comment with description
            if spec.description:
                lines.append(f"# {spec.description}")
            
            # Show type and requirements
            type_info = f"Type: {spec.type.__name__}"
            if spec.required:
                type_info += ", Required"
            else:
                type_info += ", Optional"
            if spec.choices:
                type_info += f", Choices: {spec.choices}"
            lines.append(f"# {type_info}")
            
            # Deprecated warning
            if spec.deprecated:
                lines.append(f"# DEPRECATED: {spec.deprecated_message}")
            
            # Variable line
            if include_defaults and spec.default is not None:
                lines.append(f"{spec.name}={spec.default}")
            else:
                if spec.required:
                    lines.append(f"{spec.name}=")
                else:
                    lines.append(f"# {spec.name}=")
            
            lines.append("")
        
        return "\n".join(lines)


class EnvValidator(BaseValidator[Dict[str, Any]]):
    """Validates environment variables against a schema."""

    # Type conversion functions
    TYPE_CONVERTERS: Dict[type, Callable[[str], Any]] = {
        str: str,
        int: int,
        float: float,
        bool: lambda v: v.lower() in ("true", "1", "yes", "on"),
        list: lambda v: [x.strip() for x in v.split(",")],
    }

    def __init__(
        self,
        schema: Union[Type[EnvSchema], Dict[str, EnvVarSpec[Any]]],
        prefix: str = "",
        strict: bool = False,
    ):
        super().__init__(name="EnvValidator")
        self.prefix = prefix
        self.strict = strict
        
        if isinstance(schema, type) and issubclass(schema, EnvSchema):
            self.specs = schema.get_specs()
        else:
            self.specs = schema
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Dict[str, Any]]:
        """Validate environment variables."""
        ctx = self._create_context(context)
        
        if value is None:
            value = dict(os.environ)
        
        if not isinstance(value, dict):
            return ValidationResult.from_error(
                f"Expected dict, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        result: ValidationResult[Dict[str, Any]] = ValidationResult.success({})
        validated: Dict[str, Any] = {}
        
        for attr_name, spec in self.specs.items():
            env_name = self.prefix + spec.name
            
            # Check for alias
            raw_value = value.get(env_name)
            if raw_value is None and spec.alias:
                raw_value = value.get(self.prefix + spec.alias)
            
            # Check if missing
            if raw_value is None:
                if spec.required:
                    result.add_issue(
                        ValidationIssue(
                            message=f"Required environment variable '{env_name}' is not set",
                            field=env_name,
                            code="missing_env_var",
                        )
                    )
                    continue
                else:
                    validated[attr_name] = spec.default
                    continue
            
            # Deprecated warning
            if spec.deprecated:
                result.warnings.append(
                    ValidationIssue(
                        message=f"Environment variable '{env_name}' is deprecated: {spec.deprecated_message}",
                        field=env_name,
                        code="deprecated_env_var",
                    )
                )
            
            # Convert type
            try:
                if spec.transformer:
                    converted = spec.transformer(raw_value)
                else:
                    converter = self.TYPE_CONVERTERS.get(spec.type)
                    if converter:
                        converted = converter(raw_value)
                    else:
                        converted = spec.type(raw_value)
            except (ValueError, TypeError) as e:
                result.add_issue(
                    ValidationIssue(
                        message=f"Invalid value for '{env_name}': cannot convert to {spec.type.__name__}",
                        field=env_name,
                        code="type_conversion_error",
                        value=raw_value if not spec.secret else "***",
                    )
                )
                continue
            
            # Check choices
            if spec.choices is not None and converted not in spec.choices:
                result.add_issue(
                    ValidationIssue(
                        message=f"Invalid value for '{env_name}': must be one of {spec.choices}",
                        field=env_name,
                        code="invalid_choice",
                        value=converted if not spec.secret else "***",
                    )
                )
                continue
            
            # Custom validator
            if spec.validator:
                try:
                    if not spec.validator(converted):
                        result.add_issue(
                            ValidationIssue(
                                message=f"Custom validation failed for '{env_name}'",
                                field=env_name,
                                code="custom_validation_failed",
                            )
                        )
                        continue
                except Exception as e:
                    result.add_issue(
                        ValidationIssue(
                            message=f"Validator error for '{env_name}': {e}",
                            field=env_name,
                            code="validator_error",
                        )
                    )
                    continue
            
            validated[attr_name] = converted
        
        result.value = validated
        return result


def validate_env(
    specs: Dict[str, EnvVarSpec[Any]],
    env: Optional[Dict[str, str]] = None,
    prefix: str = "",
    raise_on_error: bool = True,
) -> Dict[str, Any]:
    """
    Validate environment variables against specifications.
    
    Args:
        specs: Dict of variable name to EnvVarSpec
        env: Environment dict (defaults to os.environ)
        prefix: Prefix to add to all variable names
        raise_on_error: Whether to raise on validation failure
    
    Returns:
        Dict of validated values
    
    Example:
        config = validate_env({
            "database_url": env_var("DATABASE_URL", str, required=True),
            "debug": env_var("DEBUG", bool, default=False),
        })
    """
    validator = EnvValidator(specs, prefix=prefix)
    result = validator.validate(env)
    
    if not result.is_valid and raise_on_error:
        errors = [issue.message for issue in result.issues]
        raise EnvironmentValidationError(
            var_name="<multiple>",
            message=f"Environment validation failed: {'; '.join(errors)}",
        )
    
    return result.value or {}


def load_dotenv_file(
    path: str = ".env",
    override: bool = False,
    encoding: str = "utf-8",
) -> Dict[str, str]:
    """
    Load environment variables from a .env file.
    
    Args:
        path: Path to .env file
        override: Whether to override existing environment variables
        encoding: File encoding
    
    Returns:
        Dict of loaded variables
    """
    import os
    from pathlib import Path
    
    env_path = Path(path)
    if not env_path.exists():
        return {}
    
    loaded: Dict[str, str] = {}
    
    with open(env_path, "r", encoding=encoding) as f:
        for line in f:
            line = line.strip()
            
            # Skip comments and empty lines
            if not line or line.startswith("#"):
                continue
            
            # Parse key=value
            if "=" not in line:
                continue
            
            key, _, value = line.partition("=")
            key = key.strip()
            value = value.strip()
            
            # Remove quotes
            if (value.startswith('"') and value.endswith('"')) or \
               (value.startswith("'") and value.endswith("'")):
                value = value[1:-1]
            
            # Expand variables
            value = os.path.expandvars(value)
            
            loaded[key] = value
            
            # Set in environment
            if override or key not in os.environ:
                os.environ[key] = value
    
    return loaded


class EnvironmentConfig:
    """
    Configuration class that loads from environment.
    
    Example:
        class AppConfig(EnvironmentConfig):
            class Meta:
                prefix = "APP_"
            
            database_url: str
            debug: bool = False
            port: int = 8000
        
        config = AppConfig()
        print(config.database_url)
    """

    class Meta:
        prefix: str = ""
        env: Optional[Dict[str, str]] = None
    
    def __init__(self, **overrides: Any):
        # Get annotations
        annotations = {}
        for cls in type(self).__mro__:
            if hasattr(cls, "__annotations__"):
                annotations.update(cls.__annotations__)
        
        # Get prefix
        prefix = getattr(getattr(type(self), "Meta", None), "prefix", "")
        env = getattr(getattr(type(self), "Meta", None), "env", None) or dict(os.environ)
        
        # Load values
        for name, type_hint in annotations.items():
            if name.startswith("_") or name == "Meta":
                continue
            
            # Check for override
            if name in overrides:
                setattr(self, name, overrides[name])
                continue
            
            # Get from environment
            env_name = prefix + name.upper()
            raw_value = env.get(env_name)
            
            # Get default
            default = getattr(type(self), name, None)
            
            if raw_value is None:
                if default is not None:
                    setattr(self, name, default)
                else:
                    raise EnvironmentValidationError(
                        var_name=env_name,
                        message=f"Required environment variable '{env_name}' is not set",
                        required=True,
                    )
            else:
                # Convert type
                converter = EnvValidator.TYPE_CONVERTERS.get(type_hint, type_hint)
                try:
                    converted = converter(raw_value)
                    setattr(self, name, converted)
                except (ValueError, TypeError) as e:
                    raise EnvironmentValidationError(
                        var_name=env_name,
                        message=f"Invalid value for '{env_name}': {e}",
                        expected_type=type_hint.__name__,
                    )
