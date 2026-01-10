"""Abstract base classes and protocols for DAFX execution.

Defines interfaces that modules must implement for interoperability.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from typing import Any, Generic, Protocol, TypeVar

from numpy.typing import NDArray
import numpy as np


T = TypeVar("T")
InputT = TypeVar("InputT", contravariant=True)
OutputT = TypeVar("OutputT", covariant=True)


class Processor(Protocol[InputT, OutputT]):
    """Protocol for processors that transform input to output."""
    
    def process(self, data: InputT) -> OutputT:
        """Process input data."""
        ...


class AudioProcessor(ABC):
    """Base class for audio processing components."""
    
    @property
    @abstractmethod
    def sample_rate(self) -> int:
        """Processing sample rate in Hz."""
        ...
    
    @abstractmethod
    def init(self, sample_rate: int) -> None:
        """Initialize processor with sample rate."""
        ...
    
    @abstractmethod
    def process(self, samples: NDArray[np.float32]) -> NDArray[np.float32]:
        """Process audio samples."""
        ...
    
    @abstractmethod
    def reset(self) -> None:
        """Reset processor state."""
        ...


class Configurable(Protocol):
    """Protocol for configurable objects."""
    
    def get_params(self) -> dict[str, Any]:
        """Get current parameters."""
        ...
    
    def set_param(self, name: str, value: Any) -> None:
        """Set a parameter value."""
        ...


class Serializable(Protocol):
    """Protocol for serializable objects."""
    
    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary."""
        ...
    
    @classmethod
    def from_dict(cls, data: dict[str, Any]) -> "Serializable":
        """Create from dictionary."""
        ...


class Validatable(ABC, Generic[T]):
    """Base class for objects that can be validated."""
    
    @abstractmethod
    def validate(self, data: T) -> bool:
        """Validate data, returning True if valid."""
        ...
    
    @abstractmethod
    def get_errors(self) -> list[str]:
        """Get validation error messages."""
        ...
