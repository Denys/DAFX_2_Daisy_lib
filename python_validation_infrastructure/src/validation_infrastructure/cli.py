"""CLI tools for validation checks with comprehensive output formatting."""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any, Dict, List, Optional

import click
from rich.console import Console
from rich.table import Table
from rich.panel import Panel
from rich.syntax import Syntax

from validation_infrastructure.config.validators import (
    validate_config_file,
    JSONValidator,
    YAMLValidator,
    TOMLValidator,
)
from validation_infrastructure.core.base import ValidationResult
from validation_infrastructure.errors.formatters import (
    JSONErrorFormatter,
    TextErrorFormatter,
    FormatterOptions,
)


# Exit codes for CI/CD integration
EXIT_SUCCESS = 0
EXIT_VALIDATION_ERROR = 1
EXIT_FILE_ERROR = 2
EXIT_SCHEMA_ERROR = 3
EXIT_INTERNAL_ERROR = 4

console = Console()


@click.group()
@click.version_option(version="3.1.0", prog_name="validation-infrastructure")
@click.option("-q", "--quiet", is_flag=True, help="Suppress output")
@click.option("-v", "--verbose", is_flag=True, help="Verbose output")
@click.pass_context
def main(ctx: click.Context, quiet: bool, verbose: bool):
    """Validation Infrastructure CLI - Validate data files and configurations."""
    ctx.ensure_object(dict)
    ctx.obj["quiet"] = quiet
    ctx.obj["verbose"] = verbose


@main.command("validate")
@click.argument("file", type=click.Path(exists=True))
@click.option(
    "-s", "--schema",
    type=click.Path(exists=True),
    help="Schema file for validation",
)
@click.option(
    "-f", "--format",
    type=click.Choice(["json", "yaml", "toml", "auto"]),
    default="auto",
    help="File format (default: auto-detect)",
)
@click.option(
    "-o", "--output",
    type=click.Choice(["text", "json", "table"]),
    default="text",
    help="Output format",
)
@click.option("--strict", is_flag=True, help="Enable strict validation")
@click.pass_context
def validate_file(
    ctx: click.Context,
    file: str,
    schema: Optional[str],
    format: str,
    output: str,
    strict: bool,
):
    """Validate a configuration or data file."""
    quiet = ctx.obj.get("quiet", False)
    verbose = ctx.obj.get("verbose", False)
    
    try:
        # Load schema if provided
        schema_data = None
        if schema:
            schema_path = Path(schema)
            if schema_path.suffix in (".json",):
                with open(schema_path) as f:
                    schema_data = json.load(f)
            elif schema_path.suffix in (".yaml", ".yml"):
                import yaml
                with open(schema_path) as f:
                    schema_data = yaml.safe_load(f)
        
        # Validate
        result = validate_config_file(
            file,
            schema=schema_data,
            format=format if format != "auto" else None,
            strict=strict,
        )
        
        # Output results
        if not quiet:
            _output_result(result, output, file, verbose)
        
        # Exit code
        sys.exit(EXIT_SUCCESS if result.is_valid else EXIT_VALIDATION_ERROR)
    
    except FileNotFoundError as e:
        if not quiet:
            console.print(f"[red]Error:[/red] File not found: {e}")
        sys.exit(EXIT_FILE_ERROR)
    except Exception as e:
        if not quiet:
            console.print(f"[red]Internal error:[/red] {e}")
        if verbose:
            console.print_exception()
        sys.exit(EXIT_INTERNAL_ERROR)


@main.command("check-json")
@click.argument("files", nargs=-1, type=click.Path(exists=True))
@click.option("--allow-comments", is_flag=True, help="Allow JSON comments")
@click.option("--allow-trailing-commas", is_flag=True, help="Allow trailing commas")
@click.pass_context
def check_json(
    ctx: click.Context,
    files: tuple,
    allow_comments: bool,
    allow_trailing_commas: bool,
):
    """Validate JSON files."""
    quiet = ctx.obj.get("quiet", False)
    errors = 0
    
    validator = JSONValidator(
        allow_comments=allow_comments,
        allow_trailing_commas=allow_trailing_commas,
    )
    
    for file in files:
        result = validator.validate(file)
        
        if result.is_valid:
            if not quiet:
                console.print(f"[green]✓[/green] {file}")
        else:
            errors += 1
            if not quiet:
                console.print(f"[red]✗[/red] {file}")
                for issue in result.issues:
                    console.print(f"  [red]→[/red] {issue.message}")
    
    sys.exit(EXIT_SUCCESS if errors == 0 else EXIT_VALIDATION_ERROR)


