"""DSP module for audio processing operations."""

from dafx_execution.modules.dsp.processor import DSPProcessor, EffectChain
from dafx_execution.modules.dsp.effects import (
    Effect,
    TubeEffect,
    WahWahEffect,
    VibratoEffect,
)

__all__ = [
    "DSPProcessor",
    "EffectChain",
    "Effect",
    "TubeEffect",
    "WahWahEffect",
    "VibratoEffect",
]
