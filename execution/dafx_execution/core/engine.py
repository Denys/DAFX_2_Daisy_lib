"""Main execution engine for DAFX processing tasks.

The ExecutionEngine orchestrates command execution using the Command Pattern.
It provides:
- Unified command registration and dispatch
- Pre/post execution hooks
- Error handling and cleanup
- Execution metrics collection
"""

from __future__ import annotations

import time
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from enum import Enum, auto
from pathlib import Path
from typing import Any, Callable, Generic, TypeVar

from dafx_execution.core.exceptions import ExecutionError, ProcessingError
from dafx_execution.core.logger import get_logger, LoggerMixin


T = TypeVar("T")  # Command result type


class CommandStatus(Enum):
    """Status of a command execution."""
    PENDING = auto()
    RUNNING = auto()
    SUCCESS = auto()
    FAILED = auto()
    CANCELLED = auto()


@dataclass
class CommandResult(Generic[T]):
    """Result of a command execution.
    
    Attributes:
        status: Execution status.
        data: Result data if successful.
        error: Error if failed.
        duration_seconds: Execution time.
        metrics: Additional execution metrics.
    """
    status: CommandStatus
    data: T | None = None
    error: ExecutionError | None = None
    duration_seconds: float = 0.0
    metrics: dict[str, Any] = field(default_factory=dict)
    
    @property
    def is_success(self) -> bool:
        """Check if command succeeded."""
        return self.status == CommandStatus.SUCCESS
    
    def unwrap(self) -> T:
        """Get result data or raise error.
        
        Returns:
            The result data if successful.
        
        Raises:
            ExecutionError: If command failed.
        """
        if self.error:
            raise self.error
        if self.data is None:
            raise ExecutionError("Command completed but returned no data")
        return self.data


class Command(ABC, Generic[T]):
    """Base class for executable commands.
    
    Commands encapsulate a single unit of work that can be:
    - Validated before execution
    - Executed with error handling
    - Cleaned up after execution (success or failure)
    
    Example:
        >>> class ProcessAudioCommand(Command[Path]):
        ...     def __init__(self, input_file: Path):
        ...         self.input_file = input_file
        ...     
        ...     def validate(self) -> None:
        ...         if not self.input_file.exists():
        ...             raise ValidationError("File not found")
        ...     
        ...     def execute(self) -> Path:
        ...         # Process audio...
        ...         return output_path
    """
    
    @property
    def name(self) -> str:
        """Command name for logging."""
        return self.__class__.__name__
    
    def validate(self) -> None:
        """Validate command before execution.
        
        Override to add validation logic. Should raise ExecutionError
        subclass on validation failure.
        """
        pass
    
    @abstractmethod
    def execute(self) -> T:
        """Execute the command.
        
        Returns:
            Command result of type T.
        
        Raises:
            ExecutionError: On execution failure.
        """
        ...
    
    def cleanup(self, success: bool) -> None:
        """Cleanup after execution.
        
        Called after execute() completes, regardless of success/failure.
        Override to release resources.
        
        Args:
            success: Whether execution succeeded.
        """
        pass


# Type for pre/post execution hooks
HookFn = Callable[[Command[Any]], None]


