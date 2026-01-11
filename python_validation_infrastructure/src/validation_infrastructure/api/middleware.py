"""API middleware for Flask and FastAPI request/response validation.

Provides decorators and middleware for automatic validation of
API requests and responses.
"""

from __future__ import annotations

import functools
from dataclasses import dataclass
from typing import Any, Callable, Type, TypeVar

from pydantic import BaseModel, ValidationError as PydanticValidationError

from validation_infrastructure.core.base import ValidationResult
from validation_infrastructure.errors.exceptions import ValidationError


T = TypeVar("T")


# ============================================================================
# Common Types
# ============================================================================

@dataclass
class APIValidationError:
    """Structured API validation error response."""
    
    status: str = "error"
    code: int = 400
    message: str = "Validation failed"
    errors: list[dict[str, Any]] | None = None
    
    def to_dict(self) -> dict[str, Any]:
        """Convert to dictionary for JSON response."""
        result = {
            "status": self.status,
            "code": self.code,
            "message": self.message,
        }
        if self.errors:
            result["errors"] = self.errors
        return result


# ============================================================================
# Flask Middleware
# ============================================================================

class FlaskValidationMiddleware:
    """Validation middleware for Flask applications.
    
    Usage:
        from flask import Flask
        from validation_infrastructure.api import FlaskValidationMiddleware
        
        app = Flask(__name__)
        validation = FlaskValidationMiddleware(app)
        
        @app.route("/users", methods=["POST"])
        @validation.validate_json(UserCreateSchema)
        def create_user(validated_data):
            return {"id": 1, **validated_data}
    """
    
    def __init__(self, app: Any = None) -> None:
        """Initialize middleware.
        
        Args:
            app: Flask application instance (optional).
        """
        self.app = app
        if app is not None:
            self.init_app(app)
    
    def init_app(self, app: Any) -> None:
        """Initialize with Flask app (factory pattern).
        
        Args:
            app: Flask application instance.
        """
        self.app = app
        
        # Register error handler
        @app.errorhandler(ValidationError)
        def handle_validation_error(error: ValidationError) -> tuple[dict, int]:
            return APIValidationError(
                message=str(error),
                errors=[{"message": str(error)}],
            ).to_dict(), 400
    
    def validate_json(
        self,
        schema: Type[BaseModel],
    ) -> Callable[[Callable[..., T]], Callable[..., T]]:
        """Decorator to validate JSON request body.
        
        Args:
            schema: Pydantic model for validation.
        
        Returns:
            Decorator function.
        """
        def decorator(f: Callable[..., T]) -> Callable[..., T]:
            @functools.wraps(f)
            def wrapper(*args: Any, **kwargs: Any) -> T:
                try:
                    from flask import request, jsonify
                except ImportError:
                    raise ImportError("Flask is required for FlaskValidationMiddleware")
                
                data = request.get_json(force=True, silent=True)
                if data is None:
                    return jsonify(APIValidationError(
                        message="Invalid or missing JSON body",
                    ).to_dict()), 400
                
                try:
                    validated = schema.model_validate(data)
                    return f(validated.model_dump(), *args, **kwargs)
                except PydanticValidationError as e:
                    errors = [
                        {
                            "field": ".".join(str(loc) for loc in err["loc"]),
                            "message": err["msg"],
                            "type": err["type"],
                        }
                        for err in e.errors()
                    ]
                    return jsonify(APIValidationError(
                        message="Validation failed",
                        errors=errors,
                    ).to_dict()), 400
            
            return wrapper
        return decorator
    
    def validate_query(
        self,
        schema: Type[BaseModel],
    ) -> Callable[[Callable[..., T]], Callable[..., T]]:
        """Decorator to validate query parameters.
        
        Args:
            schema: Pydantic model for validation.
        
        Returns:
            Decorator function.
        """
        def decorator(f: Callable[..., T]) -> Callable[..., T]:
            @functools.wraps(f)
            def wrapper(*args: Any, **kwargs: Any) -> T:
                try:
                    from flask import request, jsonify
                except ImportError:
                    raise ImportError("Flask is required for FlaskValidationMiddleware")
                
                try:
                    validated = schema.model_validate(dict(request.args))
                    return f(validated.model_dump(), *args, **kwargs)
                except PydanticValidationError as e:
                    errors = [
                        {
                            "field": ".".join(str(loc) for loc in err["loc"]),
                            "message": err["msg"],
                        }
                        for err in e.errors()
                    ]
                    return jsonify(APIValidationError(
                        message="Query parameter validation failed",
                        errors=errors,
                    ).to_dict()), 400
            
            return wrapper
        return decorator


# ============================================================================
# FastAPI Middleware
# ============================================================================

