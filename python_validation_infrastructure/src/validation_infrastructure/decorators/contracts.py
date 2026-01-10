"""Design by Contract decorators for validation."""

from __future__ import annotations

import asyncio
import functools
import inspect
from typing import Any, Callable, Dict, List, Optional, TypeVar, Union

from validation_infrastructure.errors.exceptions import ValidationError, ValidationErrorCollection

F = TypeVar("F", bound=Callable[..., Any])


class ContractViolation(ValidationError):
    """Raised when a design contract is violated."""

    def __init__(
        self,
        contract_type: str,
        condition_name: str,
        message: str,
        function_name: str,
        value: Any = None,
    ):
        super().__init__(
            message=f"{contract_type} contract '{condition_name}' violated in {function_name}: {message}",
            code=f"{contract_type.lower()}_violation",
            value=value,
            context={
                "contract_type": contract_type,
                "condition_name": condition_name,
                "function_name": function_name,
            },
        )
        self.contract_type = contract_type
        self.condition_name = condition_name
        self.function_name = function_name


class Condition:
    """Represents a contract condition."""

    def __init__(
        self,
        predicate: Callable[..., bool],
        message: Optional[str] = None,
        name: Optional[str] = None,
    ):
        self.predicate = predicate
        self.message = message or "Condition not satisfied"
        self.name = name or getattr(predicate, "__name__", "unnamed")
    
    def check(self, *args: Any, **kwargs: Any) -> tuple[bool, str]:
        """Check if condition is satisfied."""
        try:
            result = self.predicate(*args, **kwargs)
            return result, self.message
        except Exception as e:
            return False, f"{self.message} (error: {e})"


def requires(
    *conditions: Union[Callable[..., bool], Condition],
    message: Optional[str] = None,
) -> Callable[[F], F]:
    """
    Decorator specifying preconditions for a function.
    
    Preconditions must be true when the function is called.
    
    Args:
        conditions: Predicates that must return True
        message: Optional custom error message
    
    Example:
        @requires(lambda x: x > 0, message="x must be positive")
        def sqrt(x: float) -> float:
            return x ** 0.5
        
        @requires(
            lambda items: len(items) > 0,
            lambda items: all(isinstance(i, int) for i in items),
        )
        def process_items(items: List[int]) -> int:
            return sum(items)
    """
    parsed_conditions: List[Condition] = []
    for cond in conditions:
        if isinstance(cond, Condition):
            parsed_conditions.append(cond)
        else:
            parsed_conditions.append(Condition(cond, message=message))
    
    def decorator(func: F) -> F:
        func_name = func.__qualname__
        
        @functools.wraps(func)
        def sync_wrapper(*args: Any, **kwargs: Any) -> Any:
            # Get argument names for better error messages
            sig = inspect.signature(func)
            bound = sig.bind(*args, **kwargs)
            bound.apply_defaults()
            
            for condition in parsed_conditions:
                # Try calling with bound arguments first
                try:
                    satisfied, msg = condition.check(**bound.arguments)
                except TypeError:
                    # Fall back to positional args
                    satisfied, msg = condition.check(*args, **kwargs)
                
                if not satisfied:
                    raise ContractViolation(
                        contract_type="Precondition",
                        condition_name=condition.name,
                        message=msg,
                        function_name=func_name,
                        value=bound.arguments,
                    )
            
            return func(*args, **kwargs)
        
        @functools.wraps(func)
        async def async_wrapper(*args: Any, **kwargs: Any) -> Any:
            sig = inspect.signature(func)
            bound = sig.bind(*args, **kwargs)
            bound.apply_defaults()
            
            for condition in parsed_conditions:
                try:
                    satisfied, msg = condition.check(**bound.arguments)
                except TypeError:
                    satisfied, msg = condition.check(*args, **kwargs)
                
                if not satisfied:
                    raise ContractViolation(
                        contract_type="Precondition",
                        condition_name=condition.name,
                        message=msg,
                        function_name=func_name,
                        value=bound.arguments,
                    )
            
            return await func(*args, **kwargs)
        
        if asyncio.iscoroutinefunction(func):
            return async_wrapper  # type: ignore
        return sync_wrapper  # type: ignore
    
    return decorator