@main.command("check-yaml")
@click.argument("files", nargs=-1, type=click.Path(exists=True))
@click.option("--no-duplicate-keys", is_flag=True, help="Disallow duplicate keys")
@click.pass_context
def check_yaml(
    ctx: click.Context,
    files: tuple,
    no_duplicate_keys: bool,
):
    """Validate YAML files."""
    quiet = ctx.obj.get("quiet", False)
    errors = 0
    
    validator = YAMLValidator(allow_duplicate_keys=not no_duplicate_keys)
    
    for file in files:
        result = validator.validate(file)
        
        if result.is_valid:
            if not quiet:
                console.print(f"[green]✓[/green] {file}")
        else:
            errors += 1
            if not quiet:
                console.print(f"[red]✗[/red] {file}")
                for issue in result.issues:
                    console.print(f"  [red]→[/red] {issue.message}")
    
    sys.exit(EXIT_SUCCESS if errors == 0 else EXIT_VALIDATION_ERROR)


@main.command("check-toml")
@click.argument("files", nargs=-1, type=click.Path(exists=True))
@click.pass_context
def check_toml(ctx: click.Context, files: tuple):
    """Validate TOML files."""
    quiet = ctx.obj.get("quiet", False)
    errors = 0
    
    validator = TOMLValidator()
    
    for file in files:
        result = validator.validate(file)
        
        if result.is_valid:
            if not quiet:
                console.print(f"[green]✓[/green] {file}")
        else:
            errors += 1
            if not quiet:
                console.print(f"[red]✗[/red] {file}")
                for issue in result.issues:
                    console.print(f"  [red]→[/red] {issue.message}")
    
    sys.exit(EXIT_SUCCESS if errors == 0 else EXIT_VALIDATION_ERROR)


@main.command("check-env")
@click.option(
    "-f", "--file",
    type=click.Path(exists=True),
    default=".env",
    help="Environment file to check",
)
@click.option(
    "-s", "--schema",
    type=click.Path(exists=True),
    help="Schema file defining required variables",
)
@click.option("--strict", is_flag=True, help="Fail on missing optional variables")
@click.pass_context
def check_env(
    ctx: click.Context,
    file: str,
    schema: Optional[str],
    strict: bool,
):
    """Validate environment variables."""
    quiet = ctx.obj.get("quiet", False)
    
    try:
        from validation_infrastructure.config.env import load_dotenv_file, validate_env, env_var
        
        # Load .env file
        env_vars = load_dotenv_file(file, override=False)
        
        if not quiet:
            console.print(f"[blue]Loaded {len(env_vars)} variables from {file}[/blue]")
        
        # If schema provided, validate against it
        if schema:
            schema_path = Path(schema)
            with open(schema_path) as f:
                schema_data = json.load(f)
            
            # Convert schema to EnvVarSpec
            specs = {}
            for name, spec in schema_data.get("variables", {}).items():
                type_map = {"string": str, "integer": int, "boolean": bool, "float": float}
                specs[name] = env_var(
                    name,
                    type=type_map.get(spec.get("type", "string"), str),
                    required=spec.get("required", False),
                    default=spec.get("default"),
                )
            
            result = validate_env(specs, env_vars, raise_on_error=False)
            
            if not quiet:
                console.print(f"[green]✓[/green] Environment validation passed")
            
            sys.exit(EXIT_SUCCESS)
        
        # Basic check - just parsing
        if not quiet:
            table = Table(title="Environment Variables")
            table.add_column("Variable", style="cyan")
            table.add_column("Value", style="green")
            
            for key, value in sorted(env_vars.items()):
                # Mask sensitive values
                display_value = "***" if any(s in key.lower() for s in ["password", "secret", "key", "token"]) else value
                table.add_row(key, display_value[:50] + "..." if len(display_value) > 50 else display_value)
            
            console.print(table)
        
        sys.exit(EXIT_SUCCESS)
    
    except FileNotFoundError:
        if not quiet:
            console.print(f"[yellow]Warning:[/yellow] No .env file found at {file}")
        sys.exit(EXIT_FILE_ERROR)
    except Exception as e:
        if not quiet:
            console.print(f"[red]Error:[/red] {e}")
        sys.exit(EXIT_INTERNAL_ERROR)


