"""Sanitization pipelines for processing data through multiple stages."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass, field
from enum import Enum
from typing import Any, Callable, Dict, Generic, List, Optional, TypeVar, Union

from validation_infrastructure.sanitization.sanitizers import Sanitizer

T = TypeVar("T")


class StepResult(Enum):
    """Result of a sanitization step."""
    CONTINUE = "continue"
    SKIP = "skip"
    ABORT = "abort"


@dataclass
class SanitizationStepResult:
    """Result from a single sanitization step."""
    
    value: Any
    step_name: str
    result: StepResult = StepResult.CONTINUE
    message: Optional[str] = None
    original_value: Any = None
    modified: bool = False
    
    def __post_init__(self):
        if self.original_value is None:
            self.original_value = self.value


@dataclass
class SanitizationPipelineResult:
    """Result from running a full sanitization pipeline."""
    
    value: Any
    original_value: Any
    success: bool = True
    steps_executed: int = 0
    steps_skipped: int = 0
    step_results: List[SanitizationStepResult] = field(default_factory=list)
    errors: List[str] = field(default_factory=list)
    
    @property
    def was_modified(self) -> bool:
        """Check if value was modified during sanitization."""
        return self.value != self.original_value
    
    @property
    def modifications(self) -> List[str]:
        """Get list of modifications made."""
        return [
            f"{r.step_name}: {r.message}"
            for r in self.step_results
            if r.modified and r.message
        ]


class SanitizationStep(Generic[T]):
    """A single step in a sanitization pipeline."""

    def __init__(
        self,
        name: str,
        sanitizer: Union[Sanitizer[T], Callable[[Any], T]],
        condition: Optional[Callable[[Any], bool]] = None,
        on_error: str = "continue",  # "continue", "skip", "abort"
        description: Optional[str] = None,
    ):
        """
        Initialize a sanitization step.
        
        Args:
            name: Step identifier
            sanitizer: Sanitizer or callable to apply
            condition: Optional condition for running this step
            on_error: How to handle errors
            description: Description of what this step does
        """
        self.name = name
        self.sanitizer = sanitizer
        self.condition = condition
        self.on_error = on_error
        self.description = description
    
    def should_run(self, value: Any) -> bool:
        """Check if this step should run."""
        if self.condition is None:
            return True
        try:
            return self.condition(value)
        except Exception:
            return False
    
    def execute(self, value: Any) -> SanitizationStepResult:
        """Execute this step."""
        original = value
        
        if not self.should_run(value):
            return SanitizationStepResult(
                value=value,
                step_name=self.name,
                result=StepResult.SKIP,
                message="Condition not met",
            )
        
        try:
            if isinstance(self.sanitizer, Sanitizer):
                result = self.sanitizer.sanitize(value)
            else:
                result = self.sanitizer(value)
            
            modified = result != original
            
            return SanitizationStepResult(
                value=result,
                step_name=self.name,
                result=StepResult.CONTINUE,
                original_value=original,
                modified=modified,
                message=self.description if modified else None,
            )
        except Exception as e:
            if self.on_error == "abort":
                return SanitizationStepResult(
                    value=value,
                    step_name=self.name,
                    result=StepResult.ABORT,
                    message=f"Error: {str(e)}",
                    original_value=original,
                )
            elif self.on_error == "skip":
                return SanitizationStepResult(
                    value=value,
                    step_name=self.name,
                    result=StepResult.SKIP,
                    message=f"Skipped due to error: {str(e)}",
                    original_value=original,
                )
            else:  # continue
                return SanitizationStepResult(
                    value=value,
                    step_name=self.name,
                    result=StepResult.CONTINUE,
                    message=f"Error (continued): {str(e)}",
                    original_value=original,
                )
    
    async def execute_async(self, value: Any) -> SanitizationStepResult:
        """Execute this step asynchronously."""
        # Most sanitizers are CPU-bound, so run in executor
        loop = asyncio.get_event_loop()
        return await loop.run_in_executor(None, self.execute, value)


class SanitizationPipeline(Generic[T]):
    """
    Pipeline for applying multiple sanitization steps in sequence.
    
    Example:
        pipeline = SanitizationPipeline("user_input")
        pipeline.add_step("strip", StringSanitizer(strip=True))
        pipeline.add_step("escape", HTMLSanitizer())
        pipeline.add_step("truncate", StringSanitizer(max_length=1000))
        
        result = pipeline.execute(user_input)
        safe_input = result.value
    """

    def __init__(
        self,
        name: str,
        fail_fast: bool = False,
        log_changes: bool = True,
    ):
        """
        Initialize pipeline.
        
        Args:
            name: Pipeline identifier
            fail_fast: Stop on first error
            log_changes: Track changes made during sanitization
        """
        self.name = name
        self.fail_fast = fail_fast
        self.log_changes = log_changes
        self._steps: List[SanitizationStep[Any]] = []
    
    def add_step(
        self,
        name: str,
        sanitizer: Union[Sanitizer[Any], Callable[[Any], Any]],
        condition: Optional[Callable[[Any], bool]] = None,
        on_error: str = "continue",
        description: Optional[str] = None,
    ) -> SanitizationPipeline[T]:
        """Add a step to the pipeline."""
        step = SanitizationStep(
            name=name,
            sanitizer=sanitizer,
            condition=condition,
            on_error=on_error,
            description=description,
        )
        self._steps.append(step)
        return self
    
    def add_sanitizer(
        self,
        sanitizer: Sanitizer[Any],
        condition: Optional[Callable[[Any], bool]] = None,
    ) -> SanitizationPipeline[T]:
        """Add a sanitizer with auto-generated name."""
        name = type(sanitizer).__name__
        return self.add_step(name, sanitizer, condition)
    
    def execute(self, value: Any) -> SanitizationPipelineResult:
        """Execute the pipeline."""
        original = value
        current = value
        step_results: List[SanitizationStepResult] = []
        steps_executed = 0
        steps_skipped = 0
        errors: List[str] = []
        
        for step in self._steps:
            result = step.execute(current)
            step_results.append(result)
            
            if result.result == StepResult.ABORT:
                errors.append(f"{step.name}: {result.message}")
                return SanitizationPipelineResult(
                    value=current,
                    original_value=original,
                    success=False,
                    steps_executed=steps_executed,
                    steps_skipped=steps_skipped,
                    step_results=step_results,
                    errors=errors,
                )
            
            if result.result == StepResult.SKIP:
                steps_skipped += 1
                continue
            
            current = result.value
            steps_executed += 1
            
            if result.message and "Error" in result.message:
                errors.append(f"{step.name}: {result.message}")
                if self.fail_fast:
                    break
        
        return SanitizationPipelineResult(
            value=current,
            original_value=original,
            success=len(errors) == 0,
            steps_executed=steps_executed,
            steps_skipped=steps_skipped,
            step_results=step_results,
            errors=errors,
        )
    
    async def execute_async(self, value: Any) -> SanitizationPipelineResult:
        """Execute the pipeline asynchronously."""
        original = value
        current = value
        step_results: List[SanitizationStepResult] = []
        steps_executed = 0
        steps_skipped = 0
        errors: List[str] = []
        
        for step in self._steps:
            result = await step.execute_async(current)
            step_results.append(result)
            
            if result.result == StepResult.ABORT:
                errors.append(f"{step.name}: {result.message}")
                return SanitizationPipelineResult(
                    value=current,
                    original_value=original,
                    success=False,
                    steps_executed=steps_executed,
                    steps_skipped=steps_skipped,
                    step_results=step_results,
                    errors=errors,
                )
            
            if result.result == StepResult.SKIP:
                steps_skipped += 1
                continue
            
            current = result.value
            steps_executed += 1
        
        return SanitizationPipelineResult(
            value=current,
            original_value=original,
            success=len(errors) == 0,
            steps_executed=steps_executed,
            steps_skipped=steps_skipped,
            step_results=step_results,
            errors=errors,
        )
    
    def __call__(self, value: Any) -> Any:
        """Execute pipeline and return sanitized value."""
        result = self.execute(value)
        return result.value


def create_pipeline(
    name: str,
    steps: List[Dict[str, Any]],
    fail_fast: bool = False,
) -> SanitizationPipeline[Any]:
    """
    Create a pipeline from a configuration dictionary.
    
    Example:
        pipeline = create_pipeline("input", [
            {"name": "strip", "type": "string", "strip": True},
            {"name": "escape", "type": "html"},
            {"name": "limit", "type": "string", "max_length": 500},
        ])
    """
    from validation_infrastructure.sanitization.sanitizers import (
        StringSanitizer,
        HTMLSanitizer,
        SQLSanitizer,
        PathSanitizer,
        NumericSanitizer,
        EmailSanitizer,
        URLSanitizer,
    )
    
    sanitizer_map: Dict[str, type] = {
        "string": StringSanitizer,
        "html": HTMLSanitizer,
        "sql": SQLSanitizer,
        "path": PathSanitizer,
        "numeric": NumericSanitizer,
        "email": EmailSanitizer,
        "url": URLSanitizer,
    }
    
    pipeline: SanitizationPipeline[Any] = SanitizationPipeline(name, fail_fast=fail_fast)
    
    for step_config in steps:
        step_name = step_config.get("name", "unnamed")
        step_type = step_config.get("type", "string")
        
        sanitizer_class = sanitizer_map.get(step_type)
        if sanitizer_class is None:
            raise ValueError(f"Unknown sanitizer type: {step_type}")
        
        # Extract sanitizer options (everything except name and type)
        options = {
            k: v for k, v in step_config.items()
            if k not in ("name", "type", "condition", "on_error", "description")
        }
        
        sanitizer = sanitizer_class(**options)
        
        pipeline.add_step(
            name=step_name,
            sanitizer=sanitizer,
            on_error=step_config.get("on_error", "continue"),
            description=step_config.get("description"),
        )
    
    return pipeline


# Pre-built pipelines for common use cases

def create_user_input_pipeline(max_length: int = 1000) -> SanitizationPipeline[str]:
    """Create a pipeline for sanitizing user input."""
    from validation_infrastructure.sanitization.sanitizers import StringSanitizer, HTMLSanitizer
    
    return (
        SanitizationPipeline[str]("user_input")
        .add_step("strip", StringSanitizer(strip=True))
        .add_step("normalize", StringSanitizer(
            normalize_unicode="NFC",
            remove_control_chars=True,
        ))
        .add_step("escape_html", HTMLSanitizer(strip_tags=True))
        .add_step("truncate", StringSanitizer(max_length=max_length))
    )


def create_html_content_pipeline(
    allowed_tags: Optional[set] = None,
) -> SanitizationPipeline[str]:
    """Create a pipeline for sanitizing HTML content."""
    from validation_infrastructure.sanitization.sanitizers import StringSanitizer, HTMLSanitizer
    
    return (
        SanitizationPipeline[str]("html_content")
        .add_step("normalize", StringSanitizer(normalize_unicode="NFC"))
        .add_step("sanitize_html", HTMLSanitizer(allowed_tags=allowed_tags))
        .add_step("cleanup", StringSanitizer(collapse_whitespace=True))
    )


def create_filename_pipeline() -> SanitizationPipeline[str]:
    """Create a pipeline for sanitizing filenames."""
    from validation_infrastructure.sanitization.sanitizers import StringSanitizer, PathSanitizer
    
    return (
        SanitizationPipeline[str]("filename")
        .add_step("strip", StringSanitizer(strip=True))
        .add_step("normalize", StringSanitizer(normalize_unicode="NFC"))
        .add_step(
            "sanitize_path",
            lambda x: PathSanitizer.sanitize_filename(x),
            description="Remove invalid filename characters",
        )
        .add_step("truncate", StringSanitizer(max_length=255))
    )


def create_sql_value_pipeline() -> SanitizationPipeline[str]:
    """Create a pipeline for sanitizing SQL values."""
    from validation_infrastructure.sanitization.sanitizers import StringSanitizer, SQLSanitizer
    
    return (
        SanitizationPipeline[str]("sql_value")
        .add_step("strip", StringSanitizer(strip=True))
        .add_step("sanitize_sql", SQLSanitizer())
    )