class ExecutionEngine(LoggerMixin):
    """Central engine for running commands.
    
    Features:
    - Command registration with validation
    - Pre/post execution hooks
    - Metrics collection
    - Dry-run mode
    
    Example:
        >>> engine = ExecutionEngine()
        >>> engine.add_pre_hook(lambda cmd: print(f"Starting {cmd.name}"))
        >>> result = engine.run(ProcessAudioCommand(Path("audio.wav")))
        >>> if result.is_success:
        ...     print(f"Output: {result.data}")
    """
    
    def __init__(
        self,
        dry_run: bool = False,
        collect_metrics: bool = True,
    ) -> None:
        """Initialize the execution engine.
        
        Args:
            dry_run: If True, validate but don't execute commands.
            collect_metrics: Whether to collect execution metrics.
        """
        self._dry_run = dry_run
        self._collect_metrics = collect_metrics
        self._pre_hooks: list[HookFn] = []
        self._post_hooks: list[HookFn] = []
        self._commands_run: int = 0
        self._commands_failed: int = 0
        self._total_duration: float = 0.0
    
    @property
    def dry_run(self) -> bool:
        """Check if engine is in dry-run mode."""
        return self._dry_run
    
    @property
    def stats(self) -> dict[str, Any]:
        """Get execution statistics."""
        return {
            "commands_run": self._commands_run,
            "commands_failed": self._commands_failed,
            "success_rate": (
                (self._commands_run - self._commands_failed) / self._commands_run
                if self._commands_run > 0 else 0.0
            ),
            "total_duration_seconds": self._total_duration,
            "avg_duration_seconds": (
                self._total_duration / self._commands_run
                if self._commands_run > 0 else 0.0
            ),
        }
    
    def add_pre_hook(self, hook: HookFn) -> None:
        """Add a hook to run before each command.
        
        Args:
            hook: Function called with command before execution.
        """
        self._pre_hooks.append(hook)
    
    def add_post_hook(self, hook: HookFn) -> None:
        """Add a hook to run after each command.
        
        Args:
            hook: Function called with command after execution.
        """
        self._post_hooks.append(hook)
    
    def run(self, command: Command[T]) -> CommandResult[T]:
        """Run a command with full lifecycle management.
        
        Executes:
        1. Pre-hooks
        2. Validation
        3. Execution (skipped in dry-run mode)
        4. Cleanup
        5. Post-hooks
        
        Args:
            command: The command to execute.
        
        Returns:
            CommandResult with status, data, and metrics.
        """
        self.logger.info("Running command", command=command.name, dry_run=self._dry_run)
        
        start_time = time.perf_counter()
        result: CommandResult[T]
        success = False
        
        try:
            # Pre-hooks
            for hook in self._pre_hooks:
                hook(command)
            
            # Validation
            command.validate()
            
            # Dry-run check
            if self._dry_run:
                self.logger.info("Dry-run mode: skipping execution", command=command.name)
                result = CommandResult(
                    status=CommandStatus.SUCCESS,
                    data=None,
                    metrics={"dry_run": True},
                )
                success = True
            else:
                # Execute
                data = command.execute()
                result = CommandResult(
                    status=CommandStatus.SUCCESS,
                    data=data,
                )
                success = True
                self.logger.info("Command succeeded", command=command.name)
        
        except ExecutionError as e:
            self.logger.error(
                "Command failed",
                command=command.name,
                error=str(e),
                error_type=type(e).__name__,
            )
            result = CommandResult(
                status=CommandStatus.FAILED,
                error=e,
            )
            self._commands_failed += 1
        
        except Exception as e:
            # Wrap unexpected exceptions
            self.logger.exception("Unexpected error in command", command=command.name)
            wrapped_error = ProcessingError(
                f"Unexpected error: {e}",
                stage=command.name,
                cause=e,
            )
            result = CommandResult(
                status=CommandStatus.FAILED,
                error=wrapped_error,
            )
            self._commands_failed += 1
        
        finally:
            # Duration tracking
            duration = time.perf_counter() - start_time
            result.duration_seconds = duration
            
            # Cleanup
            try:
                command.cleanup(success)
            except Exception as cleanup_error:
                self.logger.warning(
                    "Cleanup failed",
                    command=command.name,
                    error=str(cleanup_error),
                )
            
            # Post-hooks
            for hook in self._post_hooks:
                try:
                    hook(command)
                except Exception as hook_error:
                    self.logger.warning(
                        "Post-hook failed",
                        command=command.name,
                        error=str(hook_error),
                    )
            
            # Metrics
            self._commands_run += 1
            self._total_duration += duration
            
            if self._collect_metrics:
                result.metrics.update({
                    "duration_seconds": duration,
                    "command_name": command.name,
                })
        
        return result
    
    def run_batch(
        self,
        commands: list[Command[T]],
        stop_on_failure: bool = False,
    ) -> list[CommandResult[T]]:
        """Run multiple commands in sequence.
        
        Args:
            commands: List of commands to execute.
            stop_on_failure: If True, stop on first failure.
        
        Returns:
            List of results in order of execution.
        """
        results: list[CommandResult[T]] = []
        
        for command in commands:
            result = self.run(command)
            results.append(result)
            
            if stop_on_failure and not result.is_success:
                self.logger.warning(
                    "Batch execution stopped on failure",
                    command=command.name,
                    completed=len(results),
                    total=len(commands),
                )
                break
        
        return results
