"""Asynchronous validation support for concurrent data processing."""

from __future__ import annotations

import asyncio
from dataclasses import dataclass
from typing import Any, Awaitable, Callable, Dict, Generic, List, Optional, TypeVar, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
)

T = TypeVar("T")


class AsyncValidator(BaseValidator[T], Generic[T]):
    """Base class for asynchronous validators."""

    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Validate the given value asynchronously."""
        raise NotImplementedError("Subclasses must implement validate_async")
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        """Synchronous validation - runs async in new event loop if needed."""
        try:
            loop = asyncio.get_running_loop()
            # Already in async context - create task
            import concurrent.futures
            with concurrent.futures.ThreadPoolExecutor() as pool:
                future = pool.submit(
                    asyncio.run,
                    self.validate_async(value, context)
                )
                return future.result()
        except RuntimeError:
            # No running loop - run directly
            return asyncio.run(self.validate_async(value, context))


class AsyncCallableValidator(AsyncValidator[T]):
    """Validator that wraps an async callable."""

    def __init__(
        self,
        validator_func: Callable[[Any], Awaitable[Union[bool, ValidationResult[T], T]]],
        error_message: str = "Validation failed",
        error_code: str = "validation_error",
    ):
        super().__init__(name="AsyncCallableValidator")
        self.validator_func = validator_func
        self.error_message = error_message
        self.error_code = error_code
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        
        try:
            result = await self.validator_func(value)
            
            # Handle different return types
            if isinstance(result, ValidationResult):
                return result
            elif isinstance(result, bool):
                if result:
                    return ValidationResult.success(value)
                else:
                    return ValidationResult.from_error(
                        self.error_message,
                        code=self.error_code,
                        value=value,
                    )
            else:
                # Assume transformed value
                return ValidationResult.success(result)
        except Exception as e:
            return ValidationResult.from_error(
                f"Async validation error: {str(e)}",
                code="async_error",
                value=value,
            )


class ParallelValidator(AsyncValidator[List[T]]):
    """Validates multiple items in parallel."""

    def __init__(
        self,
        item_validator: BaseValidator[T],
        max_concurrency: int = 10,
        fail_fast: bool = False,
    ):
        super().__init__(name="ParallelValidator")
        self.item_validator = item_validator
        self.max_concurrency = max_concurrency
        self.fail_fast = fail_fast
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[List[T]]:
        ctx = self._create_context(context)
        
        if not isinstance(value, (list, tuple)):
            return ValidationResult.from_error(
                f"Expected list, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        semaphore = asyncio.Semaphore(self.max_concurrency)
        
        async def validate_item(index: int, item: Any) -> tuple[int, ValidationResult[T]]:
            async with semaphore:
                item_ctx = ctx.child(str(index))
                
                # Use async validation if available
                if hasattr(self.item_validator, "validate_async"):
                    result = await self.item_validator.validate_async(item, item_ctx)
                else:
                    result = self.item_validator.validate(item, item_ctx)
                
                return index, result
        
        # Create tasks
        tasks = [
            asyncio.create_task(validate_item(i, item))
            for i, item in enumerate(value)
        ]
        
        # Gather results
        results: Dict[int, ValidationResult[T]] = {}
        errors: List[ValidationIssue] = []
        
        if self.fail_fast:
            # Use as_completed for fail-fast
            for coro in asyncio.as_completed(tasks):
                index, result = await coro
                results[index] = result
                
                if not result.is_valid:
                    errors.extend(result.issues)
                    # Cancel remaining tasks
                    for task in tasks:
                        if not task.done():
                            task.cancel()
                    break
        else:
            # Wait for all
            completed = await asyncio.gather(*tasks, return_exceptions=True)
            for item in completed:
                if isinstance(item, Exception):
                    errors.append(
                        ValidationIssue(
                            message=f"Validation task failed: {item}",
                            code="task_error",
                        )
                    )
                else:
                    index, result = item
                    results[index] = result
                    if not result.is_valid:
                        errors.extend(result.issues)
        
        # Build validated list
        validated: List[T] = []
        for i in range(len(value)):
            if i in results and results[i].is_valid:
                validated.append(results[i].value)
            else:
                validated.append(value[i])  # type: ignore
        
        if errors:
            return ValidationResult.failure(errors, value=validated)
        
        return ValidationResult.success(validated)


class BatchValidator(AsyncValidator[List[T]]):
    """Validates items in batches for efficiency."""

    def __init__(
        self,
        batch_validator: Callable[[List[Any]], Awaitable[List[ValidationResult[T]]]],
        batch_size: int = 100,
    ):
        super().__init__(name="BatchValidator")
        self.batch_validator = batch_validator
        self.batch_size = batch_size
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[List[T]]:
        ctx = self._create_context(context)
        
        if not isinstance(value, (list, tuple)):
            return ValidationResult.from_error(
                f"Expected list, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        all_results: List[ValidationResult[T]] = []
        
        # Process in batches
        for i in range(0, len(value), self.batch_size):
            batch = value[i:i + self.batch_size]
            batch_results = await self.batch_validator(batch)
            all_results.extend(batch_results)
        
        # Combine results
        validated: List[T] = []
        issues: List[ValidationIssue] = []
        
        for i, result in enumerate(all_results):
            if result.is_valid:
                validated.append(result.value)
            else:
                # Add index to issue paths
                for issue in result.issues:
                    issue.path = f"[{i}].{issue.path}" if issue.path else f"[{i}]"
                issues.extend(result.issues)
                validated.append(value[i])  # type: ignore
        
        if issues:
            return ValidationResult.failure(issues, value=validated)
        
        return ValidationResult.success(validated)


class ConcurrentFieldValidator(AsyncValidator[Dict[str, Any]]):
    """Validates object fields concurrently."""

    def __init__(
        self,
        field_validators: Dict[str, BaseValidator[Any]],
        required_fields: Optional[set] = None,
    ):
        super().__init__(name="ConcurrentFieldValidator")
        self.field_validators = field_validators
        self.required_fields = required_fields or set()
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[Dict[str, Any]]:
        ctx = self._create_context(context)
        
        if not isinstance(value, dict):
            return ValidationResult.from_error(
                f"Expected dict, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        # Check required fields
        missing = self.required_fields - set(value.keys())
        if missing:
            issues = [
                ValidationIssue(
                    message=f"Missing required field: {field}",
                    field=field,
                    code="missing_field",
                )
                for field in missing
            ]
            return ValidationResult.failure(issues, value=value)
        
        async def validate_field(
            name: str,
            field_value: Any,
            validator: BaseValidator[Any],
        ) -> tuple[str, ValidationResult[Any]]:
            field_ctx = ctx.child(name)
            
            if hasattr(validator, "validate_async"):
                result = await validator.validate_async(field_value, field_ctx)
            else:
                result = validator.validate(field_value, field_ctx)
            
            return name, result
        
        # Create tasks for fields that have validators
        tasks = [
            validate_field(name, value.get(name), validator)
            for name, validator in self.field_validators.items()
            if name in value
        ]
        
        # Run concurrently
        completed = await asyncio.gather(*tasks)
        
        # Collect results
        validated: Dict[str, Any] = dict(value)  # Start with original
        issues: List[ValidationIssue] = []
        
        for name, result in completed:
            if result.is_valid:
                validated[name] = result.value
            else:
                issues.extend(result.issues)
        
        if issues:
            return ValidationResult.failure(issues, value=validated)
        
        return ValidationResult.success(validated)


class AsyncPipelineValidator(AsyncValidator[T]):
    """Runs validators in a pipeline, passing each result to the next."""

    def __init__(self, validators: List[BaseValidator[Any]]):
        super().__init__(name="AsyncPipelineValidator")
        self.validators = validators
    
    async def validate_async(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[T]:
        ctx = self._create_context(context)
        current = value
        all_issues: List[ValidationIssue] = []
        all_warnings: List[ValidationIssue] = []
        
        for validator in self.validators:
            if hasattr(validator, "validate_async"):
                result = await validator.validate_async(current, ctx)
            else:
                result = validator.validate(current, ctx)
            
            all_warnings.extend(result.warnings)
            
            if not result.is_valid:
                all_issues.extend(result.issues)
                break
            
            current = result.value
        
        if all_issues:
            result = ValidationResult.failure(all_issues, value=current)
        else:
            result = ValidationResult.success(current)
        
        result.warnings = all_warnings
        return result  # type: ignore


async def validate_all(
    validators: List[tuple[str, Any, BaseValidator[Any]]],
    concurrency: int = 10,
    fail_fast: bool = False,
) -> Dict[str, ValidationResult[Any]]:
    """
    Validate multiple values concurrently.
    
    Args:
        validators: List of (name, value, validator) tuples
        concurrency: Maximum concurrent validations
        fail_fast: Stop on first error
    
    Returns:
        Dict mapping names to validation results
    
    Example:
        results = await validate_all([
            ("user", user_data, user_validator),
            ("order", order_data, order_validator),
        ])
    """
    semaphore = asyncio.Semaphore(concurrency)
    results: Dict[str, ValidationResult[Any]] = {}
    
    async def run_validation(
        name: str,
        value: Any,
        validator: BaseValidator[Any],
    ) -> tuple[str, ValidationResult[Any]]:
        async with semaphore:
            if hasattr(validator, "validate_async"):
                result = await validator.validate_async(value)
            else:
                loop = asyncio.get_event_loop()
                result = await loop.run_in_executor(
                    None,
                    validator.validate,
                    value,
                )
            return name, result
    
    tasks = [
        asyncio.create_task(run_validation(name, value, validator))
        for name, value, validator in validators
    ]
    
    if fail_fast:
        for coro in asyncio.as_completed(tasks):
            name, result = await coro
            results[name] = result
            if not result.is_valid:
                for task in tasks:
                    if not task.done():
                        task.cancel()
                break
    else:
        completed = await asyncio.gather(*tasks, return_exceptions=True)
        for item in completed:
            if isinstance(item, Exception):
                continue
            name, result = item
            results[name] = result
    
    return results


def make_async(validator: BaseValidator[T]) -> AsyncValidator[T]:
    """
    Wrap a synchronous validator to work asynchronously.
    
    Example:
        async_validator = make_async(StringValidator())
        result = await async_validator.validate_async("test")
    """
    class WrappedValidator(AsyncValidator[T]):
        def __init__(self, inner: BaseValidator[T]):
            super().__init__(name=f"Async({inner.name})")
            self.inner = inner
        
        async def validate_async(
            self,
            value: Any,
            context: Optional[ValidationContext] = None,
        ) -> ValidationResult[T]:
            loop = asyncio.get_event_loop()
            return await loop.run_in_executor(
                None,
                self.inner.validate,
                value,
                context,
            )
    
    return WrappedValidator(validator)
