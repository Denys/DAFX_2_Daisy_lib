"""DSP Effect definitions.

Provides Python implementations of DSP effects for validation against
the C++ implementations in src/effects/, src/filters/, etc.
"""

from __future__ import annotations

from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Any

import numpy as np
from numpy.typing import NDArray


@dataclass
class EffectParams:
    """Base class for effect parameters.
    
    Subclass this for each effect to define its specific parameters.
    """
    pass


class Effect(ABC):
    """Base class for all DSP effects.
    
    Effects process audio samples and can be chained together.
    Each effect maintains its own state for sample-by-sample processing.
    
    Example:
        >>> effect = TubeEffect(drive=0.5)
        >>> effect.init(48000)
        >>> output = effect.process(input_samples)
    """
    
    def __init__(self, **params: Any) -> None:
        """Initialize effect with parameters.
        
        Args:
            **params: Effect-specific parameters.
        """
        self._sample_rate: int = 48000
        self._initialized: bool = False
        self._params: dict[str, Any] = params
    
    @property
    def name(self) -> str:
        """Effect name for logging."""
        return self.__class__.__name__
    
    @property
    def sample_rate(self) -> int:
        """Current sample rate."""
        return self._sample_rate
    
    @property
    def is_initialized(self) -> bool:
        """Check if effect is initialized."""
        return self._initialized
    
    def init(self, sample_rate: int) -> None:
        """Initialize effect for processing.
        
        Call this before processing to set sample rate and
        initialize internal state.
        
        Args:
            sample_rate: Audio sample rate in Hz.
        """
        self._sample_rate = sample_rate
        self._initialized = True
        self._on_init()
    
    def _on_init(self) -> None:
        """Override to perform custom initialization."""
        pass
    
    @abstractmethod
    def process_sample(self, sample: float) -> float:
        """Process a single sample.
        
        Args:
            sample: Input sample (-1.0 to 1.0).
        
        Returns:
            Processed sample.
        """
        ...
    
    def process(self, samples: NDArray[np.float32]) -> NDArray[np.float32]:
        """Process an array of samples.
        
        Args:
            samples: Input samples as numpy array.
        
        Returns:
            Processed samples as numpy array.
        """
        if not self._initialized:
            raise RuntimeError(f"{self.name} not initialized. Call init() first.")
        
        output = np.zeros_like(samples)
        for i in range(len(samples)):
            output[i] = self.process_sample(float(samples[i]))
        return output
    
    def reset(self) -> None:
        """Reset effect state.
        
        Call this between processing different audio files to
        clear any internal state (delay lines, filters, etc.).
        """
        self._on_init()
    
    def get_params(self) -> dict[str, Any]:
        """Get current parameters."""
        return self._params.copy()
    
    def set_param(self, name: str, value: Any) -> None:
        """Set a parameter value.
        
        Args:
            name: Parameter name.
            value: New value.
        """
        self._params[name] = value


class TubeEffect(Effect):
    """Tube distortion effect.
    
    Simulates vacuum tube saturation with asymmetric soft clipping.
    Based on DAFX book algorithm (Chapter 5).
    
    Parameters:
        drive: Distortion amount (0.0 to 1.0)
        mix: Dry/wet mix (0.0 = dry, 1.0 = wet)
    """
    
    def __init__(
        self,
        drive: float = 0.5,
        mix: float = 1.0,
    ) -> None:
        super().__init__(drive=drive, mix=mix)
        self._drive = drive
        self._mix = mix
    
    def _on_init(self) -> None:
        """Reset tube state."""
        # No internal state for this simple model
        pass
    
    def process_sample(self, sample: float) -> float:
        """Apply tube saturation to a sample."""
        # Scale input by drive
        x = sample * (1.0 + self._drive * 10.0)
        
        # Asymmetric soft clipping (tube-like)
        if x > 0:
            y = 1.0 - np.exp(-x)
        else:
            y = -1.0 + np.exp(x)
        
        # Mix dry/wet
        return float(sample * (1.0 - self._mix) + y * self._mix)


