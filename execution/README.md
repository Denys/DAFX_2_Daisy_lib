# DAFX Execution Scripts

Python execution scripts for the DAFX-to-DaisySP library. Provides DSP algorithm validation, automated testing, and data processing utilities.

## Installation

```bash
cd execution
pip install -e .

# With dev dependencies
pip install -e ".[dev]"
```

## Quick Start

### CLI Commands

```bash
# Check environment
dafx-cli check

# View configuration
dafx-cli config

# Process audio (coming soon)
dafx-cli process input.wav -e tube -o output.wav

# Show version
dafx-cli version
```

### Python API

```python
from dafx_execution import ExecutionEngine, get_logger, configure_logging
from dafx_execution.modules.dsp import DSPProcessor, TubeEffect, VibratoEffect

# Configure logging
configure_logging(level="INFO")

# Process audio with effect chain
processor = DSPProcessor(sample_rate=48000)
processor.add_effect(TubeEffect(drive=0.5))
processor.add_effect(VibratoEffect(rate=5.0))
processor.process_file(Path("input.wav"), Path("output.wav"))
```

### Validation

```python
from dafx_execution.modules.testing import OutputComparator, TestRunner, TestCase

# Compare outputs
comparator = OutputComparator(tolerance=1e-5)
result = comparator.validate(actual, expected)
print(result)  # ✓ PASSED | Max Error: 0.000001 | SNR: 120.5 dB
```

## Structure

```
dafx_execution/
├── core/           # Engine, exceptions, logging
├── config/         # Pydantic-based settings
├── bin/            # CLI entry points
├── modules/
│   ├── dsp/        # DSP effects and processor
│   ├── testing/    # Validation and test runner
│   └── utils/      # File I/O, timing utilities
└── tests/          # Unit tests
```

## Configuration

Settings hierarchy (highest to lowest priority):
1. CLI arguments
2. Environment variables (`DAFX_*`)
3. Configuration file
4. Defaults

Example `.env`:
```
DAFX_LOG_LEVEL=DEBUG
DAFX_DSP_SAMPLE_RATE=48000
DAFX_EXEC_DRY_RUN=false
```

## Development

```bash
# Run tests
pytest

# Type checking
mypy dafx_execution

# Format code
black dafx_execution
ruff check dafx_execution
```
