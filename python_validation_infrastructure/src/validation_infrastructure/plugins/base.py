"""Base classes for plugin development."""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Callable, Generic, Optional, TypeVar

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationResult,
)
from validation_infrastructure.sanitization.sanitizers import Sanitizer

T = TypeVar("T")


class PluginBase(ABC):
    """Base class for all plugins."""

    @property
    @abstractmethod
    def name(self) -> str:
        """Plugin name."""
        ...
    
    @property
    def version(self) -> str:
        """Plugin version."""
        return "1.0.0"
    
    @property
    def description(self) -> str:
        """Plugin description."""
        return ""
    
    def on_load(self) -> None:
        """Called when plugin is loaded."""
        pass
    
    def on_unload(self) -> None:
        """Called when plugin is unloaded."""
        pass


class CustomValidatorPlugin(PluginBase, BaseValidator[T], Generic[T]):
    """Base class for custom validator plugins."""

    def __init__(self, **config: Any):
        BaseValidator.__init__(self, name=self.name)
        self.config = config
    
    @abstractmethod
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Implement validation logic."""
        ...


class SanitizerPlugin(PluginBase, Sanitizer[T], Generic[T]):
    """Base class for sanitizer plugins."""

    def __init__(self, **config: Any):
        self.config = config
    
    @abstractmethod
    def sanitize(self, value: Any) -> T:
        """Implement sanitization logic."""
        ...


class TransformerPlugin(PluginBase, Generic[T]):
    """Base class for transformer plugins."""

    def __init__(self, **config: Any):
        self.config = config
    
    @abstractmethod
    def transform(self, value: Any) -> T:
        """Transform the input value."""
        ...
    
    def __call__(self, value: Any) -> T:
        """Make transformer callable."""
        return self.transform(value)