class WahWahEffect(Effect):
    """Wah-wah filter effect.
    
    Resonant bandpass filter with LFO-controlled center frequency.
    Based on CryBaby wah pedal characteristics.
    
    Parameters:
        rate: LFO rate in Hz (0.1 to 10.0)
        depth: Frequency sweep depth (0.0 to 1.0)
        q: Filter resonance (1.0 to 20.0)
    """
    
    def __init__(
        self,
        rate: float = 1.0,
        depth: float = 0.8,
        q: float = 5.0,
    ) -> None:
        super().__init__(rate=rate, depth=depth, q=q)
        self._rate = rate
        self._depth = depth
        self._q = q
        self._phase: float = 0.0
        self._y1: float = 0.0
        self._y2: float = 0.0
    
    def _on_init(self) -> None:
        """Reset filter state."""
        self._phase = 0.0
        self._y1 = 0.0
        self._y2 = 0.0
    
    def process_sample(self, sample: float) -> float:
        """Apply wah effect to a sample."""
        # LFO for frequency modulation
        self._phase += 2.0 * np.pi * self._rate / self._sample_rate
        if self._phase > 2.0 * np.pi:
            self._phase -= 2.0 * np.pi
        
        lfo = (np.sin(self._phase) + 1.0) / 2.0  # 0 to 1
        
        # Calculate center frequency (300 Hz to 2000 Hz range)
        f_min = 300.0
        f_max = 2000.0
        fc = f_min + (f_max - f_min) * lfo * self._depth
        
        # State variable filter coefficients
        f = 2.0 * np.sin(np.pi * fc / self._sample_rate)
        q = self._q
        
        # Bandpass filter
        low = self._y2 + f * self._y1
        high = sample - low - self._y1 / q
        band = f * high + self._y1
        
        self._y1 = band
        self._y2 = low
        
        return float(band)


class VibratoEffect(Effect):
    """Vibrato effect using modulated delay.
    
    Creates pitch modulation by varying a short delay line.
    
    Parameters:
        rate: Modulation rate in Hz (0.1 to 14.0)
        depth: Modulation depth in ms (0.0 to 3.0)
    """
    
    def __init__(
        self,
        rate: float = 5.0,
        depth: float = 1.0,
    ) -> None:
        super().__init__(rate=rate, depth=depth)
        self._rate = rate
        self._depth = depth
        self._phase: float = 0.0
        self._delay_line: NDArray[np.float32] = np.array([], dtype=np.float32)
        self._write_pos: int = 0
    
    def _on_init(self) -> None:
        """Initialize delay line."""
        # Max delay: depth + some margin (in samples)
        max_delay_samples = int(self._sample_rate * 0.01)  # 10ms max
        self._delay_line = np.zeros(max_delay_samples, dtype=np.float32)
        self._write_pos = 0
        self._phase = 0.0
    
    def process_sample(self, sample: float) -> float:
        """Apply vibrato to a sample."""
        # LFO
        self._phase += 2.0 * np.pi * self._rate / self._sample_rate
        if self._phase > 2.0 * np.pi:
            self._phase -= 2.0 * np.pi
        
        lfo = np.sin(self._phase)
        
        # Write to delay line
        self._delay_line[self._write_pos] = sample
        
        # Calculate modulated delay in samples
        delay_ms = self._depth * (lfo + 1.0) / 2.0  # 0 to depth ms
        delay_samples = delay_ms * self._sample_rate / 1000.0
        
        # Read position with linear interpolation
        read_pos = self._write_pos - delay_samples
        if read_pos < 0:
            read_pos += len(self._delay_line)
        
        idx = int(read_pos)
        frac = read_pos - idx
        
        # Wrap indices
        idx1 = idx % len(self._delay_line)
        idx2 = (idx + 1) % len(self._delay_line)
        
        # Linear interpolation
        output = self._delay_line[idx1] * (1.0 - frac) + self._delay_line[idx2] * frac
        
        # Advance write position
        self._write_pos = (self._write_pos + 1) % len(self._delay_line)
        
        return float(output)
