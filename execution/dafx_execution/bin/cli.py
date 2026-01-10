"""DAFX CLI - Command Line Interface for DAFX execution scripts.

Provides commands for:
- Processing audio files with DSP effects
- Running validation tests
- Environment health checks
- Configuration inspection
"""

from __future__ import annotations

import sys
from pathlib import Path
from typing import Any

import click
from rich.console import Console
from rich.panel import Panel
from rich.table import Table
from rich.tree import Tree

from dafx_execution.config import get_settings, AppSettings
from dafx_execution.core import (
    configure_logging,
    get_logger,
    ExecutionEngine,
    ExecutionError,
)


console = Console()


def print_error(message: str, exit_code: int = 1) -> None:
    """Print error message and exit."""
    console.print(f"[bold red]Error:[/bold red] {message}")
    sys.exit(exit_code)


def print_success(message: str) -> None:
    """Print success message."""
    console.print(f"[bold green]✓[/bold green] {message}")


@click.group()
@click.option("--verbose", "-v", is_flag=True, help="Enable verbose output")
@click.option("--debug", is_flag=True, help="Enable debug mode")
@click.option("--dry-run", is_flag=True, help="Simulate without executing")
@click.pass_context
def main(ctx: click.Context, verbose: bool, debug: bool, dry_run: bool) -> None:
    """DAFX Execution CLI - DSP algorithm validation and processing.
    
    Use 'dafx-cli COMMAND --help' for command-specific help.
    """
    # Ensure context object exists
    ctx.ensure_object(dict)
    
    # Configure settings
    settings = get_settings()
    
    # Determine log level
    log_level = "DEBUG" if debug else ("INFO" if verbose else settings.logging.level)
    
    # Configure logging
    configure_logging(
        level=log_level,
        log_dir=settings.logging.log_dir,
        console_output=True,
        json_output=settings.logging.json_output,
    )
    
    # Store in context for subcommands
    ctx.obj["settings"] = settings
    ctx.obj["dry_run"] = dry_run
    ctx.obj["verbose"] = verbose
    ctx.obj["debug"] = debug
    
    logger = get_logger(__name__)
    logger.debug("CLI initialized", command=ctx.invoked_subcommand)


@main.command()
@click.pass_context
def check(ctx: click.Context) -> None:
    """Check environment and dependencies.
    
    Verifies:
    - Python version compatibility
    - Required directories exist
    - Dependencies are installed
    - Configuration is valid
    """
    settings: AppSettings = ctx.obj["settings"]
    issues: list[str] = []
    
    console.print(Panel.fit(
        "[bold]DAFX Environment Check[/bold]",
        border_style="blue",
    ))
    
    # Create results table
    table = Table(show_header=True, header_style="bold cyan")
    table.add_column("Check", style="dim")
    table.add_column("Status")
    table.add_column("Details")
    
    # Python version
    py_version = sys.version_info
    py_ok = py_version >= (3, 10)
    table.add_row(
        "Python Version",
        "[green]✓[/green]" if py_ok else "[red]✗[/red]",
        f"{py_version.major}.{py_version.minor}.{py_version.micro}",
    )
    if not py_ok:
        issues.append("Python 3.10+ required")
    
    # Directories
    dirs_to_check = [
        ("Input Directory", settings.paths.input_dir),
        ("Output Directory", settings.paths.output_dir),
        ("Temp Directory", settings.paths.temp_dir),
        ("Log Directory", settings.logging.log_dir),
    ]
    
    for name, path in dirs_to_check:
        exists = path.exists()
        table.add_row(
            name,
            "[green]✓[/green]" if exists else "[yellow]○[/yellow]",
            str(path),
        )
    
    # Dependencies
    optional_deps = [
        ("numpy", "Numerical processing"),
        ("scipy", "Scientific computing"),
        ("soundfile", "Audio file I/O"),
        ("structlog", "Structured logging"),
    ]
    
    for module, desc in optional_deps:
        try:
            __import__(module)
            table.add_row(module, "[green]✓[/green]", desc)
        except ImportError:
            table.add_row(module, "[red]✗[/red]", f"{desc} (not installed)")
            issues.append(f"Missing dependency: {module}")
    
    console.print(table)
    console.print()
    
    if issues:
        console.print("[yellow]Issues found:[/yellow]")
        for issue in issues:
            console.print(f"  [yellow]•[/yellow] {issue}")
        sys.exit(1)
    else:
        print_success("All checks passed!")


