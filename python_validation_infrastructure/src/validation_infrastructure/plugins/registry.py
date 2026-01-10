"""Plugin registry and discovery system."""

from __future__ import annotations

import importlib
import importlib.util
import sys
import threading
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Set, Type, TypeVar

from validation_infrastructure.core.base import BaseValidator

T = TypeVar("T")


@dataclass
class ValidatorPlugin:
    """Metadata for a registered validator plugin."""

    name: str
    version: str
    validator_class: Type[BaseValidator[Any]]
    description: str = ""
    author: str = ""
    tags: Set[str] = field(default_factory=set)
    dependencies: List[str] = field(default_factory=list)
    config_schema: Optional[Dict[str, Any]] = None
    
    def create(self, **kwargs: Any) -> BaseValidator[Any]:
        """Create an instance of the validator."""
        return self.validator_class(**kwargs)


class PluginRegistry:
    """
    Central registry for validation plugins.
    
    Thread-safe singleton pattern for global plugin management.
    """

    _instance: Optional[PluginRegistry] = None
    _lock = threading.Lock()
    
    def __new__(cls) -> PluginRegistry:
        if cls._instance is None:
            with cls._lock:
                if cls._instance is None:
                    cls._instance = super().__new__(cls)
                    cls._instance._initialized = False
        return cls._instance
    
    def __init__(self):
        if self._initialized:
            return
        
        self._validators: Dict[str, ValidatorPlugin] = {}
        self._sanitizers: Dict[str, Any] = {}
        self._transformers: Dict[str, Any] = {}
        self._hooks: Dict[str, List[Callable[..., Any]]] = {
            "pre_validate": [],
            "post_validate": [],
            "on_error": [],
            "on_success": [],
        }
        self._loaded_modules: Set[str] = set()
        self._initialized = True
    
    def register_validator(
        self,
        name: str,
        validator_class: Type[BaseValidator[Any]],
        version: str = "1.0.0",
        description: str = "",
        author: str = "",
        tags: Optional[Set[str]] = None,
        dependencies: Optional[List[str]] = None,
        config_schema: Optional[Dict[str, Any]] = None,
        overwrite: bool = False,
    ) -> ValidatorPlugin:
        """
        Register a validator plugin.
        
        Args:
            name: Unique plugin name
            validator_class: The validator class
            version: Plugin version
            description: Plugin description
            author: Plugin author
            tags: Optional tags for categorization
            dependencies: Required dependencies
            config_schema: Schema for plugin configuration
            overwrite: Whether to overwrite existing plugin
        
        Returns:
            The registered plugin metadata
        """
        if name in self._validators and not overwrite:
            raise ValueError(f"Validator plugin '{name}' already registered")
        
        plugin = ValidatorPlugin(
            name=name,
            version=version,
            validator_class=validator_class,
            description=description,
            author=author,
            tags=tags or set(),
            dependencies=dependencies or [],
            config_schema=config_schema,
        )
        
        self._validators[name] = plugin
        return plugin
    
    def unregister_validator(self, name: str) -> bool:
        """Unregister a validator plugin."""
        if name in self._validators:
            del self._validators[name]
            return True
        return False
    
    def get_validator(self, name: str) -> Optional[ValidatorPlugin]:
        """Get a validator plugin by name."""
        return self._validators.get(name)
    
    def create_validator(self, name: str, **kwargs: Any) -> BaseValidator[Any]:
        """Create a validator instance from registered plugin."""
        plugin = self._validators.get(name)
        if plugin is None:
            raise ValueError(f"Unknown validator plugin: {name}")
        return plugin.create(**kwargs)
    
    def list_validators(
        self,
        tags: Optional[Set[str]] = None,
    ) -> List[ValidatorPlugin]:
        """List registered validators, optionally filtered by tags."""
        validators = list(self._validators.values())
        
        if tags:
            validators = [v for v in validators if v.tags & tags]
        
        return validators
    
    def register_sanitizer(
        self,
        name: str,
        sanitizer: Any,
        overwrite: bool = False,
    ) -> None:
        """Register a sanitizer."""
        if name in self._sanitizers and not overwrite:
            raise ValueError(f"Sanitizer '{name}' already registered")
        self._sanitizers[name] = sanitizer
    
    def get_sanitizer(self, name: str) -> Optional[Any]:
        """Get a sanitizer by name."""
        return self._sanitizers.get(name)
    
    def register_transformer(
        self,
        name: str,
        transformer: Callable[[Any], Any],
        overwrite: bool = False,
    ) -> None:
        """Register a transformer function."""
        if name in self._transformers and not overwrite:
            raise ValueError(f"Transformer '{name}' already registered")
        self._transformers[name] = transformer
    
    def get_transformer(self, name: str) -> Optional[Callable[[Any], Any]]:
        """Get a transformer by name."""
        return self._transformers.get(name)
    
    def add_hook(self, event: str, callback: Callable[..., Any]) -> None:
        """Add a hook callback for an event."""
        if event not in self._hooks:
            self._hooks[event] = []
        self._hooks[event].append(callback)
    
    def remove_hook(self, event: str, callback: Callable[..., Any]) -> bool:
        """Remove a hook callback."""
        if event in self._hooks and callback in self._hooks[event]:
            self._hooks[event].remove(callback)
            return True
        return False
    
    def call_hooks(self, event: str, *args: Any, **kwargs: Any) -> List[Any]:
        """Call all hooks for an event."""
        results = []
        for callback in self._hooks.get(event, []):
            try:
                result = callback(*args, **kwargs)
                results.append(result)
            except Exception:
                pass  # Hooks should not break validation
        return results
    
    def load_plugin_module(self, module_path: str) -> None:
        """Load a plugin from a module path."""
        if module_path in self._loaded_modules:
            return
        
        try:
            module = importlib.import_module(module_path)
            
            # Call register function if it exists
            if hasattr(module, "register"):
                module.register(self)
            
            # Auto-discover validators
            for name in dir(module):
                obj = getattr(module, name)
                if (
                    isinstance(obj, type)
                    and issubclass(obj, BaseValidator)
                    and obj is not BaseValidator
                    and hasattr(obj, "__plugin_name__")
                ):
                    self.register_validator(
                        name=getattr(obj, "__plugin_name__", name),
                        validator_class=obj,
                        version=getattr(obj, "__plugin_version__", "1.0.0"),
                        description=getattr(obj, "__plugin_description__", ""),
                    )
            
            self._loaded_modules.add(module_path)
        except ImportError as e:
            raise ImportError(f"Failed to load plugin module '{module_path}': {e}")
    
    def load_plugin_file(self, file_path: str) -> None:
        """Load a plugin from a Python file."""
        path = Path(file_path)
        if not path.exists():
            raise FileNotFoundError(f"Plugin file not found: {file_path}")
        
        module_name = path.stem
        spec = importlib.util.spec_from_file_location(module_name, path)
        if spec is None or spec.loader is None:
            raise ImportError(f"Cannot load plugin from: {file_path}")
        
        module = importlib.util.module_from_spec(spec)
        sys.modules[module_name] = module
        spec.loader.exec_module(module)
        
        # Register plugins from module
        if hasattr(module, "register"):
            module.register(self)
    
    def discover_plugins(self, directory: str, pattern: str = "*.py") -> int:
        """Discover and load plugins from a directory."""
        dir_path = Path(directory)
        if not dir_path.is_dir():
            raise NotADirectoryError(f"Plugin directory not found: {directory}")
        
        count = 0
        for file in dir_path.glob(pattern):
            if file.name.startswith("_"):
                continue
            try:
                self.load_plugin_file(str(file))
                count += 1
            except Exception:
                pass  # Skip failed plugins
        
        return count
    
    def clear(self) -> None:
        """Clear all registered plugins."""
        self._validators.clear()
        self._sanitizers.clear()
        self._transformers.clear()
        for event in self._hooks:
            self._hooks[event].clear()
        self._loaded_modules.clear()


