"""Dataset utilities for batch audio processing.

Provides classes for managing collections of audio files
for batch processing and validation.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Any, Iterator

from dafx_execution.core import get_logger
from dafx_execution.data.audio_io import AudioFile


logger = get_logger(__name__)


@dataclass
class DatasetItem:
    """Single item in a dataset.
    
    Attributes:
        path: Path to the audio file.
        metadata: Additional metadata.
        tags: Tags for filtering.
    """
    path: Path
    metadata: dict[str, Any] = field(default_factory=dict)
    tags: list[str] = field(default_factory=list)
    
    @property
    def name(self) -> str:
        """File name without extension."""
        return self.path.stem
    
    def load(self) -> AudioFile:
        """Load the audio file."""
        return AudioFile(self.path).load()


@dataclass  
class Dataset:
    """Collection of audio files for batch processing.
    
    Provides iteration and filtering for audio datasets.
    
    Example:
        >>> dataset = Dataset.from_directory(Path("audio_files/"))
        >>> for item in dataset.filter(tags=["test"]):
        ...     audio = item.load()
        ...     # Process audio...
    """
    name: str
    items: list[DatasetItem] = field(default_factory=list)
    
    def __len__(self) -> int:
        return len(self.items)
    
    def __iter__(self) -> Iterator[DatasetItem]:
        return iter(self.items)
    
    def __getitem__(self, index: int) -> DatasetItem:
        return self.items[index]
    
    def add(
        self,
        path: Path,
        metadata: dict[str, Any] | None = None,
        tags: list[str] | None = None,
    ) -> "Dataset":
        """Add an item to the dataset.
        
        Args:
            path: Path to audio file.
            metadata: Optional metadata.
            tags: Optional tags.
        
        Returns:
            Self for chaining.
        """
        self.items.append(DatasetItem(
            path=path,
            metadata=metadata or {},
            tags=tags or [],
        ))
        return self
    
    def filter(
        self,
        tags: list[str] | None = None,
        extension: str | None = None,
    ) -> Iterator[DatasetItem]:
        """Filter dataset items.
        
        Args:
            tags: Required tags (item must have all).
            extension: Required file extension.
        
        Yields:
            Matching items.
        """
        for item in self.items:
            # Tag filter
            if tags and not all(t in item.tags for t in tags):
                continue
            
            # Extension filter
            if extension:
                ext = extension.lower().lstrip(".")
                if item.path.suffix.lower().lstrip(".") != ext:
                    continue
            
            yield item
    
    @classmethod
    def from_directory(
        cls,
        directory: Path,
        name: str | None = None,
        extensions: list[str] | None = None,
        recursive: bool = True,
    ) -> "Dataset":
        """Create dataset from directory.
        
        Args:
            directory: Directory to scan.
            name: Dataset name (uses directory name if not specified).
            extensions: File extensions to include.
            recursive: Whether to search subdirectories.
        
        Returns:
            New Dataset instance.
        """
        if not directory.exists():
            raise FileNotFoundError(f"Directory not found: {directory}")
        
        # Default extensions
        if extensions is None:
            extensions = ["wav", "flac", "ogg"]
        
        # Normalize extensions
        extensions = [e.lower().lstrip(".") for e in extensions]
        
        # Build pattern
        pattern = "**/*" if recursive else "*"
        
        dataset = cls(name=name or directory.name)
        
        for ext in extensions:
            for path in directory.glob(f"{pattern}.{ext}"):
                dataset.add(path)
        
        logger.info(
            "Created dataset from directory",
            name=dataset.name,
            items=len(dataset),
            directory=str(directory),
        )
        
        return dataset
    
    def split(
        self,
        train_ratio: float = 0.8,
        seed: int = 42,
    ) -> tuple["Dataset", "Dataset"]:
        """Split dataset into train and test sets.
        
        Args:
            train_ratio: Fraction for training set.
            seed: Random seed for reproducibility.
        
        Returns:
            Tuple of (train_dataset, test_dataset).
        """
        import random
        
        rng = random.Random(seed)
        items = self.items.copy()
        rng.shuffle(items)
        
        split_idx = int(len(items) * train_ratio)
        
        train = Dataset(name=f"{self.name}_train", items=items[:split_idx])
        test = Dataset(name=f"{self.name}_test", items=items[split_idx:])
        
        return train, test