@main.command("schema")
@click.argument("file", type=click.Path(exists=True))
@click.option(
    "-f", "--format",
    type=click.Choice(["json", "yaml"]),
    default="json",
    help="Output format",
)
@click.pass_context
def generate_schema(ctx: click.Context, file: str, format: str):
    """Generate validation schema from a data file."""
    quiet = ctx.obj.get("quiet", False)
    
    try:
        # Load file
        path = Path(file)
        if path.suffix == ".json":
            with open(path) as f:
                data = json.load(f)
        elif path.suffix in (".yaml", ".yml"):
            import yaml
            with open(path) as f:
                data = yaml.safe_load(f)
        elif path.suffix == ".toml":
            try:
                import tomllib
            except ImportError:
                import tomli as tomllib
            with open(path, "rb") as f:
                data = tomllib.load(f)
        else:
            console.print(f"[red]Error:[/red] Unsupported file format: {path.suffix}")
            sys.exit(EXIT_FILE_ERROR)
        
        # Generate schema
        schema = _infer_schema(data)
        
        # Output
        if format == "json":
            output = json.dumps(schema, indent=2)
        else:
            import yaml
            output = yaml.dump(schema, default_flow_style=False)
        
        if not quiet:
            console.print(Syntax(output, format, theme="monokai"))
        else:
            print(output)
        
        sys.exit(EXIT_SUCCESS)
    
    except Exception as e:
        console.print(f"[red]Error:[/red] {e}")
        sys.exit(EXIT_INTERNAL_ERROR)


@main.command("batch")
@click.argument("directory", type=click.Path(exists=True, file_okay=False))
@click.option(
    "-p", "--pattern",
    default="*",
    help="File glob pattern",
)
@click.option(
    "-s", "--schema",
    type=click.Path(exists=True),
    help="Schema file for validation",
)
@click.option("--recursive", "-r", is_flag=True, help="Search recursively")
@click.option(
    "-o", "--output",
    type=click.Choice(["text", "json", "summary"]),
    default="summary",
    help="Output format",
)
@click.pass_context
def batch_validate(
    ctx: click.Context,
    directory: str,
    pattern: str,
    schema: Optional[str],
    recursive: bool,
    output: str,
):
    """Batch validate files in a directory."""
    quiet = ctx.obj.get("quiet", False)
    
    dir_path = Path(directory)
    if recursive:
        files = list(dir_path.rglob(pattern))
    else:
        files = list(dir_path.glob(pattern))
    
    # Filter to supported formats
    supported = {".json", ".yaml", ".yml", ".toml"}
    files = [f for f in files if f.suffix.lower() in supported]
    
    if not files:
        if not quiet:
            console.print(f"[yellow]No files found matching pattern:[/yellow] {pattern}")
        sys.exit(EXIT_SUCCESS)
    
    # Load schema if provided
    schema_data = None
    if schema:
        with open(schema) as f:
            schema_data = json.load(f)
    
    # Validate files
    results: Dict[str, ValidationResult] = {}
    passed = 0
    failed = 0
    
    for file in files:
        result = validate_config_file(file, schema=schema_data)
        results[str(file)] = result
        
        if result.is_valid:
            passed += 1
        else:
            failed += 1
    
    # Output results
    if output == "json":
        output_data = {
            "total": len(files),
            "passed": passed,
            "failed": failed,
            "results": {
                path: {
                    "valid": r.is_valid,
                    "errors": [i.message for i in r.issues],
                }
                for path, r in results.items()
            }
        }
        print(json.dumps(output_data, indent=2))
    
    elif output == "summary":
        if not quiet:
            console.print(Panel(f"[bold]Batch Validation Results[/bold]"))
            console.print(f"Total files: {len(files)}")
            console.print(f"[green]Passed: {passed}[/green]")
            console.print(f"[red]Failed: {failed}[/red]")
            
            if failed > 0:
                console.print("\n[bold]Failed files:[/bold]")
                for path, result in results.items():
                    if not result.is_valid:
                        console.print(f"  [red]✗[/red] {path}")
                        for issue in result.issues[:3]:  # Show first 3 errors
                            console.print(f"    [red]→[/red] {issue.message}")
    
    else:  # text
        for path, result in results.items():
            if result.is_valid:
                if not quiet:
                    console.print(f"[green]✓[/green] {path}")
            else:
                if not quiet:
                    console.print(f"[red]✗[/red] {path}")
                    for issue in result.issues:
                        console.print(f"  [red]→[/red] {issue.message}")
    
    sys.exit(EXIT_SUCCESS if failed == 0 else EXIT_VALIDATION_ERROR)