def ensures(
    *conditions: Union[Callable[..., bool], Condition],
    message: Optional[str] = None,
) -> Callable[[F], F]:
    """
    Decorator specifying postconditions for a function.
    
    Postconditions must be true after the function returns.
    The predicate receives the return value as its first argument.
    
    Args:
        conditions: Predicates receiving (result, *args, **kwargs)
        message: Optional custom error message
    
    Example:
        @ensures(lambda result: result >= 0, message="Result must be non-negative")
        def abs_value(x: float) -> float:
            return x if x >= 0 else -x
        
        @ensures(lambda result, items: result <= len(items))
        def count_positive(items: List[int]) -> int:
            return sum(1 for i in items if i > 0)
    """
    parsed_conditions: List[Condition] = []
    for cond in conditions:
        if isinstance(cond, Condition):
            parsed_conditions.append(cond)
        else:
            parsed_conditions.append(Condition(cond, message=message))
    
    def decorator(func: F) -> F:
        func_name = func.__qualname__
        
        @functools.wraps(func)
        def sync_wrapper(*args: Any, **kwargs: Any) -> Any:
            result = func(*args, **kwargs)
            
            for condition in parsed_conditions:
                # Try with result + kwargs first
                try:
                    sig = inspect.signature(func)
                    bound = sig.bind(*args, **kwargs)
                    bound.apply_defaults()
                    satisfied, msg = condition.check(result, **bound.arguments)
                except TypeError:
                    # Fall back to result + args
                    try:
                        satisfied, msg = condition.check(result, *args, **kwargs)
                    except TypeError:
                        # Just result
                        satisfied, msg = condition.check(result)
                
                if not satisfied:
                    raise ContractViolation(
                        contract_type="Postcondition",
                        condition_name=condition.name,
                        message=msg,
                        function_name=func_name,
                        value=result,
                    )
            
            return result
        
        @functools.wraps(func)
        async def async_wrapper(*args: Any, **kwargs: Any) -> Any:
            result = await func(*args, **kwargs)
            
            for condition in parsed_conditions:
                try:
                    sig = inspect.signature(func)
                    bound = sig.bind(*args, **kwargs)
                    bound.apply_defaults()
                    satisfied, msg = condition.check(result, **bound.arguments)
                except TypeError:
                    try:
                        satisfied, msg = condition.check(result, *args, **kwargs)
                    except TypeError:
                        satisfied, msg = condition.check(result)
                
                if not satisfied:
                    raise ContractViolation(
                        contract_type="Postcondition",
                        condition_name=condition.name,
                        message=msg,
                        function_name=func_name,
                        value=result,
                    )
            
            return result
        
        if asyncio.iscoroutinefunction(func):
            return async_wrapper  # type: ignore
        return sync_wrapper  # type: ignore
    
    return decorator


def invariant(
    *conditions: Union[Callable[[Any], bool], Condition],
    message: Optional[str] = None,
) -> Callable[[type], type]:
    """
    Class decorator specifying invariants that must hold.
    
    Invariants are checked after __init__ and after every public method call.
    
    Args:
        conditions: Predicates receiving the instance (self)
        message: Optional custom error message
    
    Example:
        @invariant(lambda self: self.balance >= 0, message="Balance cannot be negative")
        class BankAccount:
            def __init__(self, initial_balance: float):
                self.balance = initial_balance
            
            def withdraw(self, amount: float) -> None:
                self.balance -= amount
    """
    parsed_conditions: List[Condition] = []
    for cond in conditions:
        if isinstance(cond, Condition):
            parsed_conditions.append(cond)
        else:
            parsed_conditions.append(Condition(cond, message=message))
    
    def check_invariants(instance: Any, class_name: str) -> None:
        """Check all invariants on the instance."""
        for condition in parsed_conditions:
            satisfied, msg = condition.check(instance)
            if not satisfied:
                raise ContractViolation(
                    contract_type="Invariant",
                    condition_name=condition.name,
                    message=msg,
                    function_name=class_name,
                    value=instance,
                )
    
    def wrap_method(method: Callable[..., Any], class_name: str) -> Callable[..., Any]:
        """Wrap a method to check invariants after execution."""
        if asyncio.iscoroutinefunction(method):
            @functools.wraps(method)
            async def async_wrapper(self: Any, *args: Any, **kwargs: Any) -> Any:
                result = await method(self, *args, **kwargs)
                check_invariants(self, class_name)
                return result
            return async_wrapper
        else:
            @functools.wraps(method)
            def sync_wrapper(self: Any, *args: Any, **kwargs: Any) -> Any:
                result = method(self, *args, **kwargs)
                check_invariants(self, class_name)
                return result
            return sync_wrapper
    
    def decorator(cls: type) -> type:
        class_name = cls.__qualname__
        
        # Wrap __init__
        original_init = cls.__init__
        
        @functools.wraps(original_init)
        def new_init(self: Any, *args: Any, **kwargs: Any) -> None:
            original_init(self, *args, **kwargs)
            check_invariants(self, class_name)
        
        cls.__init__ = new_init  # type: ignore
        
        # Wrap public methods
        for name, value in list(cls.__dict__.items()):
            if (
                callable(value)
                and not name.startswith("_")
                and name != "__init__"
            ):
                setattr(cls, name, wrap_method(value, class_name))
        
        return cls
    
    return decorator


