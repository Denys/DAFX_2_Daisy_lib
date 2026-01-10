"""Audio file I/O utilities.

Provides a unified interface for reading and writing audio files
in various formats (WAV, FLAC, etc.).
"""

from __future__ import annotations

from dataclasses import dataclass
from enum import Enum, auto
from pathlib import Path
from typing import Iterator

import numpy as np
from numpy.typing import NDArray

from dafx_execution.core import get_logger, ResourceError


logger = get_logger(__name__)


class AudioFormat(Enum):
    """Supported audio file formats."""
    WAV = auto()
    FLAC = auto()
    OGG = auto()
    MP3 = auto()  # Read-only
    
    @classmethod
    def from_extension(cls, ext: str) -> "AudioFormat":
        """Get format from file extension."""
        ext = ext.lower().lstrip(".")
        mapping = {
            "wav": cls.WAV,
            "wave": cls.WAV,
            "flac": cls.FLAC,
            "ogg": cls.OGG,
            "mp3": cls.MP3,
        }
        if ext not in mapping:
            raise ValueError(f"Unsupported audio format: {ext}")
        return mapping[ext]


@dataclass
class AudioMetadata:
    """Audio file metadata.
    
    Attributes:
        sample_rate: Sample rate in Hz.
        channels: Number of channels.
        duration: Duration in seconds.
        bit_depth: Bits per sample.
        format: Audio format.
    """
    sample_rate: int
    channels: int
    duration: float
    bit_depth: int = 16
    format: AudioFormat = AudioFormat.WAV


