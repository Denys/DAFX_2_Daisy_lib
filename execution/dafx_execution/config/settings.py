"""Configuration management for DAFX execution scripts.

Implements a hierarchical configuration system with precedence:
1. CLI arguments (highest)
2. Environment variables (DAFX_*)
3. Configuration file (config.yaml)
4. Defaults (lowest)

Uses Pydantic Settings for validation and type coercion.
"""

from __future__ import annotations

import os
from functools import lru_cache
from pathlib import Path
from typing import Any, Literal

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


class LoggingSettings(BaseSettings):
    """Logging configuration."""
    
    model_config = SettingsConfigDict(
        env_prefix="DAFX_LOG_",
        extra="ignore",
    )
    
    level: Literal["DEBUG", "INFO", "WARNING", "ERROR", "CRITICAL"] = "INFO"
    json_output: bool = True
    console_output: bool = True
    log_dir: Path = Path(".tmp/logs")


class DSPSettings(BaseSettings):
    """DSP processing configuration."""
    
    model_config = SettingsConfigDict(
        env_prefix="DAFX_DSP_",
        extra="ignore",
    )
    
    sample_rate: int = Field(default=48000, ge=8000, le=192000)
    block_size: int = Field(default=256, ge=16, le=4096)
    channels: int = Field(default=2, ge=1, le=8)
    bit_depth: Literal[16, 24, 32] = 24
    
    @field_validator("block_size")
    @classmethod
    def block_size_power_of_two(cls, v: int) -> int:
        """Ensure block size is power of 2."""
        if v & (v - 1) != 0:
            # Round up to next power of 2
            v = 1 << (v - 1).bit_length()
        return v


class PathSettings(BaseSettings):
    """Path configuration."""
    
    model_config = SettingsConfigDict(
        env_prefix="DAFX_PATH_",
        extra="ignore",
    )
    
    input_dir: Path = Path("data/input")
    output_dir: Path = Path("data/output")
    temp_dir: Path = Path(".tmp")
    cache_dir: Path = Path(".cache")
    
    @field_validator("input_dir", "output_dir", "temp_dir", "cache_dir", mode="after")
    @classmethod
    def create_directories(cls, v: Path) -> Path:
        """Create directories if they don't exist."""
        v.mkdir(parents=True, exist_ok=True)
        return v


class ExecutionSettings(BaseSettings):
    """Execution engine configuration."""
    
    model_config = SettingsConfigDict(
        env_prefix="DAFX_EXEC_",
        extra="ignore",
    )
    
    dry_run: bool = False
    collect_metrics: bool = True
    max_workers: int = Field(default=4, ge=1, le=32)
    timeout_seconds: float = Field(default=300.0, ge=1.0)
    retry_count: int = Field(default=3, ge=0, le=10)


class AppSettings(BaseSettings):
    """Main application settings.
    
    Combines all setting groups into a single configuration object.
    Environment variables override defaults.
    """
    
    model_config = SettingsConfigDict(
        env_prefix="DAFX_",
        env_file=".env",
        env_file_encoding="utf-8",
        env_nested_delimiter="__",
        extra="ignore",
    )
    
    # Application metadata
    app_name: str = "dafx-execution"
    version: str = "0.1.0"
    debug: bool = False
    
    # Nested settings
    logging: LoggingSettings = Field(default_factory=LoggingSettings)
    dsp: DSPSettings = Field(default_factory=DSPSettings)
    paths: PathSettings = Field(default_factory=PathSettings)
    execution: ExecutionSettings = Field(default_factory=ExecutionSettings)
    
    def get_sample_rate(self) -> int:
        """Shortcut to get sample rate."""
        return self.dsp.sample_rate
    
    def get_block_size(self) -> int:
        """Shortcut to get block size."""
        return self.dsp.block_size


@lru_cache(maxsize=1)
def get_settings() -> AppSettings:
    """Get cached application settings.
    
    Settings are loaded once and cached for performance.
    
    Returns:
        Configured AppSettings instance.
    
    Example:
        >>> settings = get_settings()
        >>> print(settings.dsp.sample_rate)
        48000
    """
    return AppSettings()


def reload_settings() -> AppSettings:
    """Reload settings, clearing the cache.
    
    Use this when environment or config files change during runtime.
    
    Returns:
        Fresh AppSettings instance.
    """
    get_settings.cache_clear()
    return get_settings()
