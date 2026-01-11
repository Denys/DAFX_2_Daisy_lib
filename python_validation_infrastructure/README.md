# Python Validation Infrastructure

A comprehensive validation library for Python applications, providing type checking, schema validation, sanitization, and more.

## Installation

```bash
cd python_validation_infrastructure
pip install -e .

# With all optional dependencies
pip install -e ".[dev,all]"
```

## Quick Start

### Type Validation

```python
from validation_infrastructure.core import TypeValidator, StringValidator

# Basic type checking
validator = TypeValidator(expected_type=int)
result = validator.validate(42)
print(result.is_valid)  # True

# String validation with constraints
string_validator = StringValidator(min_length=3, max_length=50)
result = string_validator.validate("Hello")
print(result.is_valid)  # True
```

### Schema Validation (Pydantic)

```python
from pydantic import BaseModel
from validation_infrastructure.schemas import PydanticValidator

class UserSchema(BaseModel):
    name: str
    email: str
    age: int

validator = PydanticValidator(UserSchema)
result = validator.validate({
    "name": "John",
    "email": "john@example.com",
    "age": 30
})
```

### Decorator-Based Validation

```python
from validation_infrastructure.decorators import validate_params, requires

@validate_params(name=str, age=int)
def create_user(name: str, age: int):
    return {"name": name, "age": age}

# Design by Contract
@requires(lambda x: x > 0, "Value must be positive")
def process_positive(x: int) -> int:
    return x * 2
```

### Sanitization Pipelines

```python
from validation_infrastructure.sanitization import (
    StringSanitizer,
    SanitizationPipeline,
)

# Chain sanitizers
pipeline = SanitizationPipeline([
    StringSanitizer(trim=True, lowercase=True),
])
clean_input = pipeline.apply("  HELLO WORLD  ")
print(clean_input)  # "hello world"
```

### Async Validation

```python
from validation_infrastructure.async_validation import ParallelValidator

validator = ParallelValidator(validators=[
    email_validator,
    username_validator,
    password_validator,
])
result = await validator.validate(user_data)
```

### API Middleware (Flask)

```python
from flask import Flask
from validation_infrastructure.api import FlaskValidationMiddleware

app = Flask(__name__)
validation = FlaskValidationMiddleware(app)

@app.route("/users", methods=["POST"])
@validation.validate_json(UserCreateSchema)
def create_user(validated_data):
    return {"id": 1, **validated_data}
```

### API Middleware (FastAPI)

```python
from fastapi import FastAPI
from validation_infrastructure.api import FastAPIValidationMiddleware

app = FastAPI()
validation = FastAPIValidationMiddleware(app)

@app.post("/users")
@validation.validate_response(UserResponseSchema)
async def create_user(user: UserCreateSchema):
    return {"id": 1, **user.model_dump()}
```

### Database Validation (SQLAlchemy)

```python
from validation_infrastructure.database import SQLAlchemyValidator

validator = SQLAlchemyValidator(User)
validator.add_validator("email", lambda e: "@" in e)

result = validator.validate_model(user_instance)
result = validator.validate_unique(session, user_instance, "email")
```

### CLI Tools

```bash
# Validate a JSON file against schema
validate-cli validate-file data.json --schema schema.json

# Check config file
validate-cli check-config config.yaml

# Benchmark validators
validate-cli benchmark --iterations 1000
```

### Benchmarking & Profiling

```python
from validation_infrastructure.benchmarking import (
    ValidationProfiler,
    BenchmarkReport,
    ConsoleReportGenerator,
)

# Profile validation
profiler = ValidationProfiler()
with profiler.profile() as p:
    result = validator.validate(data)
print(p.result.summary())

# Generate reports
report = BenchmarkReport(title="Validation Benchmarks")
generator = ConsoleReportGenerator()
print(generator.generate(report))
```

### Testing (Pytest Integration)

```python
# conftest.py
pytest_plugins = ["validation_infrastructure.testing.pytest_integration"]

# test_validators.py
from validation_infrastructure.testing import ValidationTestCase, validation_test_cases

@validation_test_cases([
    ValidationTestCase("valid_email", "test@example.com", True),
    ValidationTestCase("invalid_email", "invalid", False),
])
def test_email(case, validation_assertions):
    result = email_validator.validate(case.input_data)
    if case.expected_valid:
        validation_assertions.assert_valid(result)
    else:
        validation_assertions.assert_invalid(result)
```

## Components

| Module | Description |
|--------|-------------|
| `core` | Base validators, type checking |
| `schemas` | Pydantic/Marshmallow integration |
| `decorators` | `@validate_params`, `@requires`, `@ensures` |
| `sanitization` | Input sanitization pipelines |
| `async_validation` | Parallel and batch validation |
| `errors` | Exception hierarchy, localization |
| `config` | YAML/JSON/TOML validators, env vars |
| `files` | File upload validation |
| `api` | Flask/FastAPI middleware |
| `database` | SQLAlchemy/Django validators |
| `plugins` | Extensible plugin system |
| `logging` | Audit logging |
| `benchmarking` | Profiling and reports |
| `testing` | Pytest fixtures and helpers |

## Configuration

Environment variables:
- `VALIDATION_STRICT_MODE`: Enable strict validation (default: false)
- `VALIDATION_LOG_LEVEL`: Logging level (default: INFO)
- `VALIDATION_LOCALE`: Error message locale (default: en)

## Development

```bash
# Install dev dependencies
pip install -e ".[dev]"

# Run tests
pytest

# Type checking
mypy src

# Format code
black src
ruff check src
```


## License

MIT License