class Contract:
    """
    Builder for creating complex contracts.
    
    Example:
        @contract()
        .requires(lambda x: x > 0)
        .ensures(lambda result: result >= 0)
        .apply
        def sqrt(x: float) -> float:
            return x ** 0.5
    """

    def __init__(self):
        self._preconditions: List[Condition] = []
        self._postconditions: List[Condition] = []
    
    def requires(
        self,
        predicate: Union[Callable[..., bool], Condition],
        message: Optional[str] = None,
    ) -> Contract:
        """Add a precondition."""
        if isinstance(predicate, Condition):
            self._preconditions.append(predicate)
        else:
            self._preconditions.append(Condition(predicate, message=message))
        return self
    
    def ensures(
        self,
        predicate: Union[Callable[..., bool], Condition],
        message: Optional[str] = None,
    ) -> Contract:
        """Add a postcondition."""
        if isinstance(predicate, Condition):
            self._postconditions.append(predicate)
        else:
            self._postconditions.append(Condition(predicate, message=message))
        return self
    
    @property
    def apply(self) -> Callable[[F], F]:
        """Apply the contract to a function."""
        preconditions = self._preconditions
        postconditions = self._postconditions
        
        def decorator(func: F) -> F:
            wrapped = func
            
            if postconditions:
                wrapped = ensures(*postconditions)(wrapped)
            
            if preconditions:
                wrapped = requires(*preconditions)(wrapped)
            
            return wrapped  # type: ignore
        
        return decorator


def contract() -> Contract:
    """
    Create a new contract builder.
    
    Example:
        @contract()
        .requires(lambda x, y: y != 0, message="Divisor cannot be zero")
        .ensures(lambda result, x, y: result * y == x)
        .apply
        def divide(x: float, y: float) -> float:
            return x / y
    """
    return Contract()


class OldValue:
    """
    Captures values at function entry for use in postconditions.
    
    Example:
        @ensures(lambda result, old: result == old.balance - amount)
        def withdraw(self, amount: float) -> float:
            self.balance -= amount
            return self.balance
    """

    def __init__(self, **captured: Any):
        self._values = captured
    
    def __getattr__(self, name: str) -> Any:
        if name.startswith("_"):
            return super().__getattribute__(name)
        return self._values.get(name)


def captures(*names: str) -> Callable[[F], F]:
    """
    Decorator that captures specified values before function execution.
    
    The captured values are available in postconditions via an 'old' parameter.
    
    Example:
        @captures("balance")
        @ensures(lambda result, old: self.balance == old.balance - result)
        def withdraw(self, amount: float) -> float:
            self.balance -= amount
            return amount
    """
    def decorator(func: F) -> F:
        @functools.wraps(func)
        def wrapper(self: Any, *args: Any, **kwargs: Any) -> Any:
            # Capture old values
            captured = {}
            for name in names:
                if hasattr(self, name):
                    captured[name] = getattr(self, name)
            
            # Store for postcondition access
            wrapper._old_values = OldValue(**captured)  # type: ignore
            
            return func(self, *args, **kwargs)
        
        return wrapper  # type: ignore
    
    return decorator
