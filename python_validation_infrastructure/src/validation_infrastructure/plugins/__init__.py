"""Extensible plugin architecture for custom validation rules."""

from validation_infrastructure.plugins.registry import (
    PluginRegistry,
    ValidatorPlugin,
    register_plugin,
    get_registry,
    load_plugins,
)
from validation_infrastructure.plugins.base import (
    PluginBase,
    CustomValidatorPlugin,
    SanitizerPlugin,
    TransformerPlugin,
)

__all__ = [
    "PluginRegistry",
    "ValidatorPlugin",
    "register_plugin",
    "get_registry",
    "load_plugins",
    "PluginBase",
    "CustomValidatorPlugin",
    "SanitizerPlugin",
    "TransformerPlugin",
]