@main.command()
@click.pass_context
def config(ctx: click.Context) -> None:
    """Display current configuration.
    
    Shows all configuration values including defaults,
    environment overrides, and computed values.
    """
    settings: AppSettings = ctx.obj["settings"]
    
    console.print(Panel.fit(
        "[bold]DAFX Configuration[/bold]",
        border_style="blue",
    ))
    
    # Build configuration tree
    tree = Tree(f"[bold]{settings.app_name}[/bold] v{settings.version}")
    
    # Logging
    log_branch = tree.add("[cyan]Logging[/cyan]")
    log_branch.add(f"Level: {settings.logging.level}")
    log_branch.add(f"JSON Output: {settings.logging.json_output}")
    log_branch.add(f"Console Output: {settings.logging.console_output}")
    log_branch.add(f"Log Dir: {settings.logging.log_dir}")
    
    # DSP
    dsp_branch = tree.add("[cyan]DSP[/cyan]")
    dsp_branch.add(f"Sample Rate: {settings.dsp.sample_rate} Hz")
    dsp_branch.add(f"Block Size: {settings.dsp.block_size}")
    dsp_branch.add(f"Channels: {settings.dsp.channels}")
    dsp_branch.add(f"Bit Depth: {settings.dsp.bit_depth}")
    
    # Paths
    path_branch = tree.add("[cyan]Paths[/cyan]")
    path_branch.add(f"Input: {settings.paths.input_dir}")
    path_branch.add(f"Output: {settings.paths.output_dir}")
    path_branch.add(f"Temp: {settings.paths.temp_dir}")
    path_branch.add(f"Cache: {settings.paths.cache_dir}")
    
    # Execution
    exec_branch = tree.add("[cyan]Execution[/cyan]")
    exec_branch.add(f"Dry Run: {settings.execution.dry_run}")
    exec_branch.add(f"Max Workers: {settings.execution.max_workers}")
    exec_branch.add(f"Timeout: {settings.execution.timeout_seconds}s")
    exec_branch.add(f"Retry Count: {settings.execution.retry_count}")
    
    console.print(tree)


@main.command()
@click.argument("input_file", type=click.Path(exists=True, path_type=Path))
@click.option("--output", "-o", type=click.Path(path_type=Path), help="Output file path")
@click.option("--effect", "-e", multiple=True, help="Effect to apply (can specify multiple)")
@click.pass_context
def process(
    ctx: click.Context,
    input_file: Path,
    output: Path | None,
    effect: tuple[str, ...],
) -> None:
    """Process an audio file with DSP effects.
    
    INPUT_FILE is the path to the audio file to process.
    
    Example:
        dafx-cli process audio.wav -e tube -e reverb -o output.wav
    """
    settings: AppSettings = ctx.obj["settings"]
    dry_run: bool = ctx.obj["dry_run"]
    
    logger = get_logger(__name__)
    
    # Validate input
    if not input_file.exists():
        print_error(f"Input file not found: {input_file}", exit_code=3)
    
    # Default output path
    if output is None:
        output = settings.paths.output_dir / f"processed_{input_file.name}"
    
    console.print(f"[bold]Processing:[/bold] {input_file}")
    console.print(f"[bold]Output:[/bold] {output}")
    console.print(f"[bold]Effects:[/bold] {', '.join(effect) if effect else 'none'}")
    
    if dry_run:
        console.print("\n[yellow]Dry-run mode: no changes made[/yellow]")
        return
    
    # Create execution engine
    engine = ExecutionEngine(dry_run=dry_run)
    
    # TODO: Implement actual processing with DSP module
    console.print("\n[yellow]Processing not yet implemented[/yellow]")
    console.print("This will use the DSP module once completed.")


@main.command()
@click.option("--format", "-f", type=click.Choice(["text", "json"]), default="text")
@click.pass_context
def stats(ctx: click.Context, format: str) -> None:
    """Show execution statistics.
    
    Displays metrics from recent command executions including
    success rates, timing, and resource usage.
    """
    console.print(Panel.fit(
        "[bold]Execution Statistics[/bold]",
        border_style="blue",
    ))
    
    # TODO: Load stats from persistent storage
    table = Table(show_header=True, header_style="bold cyan")
    table.add_column("Metric")
    table.add_column("Value", justify="right")
    
    table.add_row("Commands Run", "0")
    table.add_row("Success Rate", "N/A")
    table.add_row("Total Duration", "0.00s")
    table.add_row("Avg Duration", "N/A")
    
    console.print(table)
    console.print("\n[dim]No execution history available yet.[/dim]")


@main.command()
def version() -> None:
    """Show version information."""
    settings = get_settings()
    console.print(f"[bold]{settings.app_name}[/bold] v{settings.version}")
    console.print(f"Python {sys.version}")


if __name__ == "__main__":
    main()
