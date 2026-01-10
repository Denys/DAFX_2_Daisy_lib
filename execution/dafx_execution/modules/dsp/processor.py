"""DSP Processor for audio file processing.

Provides a high-level interface for loading audio, applying effect chains,
and saving results. Used for validation against C++ implementations.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterator

import numpy as np
from numpy.typing import NDArray

from dafx_execution.core import get_logger, ProcessingError
from dafx_execution.modules.dsp.effects import Effect


@dataclass
class AudioData:
    """Container for audio data.
    
    Attributes:
        samples: Audio samples as numpy array (channels x samples).
        sample_rate: Sample rate in Hz.
        channels: Number of audio channels.
        duration: Duration in seconds.
    """
    samples: NDArray[np.float32]
    sample_rate: int
    channels: int = 1
    
    @property
    def duration(self) -> float:
        """Duration in seconds."""
        return len(self.samples) / self.sample_rate if self.sample_rate > 0 else 0.0
    
    @property
    def num_samples(self) -> int:
        """Total number of samples."""
        return len(self.samples)
    
    @classmethod
    def from_file(cls, path: Path) -> "AudioData":
        """Load audio from file.
        
        Args:
            path: Path to audio file (WAV, FLAC, etc.).
        
        Returns:
            AudioData instance.
        
        Raises:
            ProcessingError: If file cannot be loaded.
        """
        try:
            import soundfile as sf
            samples, sample_rate = sf.read(path, dtype="float32")
            
            # Ensure 2D array (samples x channels)
            if samples.ndim == 1:
                samples = samples.reshape(-1, 1)
            
            channels = samples.shape[1]
            
            return cls(
                samples=samples,
                sample_rate=sample_rate,
                channels=channels,
            )
        except ImportError:
            raise ProcessingError(
                "soundfile library required for audio I/O",
                stage="audio_load",
            )
        except Exception as e:
            raise ProcessingError(
                f"Failed to load audio file: {e}",
                stage="audio_load",
                cause=e,
            )
    
    def to_file(self, path: Path) -> None:
        """Save audio to file.
        
        Args:
            path: Output path.
        
        Raises:
            ProcessingError: If file cannot be saved.
        """
        try:
            import soundfile as sf
            sf.write(path, self.samples, self.sample_rate)
        except ImportError:
            raise ProcessingError(
                "soundfile library required for audio I/O",
                stage="audio_save",
            )
        except Exception as e:
            raise ProcessingError(
                f"Failed to save audio file: {e}",
                stage="audio_save",
                cause=e,
            )
    
    def iter_blocks(self, block_size: int) -> Iterator[NDArray[np.float32]]:
        """Iterate over audio in blocks.
        
        Args:
            block_size: Number of samples per block.
        
        Yields:
            Blocks of samples.
        """
        for i in range(0, len(self.samples), block_size):
            yield self.samples[i:i + block_size]


class EffectChain:
    """Chain of effects applied in sequence.
    
    Effects are processed in order, with the output of each
    feeding into the next.
    
    Example:
        >>> chain = EffectChain()
        >>> chain.add(TubeEffect(drive=0.5))
        >>> chain.add(VibratoEffect(rate=5.0))
        >>> chain.init(48000)
        >>> output = chain.process(input_samples)
    """
    
    def __init__(self) -> None:
        """Initialize empty effect chain."""
        self._effects: list[Effect] = []
        self._sample_rate: int = 48000
        self._initialized: bool = False
        self._logger = get_logger(__name__)
    
    def add(self, effect: Effect) -> "EffectChain":
        """Add an effect to the chain.
        
        Args:
            effect: Effect to add.
        
        Returns:
            Self for chaining.
        """
        self._effects.append(effect)
        if self._initialized:
            effect.init(self._sample_rate)
        return self
    
    def init(self, sample_rate: int) -> None:
        """Initialize all effects in the chain.
        
        Args:
            sample_rate: Audio sample rate in Hz.
        """
        self._sample_rate = sample_rate
        for effect in self._effects:
            effect.init(sample_rate)
        self._initialized = True
        self._logger.debug(
            "Effect chain initialized",
            sample_rate=sample_rate,
            effects=[e.name for e in self._effects],
        )
    
    def process(self, samples: NDArray[np.float32]) -> NDArray[np.float32]:
        """Process samples through the effect chain.
        
        Args:
            samples: Input samples.
        
        Returns:
            Processed samples.
        """
        if not self._initialized:
            raise RuntimeError("Effect chain not initialized")
        
        output = samples.copy()
        for effect in self._effects:
            output = effect.process(output)
        return output
    
    def reset(self) -> None:
        """Reset all effects in the chain."""
        for effect in self._effects:
            effect.reset()
    
    def __len__(self) -> int:
        """Number of effects in chain."""
        return len(self._effects)
    
    def __iter__(self) -> Iterator[Effect]:
        """Iterate over effects."""
        return iter(self._effects)


class DSPProcessor:
    """High-level DSP processor for audio files.
    
    Provides a convenient interface for loading audio,
    applying effects, and saving results.
    
    Example:
        >>> processor = DSPProcessor(sample_rate=48000)
        >>> processor.add_effect(TubeEffect(drive=0.5))
        >>> processor.process_file(Path("input.wav"), Path("output.wav"))
    """
    
    def __init__(
        self,
        sample_rate: int = 48000,
        block_size: int = 256,
    ) -> None:
        """Initialize processor.
        
        Args:
            sample_rate: Processing sample rate in Hz.
            block_size: Block size for chunked processing.
        """
        self._sample_rate = sample_rate
        self._block_size = block_size
        self._chain = EffectChain()
        self._logger = get_logger(__name__)
    
    @property
    def sample_rate(self) -> int:
        """Processing sample rate."""
        return self._sample_rate
    
    @property
    def block_size(self) -> int:
        """Processing block size."""
        return self._block_size
    
    def add_effect(self, effect: Effect) -> "DSPProcessor":
        """Add an effect to the processing chain.
        
        Args:
            effect: Effect to add.
        
        Returns:
            Self for chaining.
        """
        self._chain.add(effect)
        return self
    
    def process_file(
        self,
        input_path: Path,
        output_path: Path,
    ) -> AudioData:
        """Process an audio file.
        
        Args:
            input_path: Input audio file path.
            output_path: Output audio file path.
        
        Returns:
            Processed AudioData.
        
        Raises:
            ProcessingError: On processing failure.
        """
        self._logger.info(
            "Processing audio file",
            input=str(input_path),
            output=str(output_path),
        )
        
        # Load audio
        audio = AudioData.from_file(input_path)
        
        # Initialize chain
        self._chain.init(audio.sample_rate)
        
        # Process each channel
        processed = np.zeros_like(audio.samples)
        for ch in range(audio.channels):
            channel_data = audio.samples[:, ch]
            processed[:, ch] = self._chain.process(channel_data)
        
        # Create output
        result = AudioData(
            samples=processed,
            sample_rate=audio.sample_rate,
            channels=audio.channels,
        )
        
        # Save
        result.to_file(output_path)
        
        self._logger.info(
            "Processing complete",
            duration=audio.duration,
            channels=audio.channels,
        )
        
        return result
    
    def process_samples(
        self,
        samples: NDArray[np.float32],
        sample_rate: int | None = None,
    ) -> NDArray[np.float32]:
        """Process raw samples.
        
        Args:
            samples: Input samples.
            sample_rate: Sample rate (uses default if not specified).
        
        Returns:
            Processed samples.
        """
        rate = sample_rate or self._sample_rate
        self._chain.init(rate)
        return self._chain.process(samples)
    
    def reset(self) -> None:
        """Reset processor state."""
        self._chain.reset()