def get_registry() -> PluginRegistry:
    """Get the global plugin registry instance."""
    return PluginRegistry()


def register_plugin(
    name: str,
    version: str = "1.0.0",
    description: str = "",
    author: str = "",
    tags: Optional[Set[str]] = None,
) -> Callable[[Type[BaseValidator[T]]], Type[BaseValidator[T]]]:
    """
    Decorator to register a validator class as a plugin.
    
    Example:
        @register_plugin(
            name="custom_email",
            version="1.0.0",
            description="Custom email validator with domain checking",
            tags={"email", "string"},
        )
        class CustomEmailValidator(BaseValidator[str]):
            ...
    """
    def decorator(cls: Type[BaseValidator[T]]) -> Type[BaseValidator[T]]:
        # Add plugin metadata to class
        cls.__plugin_name__ = name  # type: ignore
        cls.__plugin_version__ = version  # type: ignore
        cls.__plugin_description__ = description  # type: ignore
        
        # Register with global registry
        get_registry().register_validator(
            name=name,
            validator_class=cls,
            version=version,
            description=description,
            author=author,
            tags=tags,
        )
        
        return cls
    
    return decorator


def load_plugins(
    modules: Optional[List[str]] = None,
    directories: Optional[List[str]] = None,
    entry_points_group: str = "validation_infrastructure.plugins",
) -> int:
    """
    Load plugins from multiple sources.
    
    Args:
        modules: List of module paths to load
        directories: List of directories to scan
        entry_points_group: Entry points group name
    
    Returns:
        Number of plugins loaded
    """
    registry = get_registry()
    count = 0
    
    # Load from modules
    if modules:
        for module in modules:
            try:
                registry.load_plugin_module(module)
                count += 1
            except Exception:
                pass
    
    # Load from directories
    if directories:
        for directory in directories:
            try:
                count += registry.discover_plugins(directory)
            except Exception:
                pass
    
    # Load from entry points
    try:
        if sys.version_info >= (3, 10):
            from importlib.metadata import entry_points
            eps = entry_points(group=entry_points_group)
        else:
            from importlib.metadata import entry_points
            eps = entry_points().get(entry_points_group, [])
        
        for ep in eps:
            try:
                plugin = ep.load()
                if callable(plugin):
                    plugin(registry)
                count += 1
            except Exception:
                pass
    except Exception:
        pass
    
    return count