class AudioFile:
    """Audio file handler for reading and writing audio data.
    
    Provides a unified interface for various audio formats using
    the soundfile library.
    
    Example:
        >>> audio = AudioFile(Path("input.wav"))
        >>> audio.load()
        >>> print(f"Duration: {audio.duration:.2f}s")
        >>> audio.save(Path("output.wav"))
    """
    
    def __init__(self, path: Path | None = None) -> None:
        """Initialize audio file handler.
        
        Args:
            path: Path to audio file (optional for new files).
        """
        self._path = path
        self._samples: NDArray[np.float32] | None = None
        self._sample_rate: int = 48000
        self._channels: int = 1
    
    @property
    def path(self) -> Path | None:
        """File path."""
        return self._path
    
    @property
    def samples(self) -> NDArray[np.float32] | None:
        """Audio samples (samples x channels)."""
        return self._samples
    
    @samples.setter
    def samples(self, value: NDArray[np.float32]) -> None:
        """Set audio samples."""
        self._samples = value
        if value.ndim == 1:
            self._channels = 1
        else:
            self._channels = value.shape[1]
    
    @property
    def sample_rate(self) -> int:
        """Sample rate in Hz."""
        return self._sample_rate
    
    @sample_rate.setter
    def sample_rate(self, value: int) -> None:
        """Set sample rate."""
        self._sample_rate = value
    
    @property
    def channels(self) -> int:
        """Number of channels."""
        return self._channels
    
    @property
    def duration(self) -> float:
        """Duration in seconds."""
        if self._samples is None:
            return 0.0
        return len(self._samples) / self._sample_rate
    
    @property
    def num_samples(self) -> int:
        """Total number of samples."""
        return len(self._samples) if self._samples is not None else 0
    
    def load(self, path: Path | None = None) -> "AudioFile":
        """Load audio from file.
        
        Args:
            path: Path to load from (uses stored path if not provided).
        
        Returns:
            Self for chaining.
        
        Raises:
            ResourceError: If file cannot be loaded.
        """
        load_path = path or self._path
        if load_path is None:
            raise ResourceError("No path specified", resource_type="file")
        
        try:
            import soundfile as sf
            
            self._samples, self._sample_rate = sf.read(load_path, dtype="float32")
            
            # Ensure 2D
            if self._samples.ndim == 1:
                self._samples = self._samples.reshape(-1, 1)
            
            self._channels = self._samples.shape[1]
            self._path = load_path
            
            logger.debug(
                "Loaded audio",
                path=str(load_path),
                sample_rate=self._sample_rate,
                channels=self._channels,
                duration=self.duration,
            )
            
            return self
            
        except ImportError:
            raise ResourceError(
                "soundfile library required for audio I/O",
                resource_type="library",
            )
        except Exception as e:
            raise ResourceError(
                f"Failed to load audio: {e}",
                resource_type="file",
                resource_path=load_path,
                cause=e,
            )
    
    def save(
        self,
        path: Path,
        format: AudioFormat | None = None,
        bit_depth: int = 16,
    ) -> None:
        """Save audio to file.
        
        Args:
            path: Output path.
            format: Audio format (auto-detected from extension if not specified).
            bit_depth: Bits per sample (16, 24, or 32).
        
        Raises:
            ResourceError: If file cannot be saved.
        """
        if self._samples is None:
            raise ResourceError("No audio data to save", resource_type="data")
        
        # Determine format
        if format is None:
            format = AudioFormat.from_extension(path.suffix)
        
        # Map bit depth to subtype
        subtype_map = {
            16: "PCM_16",
            24: "PCM_24",
            32: "FLOAT",
        }
        subtype = subtype_map.get(bit_depth, "PCM_16")
        
        try:
            import soundfile as sf
            
            path.parent.mkdir(parents=True, exist_ok=True)
            
            # Reshape to 1D if mono
            data = self._samples
            if self._channels == 1 and data.ndim == 2:
                data = data.flatten()
            
            sf.write(path, data, self._sample_rate, subtype=subtype)
            
            logger.debug(
                "Saved audio",
                path=str(path),
                format=format.name,
                bit_depth=bit_depth,
            )
            
        except ImportError:
            raise ResourceError(
                "soundfile library required for audio I/O",
                resource_type="library",
            )
        except Exception as e:
            raise ResourceError(
                f"Failed to save audio: {e}",
                resource_type="file",
                resource_path=path,
                cause=e,
            )
    
    def iter_blocks(self, block_size: int) -> Iterator[NDArray[np.float32]]:
        """Iterate over audio in blocks.
        
        Args:
            block_size: Samples per block.
        
        Yields:
            Blocks of audio samples.
        """
        if self._samples is None:
            return
        
        for i in range(0, len(self._samples), block_size):
            yield self._samples[i:i + block_size]
    
    def get_channel(self, channel: int) -> NDArray[np.float32]:
        """Get a single channel.
        
        Args:
            channel: Channel index (0-based).
        
        Returns:
            Channel samples as 1D array.
        """
        if self._samples is None:
            raise ResourceError("No audio data loaded", resource_type="data")
        
        if channel >= self._channels:
            raise ValueError(f"Channel {channel} out of range (0-{self._channels-1})")
        
        return self._samples[:, channel]
    
    @classmethod
    def from_samples(
        cls,
        samples: NDArray[np.float32],
        sample_rate: int,
    ) -> "AudioFile":
        """Create AudioFile from samples.
        
        Args:
            samples: Audio samples.
            sample_rate: Sample rate in Hz.
        
        Returns:
            New AudioFile instance.
        """
        audio = cls()
        audio.samples = samples
        audio.sample_rate = sample_rate
        return audio
    
    @classmethod
    def generate_sine(
        cls,
        frequency: float,
        duration: float,
        sample_rate: int = 48000,
        amplitude: float = 0.5,
    ) -> "AudioFile":
        """Generate a sine wave test signal.
        
        Args:
            frequency: Frequency in Hz.
            duration: Duration in seconds.
            sample_rate: Sample rate in Hz.
            amplitude: Signal amplitude (0.0 to 1.0).
        
        Returns:
            AudioFile with sine wave.
        """
        t = np.arange(int(sample_rate * duration)) / sample_rate
        samples = (amplitude * np.sin(2 * np.pi * frequency * t)).astype(np.float32)
        return cls.from_samples(samples.reshape(-1, 1), sample_rate)