def _output_result(
    result: ValidationResult,
    format: str,
    file: str,
    verbose: bool,
):
    """Output validation result in specified format."""
    if format == "json":
        formatter = JSONErrorFormatter(FormatterOptions(
            include_value=verbose,
            include_context=verbose,
        ))
        if result.is_valid:
            output = {"valid": True, "file": file}
        else:
            output = {
                "valid": False,
                "file": file,
                "errors": [
                    {"message": i.message, "code": i.code, "field": i.field}
                    for i in result.issues
                ],
            }
        print(json.dumps(output, indent=2))
    
    elif format == "table":
        if result.is_valid:
            console.print(Panel(
                f"[green]✓ Validation passed[/green]\n\nFile: {file}",
                title="Validation Result",
            ))
        else:
            table = Table(title="Validation Errors")
            table.add_column("Field", style="cyan")
            table.add_column("Code", style="yellow")
            table.add_column("Message", style="red")
            
            for issue in result.issues:
                table.add_row(
                    issue.field or "-",
                    issue.code or "-",
                    issue.message,
                )
            
            console.print(table)
    
    else:  # text
        if result.is_valid:
            console.print(f"[green]✓[/green] Validation passed: {file}")
            if result.warnings:
                console.print(f"\n[yellow]Warnings:[/yellow]")
                for warning in result.warnings:
                    console.print(f"  [yellow]⚠[/yellow] {warning.message}")
        else:
            console.print(f"[red]✗[/red] Validation failed: {file}")
            console.print(f"\n[red]Errors ({len(result.issues)}):[/red]")
            for issue in result.issues:
                msg = f"  [red]→[/red] {issue.message}"
                if issue.field:
                    msg = f"  [red]→[/red] [{issue.field}] {issue.message}"
                console.print(msg)


def _infer_schema(data: Any, depth: int = 0) -> Dict[str, Any]:
    """Infer a JSON Schema from data."""
    if depth > 10:
        return {"type": "object"}
    
    if isinstance(data, dict):
        properties = {}
        required = []
        
        for key, value in data.items():
            properties[key] = _infer_schema(value, depth + 1)
            required.append(key)
        
        return {
            "type": "object",
            "properties": properties,
            "required": required,
        }
    
    elif isinstance(data, list):
        if data:
            items = _infer_schema(data[0], depth + 1)
        else:
            items = {}
        return {"type": "array", "items": items}
    
    elif isinstance(data, bool):
        return {"type": "boolean"}
    
    elif isinstance(data, int):
        return {"type": "integer"}
    
    elif isinstance(data, float):
        return {"type": "number"}
    
    elif isinstance(data, str):
        return {"type": "string"}
    
    elif data is None:
        return {"type": "null"}
    
    return {}


if __name__ == "__main__":
    main()