class FastAPIValidationMiddleware:
    """Validation middleware for FastAPI applications.
    
    Provides additional validation beyond FastAPI's built-in Pydantic support.
    
    Usage:
        from fastapi import FastAPI
        from validation_infrastructure.api import FastAPIValidationMiddleware
        
        app = FastAPI()
        validation = FastAPIValidationMiddleware(app)
        
        @app.post("/users")
        @validation.validate_response(UserResponseSchema)
        async def create_user(user: UserCreateSchema):
            return {"id": 1, **user.model_dump()}
    """
    
    def __init__(self, app: Any = None) -> None:
        """Initialize middleware.
        
        Args:
            app: FastAPI application instance (optional).
        """
        self.app = app
        if app is not None:
            self.init_app(app)
    
    def init_app(self, app: Any) -> None:
        """Initialize with FastAPI app.
        
        Args:
            app: FastAPI application instance.
        """
        self.app = app
        
        # Add exception handler
        try:
            from fastapi import Request
            from fastapi.responses import JSONResponse
            
            @app.exception_handler(ValidationError)
            async def validation_exception_handler(
                request: Request,
                exc: ValidationError,
            ) -> JSONResponse:
                return JSONResponse(
                    status_code=400,
                    content=APIValidationError(
                        message=str(exc),
                    ).to_dict(),
                )
        except ImportError:
            pass  # FastAPI not installed
    
    def validate_response(
        self,
        schema: Type[BaseModel],
    ) -> Callable[[Callable[..., T]], Callable[..., T]]:
        """Decorator to validate response data.
        
        Useful for validating data before sending to client.
        
        Args:
            schema: Pydantic model for validation.
        
        Returns:
            Decorator function.
        """
        def decorator(f: Callable[..., T]) -> Callable[..., T]:
            @functools.wraps(f)
            async def async_wrapper(*args: Any, **kwargs: Any) -> T:
                result = await f(*args, **kwargs)
                
                try:
                    if isinstance(result, dict):
                        schema.model_validate(result)
                    elif isinstance(result, list):
                        for item in result:
                            schema.model_validate(item)
                except PydanticValidationError as e:
                    raise ValidationError(
                        f"Response validation failed: {e.error_count()} errors"
                    )
                
                return result
            
            @functools.wraps(f)
            def sync_wrapper(*args: Any, **kwargs: Any) -> T:
                result = f(*args, **kwargs)
                
                try:
                    if isinstance(result, dict):
                        schema.model_validate(result)
                    elif isinstance(result, list):
                        for item in result:
                            schema.model_validate(item)
                except PydanticValidationError as e:
                    raise ValidationError(
                        f"Response validation failed: {e.error_count()} errors"
                    )
                
                return result
            
            import asyncio
            if asyncio.iscoroutinefunction(f):
                return async_wrapper
            return sync_wrapper
        
        return decorator
    
    def validate_custom(
        self,
        validator: Callable[[Any], ValidationResult],
    ) -> Callable[[Callable[..., T]], Callable[..., T]]:
        """Decorator for custom validation logic.
        
        Args:
            validator: Custom validation function.
        
        Returns:
            Decorator function.
        """
        def decorator(f: Callable[..., T]) -> Callable[..., T]:
            @functools.wraps(f)
            async def async_wrapper(*args: Any, **kwargs: Any) -> T:
                # Extract body from kwargs or first positional arg
                data = kwargs.get("body") or (args[0] if args else None)
                
                result = validator(data)
                if not result.is_valid:
                    raise ValidationError(
                        f"Custom validation failed: {result.errors}"
                    )
                
                return await f(*args, **kwargs)
            
            @functools.wraps(f)
            def sync_wrapper(*args: Any, **kwargs: Any) -> T:
                data = kwargs.get("body") or (args[0] if args else None)
                
                result = validator(data)
                if not result.is_valid:
                    raise ValidationError(
                        f"Custom validation failed: {result.errors}"
                    )
                
                return f(*args, **kwargs)
            
            import asyncio
            if asyncio.iscoroutinefunction(f):
                return async_wrapper
            return sync_wrapper
        
        return decorator


# ============================================================================
# Generic ASGI/WSGI Middleware
# ============================================================================

class ValidationMiddleware:
    """Generic validation middleware for ASGI applications.
    
    Can be used as ASGI middleware for any framework.
    
    Usage:
        app = ValidationMiddleware(app, schemas={
            "/api/users": {"POST": UserCreateSchema},
        })
    """
    
    def __init__(
        self,
        app: Any,
        schemas: dict[str, dict[str, Type[BaseModel]]] | None = None,
    ) -> None:
        """Initialize middleware.
        
        Args:
            app: ASGI application.
            schemas: Mapping of path -> method -> schema.
        """
        self.app = app
        self.schemas = schemas or {}
    
    async def __call__(self, scope: dict, receive: Callable, send: Callable) -> None:
        """ASGI interface."""
        if scope["type"] != "http":
            await self.app(scope, receive, send)
            return
        
        path = scope.get("path", "")
        method = scope.get("method", "GET")
        
        # Check if we have a schema for this endpoint
        if path in self.schemas and method in self.schemas[path]:
            # Wrap receive to capture and validate body
            body_parts: list[bytes] = []
            
            async def receive_wrapper() -> dict:
                message = await receive()
                if message["type"] == "http.request":
                    body_parts.append(message.get("body", b""))
                return message
            
            # Let the app process (this triggers receive)
            await self.app(scope, receive_wrapper, send)
        else:
            await self.app(scope, receive, send)
