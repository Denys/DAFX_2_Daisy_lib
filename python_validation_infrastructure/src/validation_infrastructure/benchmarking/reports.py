"""Benchmark report generation.

Provides report generators for benchmark results in multiple
formats: Console (Rich), JSON, HTML, and Markdown.
"""

from __future__ import annotations

import json
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path
from typing import Any

from validation_infrastructure.benchmarking.profiler import ProfileResult


@dataclass
class BenchmarkResult:
    """Single benchmark result.
    
    Attributes:
        name: Benchmark name.
        iterations: Number of iterations.
        min_ms: Minimum time in ms.
        max_ms: Maximum time in ms.
        avg_ms: Average time in ms.
        median_ms: Median time in ms.
        std_dev_ms: Standard deviation in ms.
        throughput: Operations per second.
        memory_kb: Memory usage in KB.
        metadata: Additional metadata.
    """
    name: str
    iterations: int
    min_ms: float
    max_ms: float
    avg_ms: float
    median_ms: float = 0.0
    std_dev_ms: float = 0.0
    throughput: float = 0.0
    memory_kb: float = 0.0
    metadata: dict[str, Any] = field(default_factory=dict)


@dataclass
class BenchmarkReport:
    """Complete benchmark report.
    
    Attributes:
        title: Report title.
        timestamp: When the benchmark was run.
        environment: Environment info (Python version, OS, etc.).
        results: List of benchmark results.
        summary: Summary statistics.
    """
    title: str
    timestamp: datetime
    environment: dict[str, str] = field(default_factory=dict)
    results: list[BenchmarkResult] = field(default_factory=list)
    summary: dict[str, Any] = field(default_factory=dict)
    
    def add_result(self, result: BenchmarkResult) -> None:
        """Add a benchmark result."""
        self.results.append(result)
    
    def compute_summary(self) -> None:
        """Compute summary statistics."""
        if not self.results:
            return
        
        self.summary = {
            "total_benchmarks": len(self.results),
            "total_iterations": sum(r.iterations for r in self.results),
            "fastest_benchmark": min(self.results, key=lambda r: r.avg_ms).name,
            "slowest_benchmark": max(self.results, key=lambda r: r.avg_ms).name,
            "avg_time_ms": sum(r.avg_ms for r in self.results) / len(self.results),
        }


class ReportGenerator(ABC):
    """Base class for report generators."""
    
    @abstractmethod
    def generate(self, report: BenchmarkReport) -> str:
        """Generate report content."""
        ...
    
    def save(self, report: BenchmarkReport, path: Path) -> None:
        """Generate and save report to file."""
        content = self.generate(report)
        path.write_text(content, encoding="utf-8")


class ConsoleReportGenerator(ReportGenerator):
    """Generate console reports using Rich.
    
    Usage:
        generator = ConsoleReportGenerator()
        print(generator.generate(report))
    """
    
    def generate(self, report: BenchmarkReport) -> str:
        """Generate console-formatted report."""
        try:
            from rich.console import Console
            from rich.table import Table
            from rich.panel import Panel
            
            console = Console(record=True, width=100)
            
            # Header
            console.print(Panel.fit(
                f"[bold blue]{report.title}[/bold blue]\n"
                f"[dim]{report.timestamp.strftime('%Y-%m-%d %H:%M:%S')}[/dim]",
            ))
            
            # Environment
            if report.environment:
                console.print("\n[bold]Environment:[/bold]")
                for key, value in report.environment.items():
                    console.print(f"  {key}: {value}")
            
            # Results table
            table = Table(
                title="Benchmark Results",
                show_header=True,
                header_style="bold cyan",
            )
            table.add_column("Benchmark", style="dim")
            table.add_column("Iterations", justify="right")
            table.add_column("Min (ms)", justify="right")
            table.add_column("Avg (ms)", justify="right")
            table.add_column("Max (ms)", justify="right")
            table.add_column("Throughput", justify="right")
            table.add_column("Memory (KB)", justify="right")
            
            for result in report.results:
                table.add_row(
                    result.name,
                    str(result.iterations),
                    f"{result.min_ms:.3f}",
                    f"{result.avg_ms:.3f}",
                    f"{result.max_ms:.3f}",
                    f"{result.throughput:.0f}/s",
                    f"{result.memory_kb:.1f}",
                )
            
            console.print(table)
            
            # Summary
            if report.summary:
                console.print("\n[bold]Summary:[/bold]")
                for key, value in report.summary.items():
                    console.print(f"  {key}: {value}")
            
            return console.export_text()
        
        except ImportError:
            # Fallback to plain text
            return self._generate_plain(report)
    
    def _generate_plain(self, report: BenchmarkReport) -> str:
        """Generate plain text report."""
        lines = [
            f"{'='*60}",
            f"  {report.title}",
            f"  {report.timestamp.strftime('%Y-%m-%d %H:%M:%S')}",
            f"{'='*60}",
            "",
        ]
        
        for result in report.results:
            lines.append(
                f"{result.name}: "
                f"avg={result.avg_ms:.3f}ms, "
                f"min={result.min_ms:.3f}ms, "
                f"max={result.max_ms:.3f}ms"
            )
        
        return "\n".join(lines)


class JSONReportGenerator(ReportGenerator):
    """Generate JSON reports.
    
    Usage:
        generator = JSONReportGenerator()
        json_str = generator.generate(report)
        generator.save(report, Path("benchmark.json"))
    """
    
    def __init__(self, indent: int = 2) -> None:
        self.indent = indent
    
    def generate(self, report: BenchmarkReport) -> str:
        """Generate JSON report."""
        data = {
            "title": report.title,
            "timestamp": report.timestamp.isoformat(),
            "environment": report.environment,
            "results": [
                {
                    "name": r.name,
                    "iterations": r.iterations,
                    "min_ms": r.min_ms,
                    "max_ms": r.max_ms,
                    "avg_ms": r.avg_ms,
                    "median_ms": r.median_ms,
                    "std_dev_ms": r.std_dev_ms,
                    "throughput": r.throughput,
                    "memory_kb": r.memory_kb,
                    "metadata": r.metadata,
                }
                for r in report.results
            ],
            "summary": report.summary,
        }
        
        return json.dumps(data, indent=self.indent)


class HTMLReportGenerator(ReportGenerator):
    """Generate HTML reports with charts.
    
    Usage:
        generator = HTMLReportGenerator()
        html = generator.generate(report)
        generator.save(report, Path("benchmark.html"))
    """
    
    def generate(self, report: BenchmarkReport) -> str:
        """Generate HTML report."""
        results_html = ""
        for result in report.results:
            results_html += f"""
            <tr>
                <td>{result.name}</td>
                <td class="number">{result.iterations}</td>
                <td class="number">{result.min_ms:.3f}</td>
                <td class="number">{result.avg_ms:.3f}</td>
                <td class="number">{result.max_ms:.3f}</td>
                <td class="number">{result.throughput:.0f}/s</td>
                <td class="number">{result.memory_kb:.1f}</td>
            </tr>
            """
        
        env_html = ""
        for key, value in report.environment.items():
            env_html += f"<dt>{key}</dt><dd>{value}</dd>"
        
        html = f"""
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>{report.title}</title>
    <style>
        body {{ font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; max-width: 1200px; margin: 0 auto; padding: 20px; }}
        h1 {{ color: #333; border-bottom: 2px solid #4A90A4; padding-bottom: 10px; }}
        .timestamp {{ color: #666; font-size: 0.9em; }}
        table {{ width: 100%; border-collapse: collapse; margin: 20px 0; }}
        th, td {{ padding: 12px; text-align: left; border-bottom: 1px solid #ddd; }}
        th {{ background: #4A90A4; color: white; }}
        tr:hover {{ background: #f5f5f5; }}
        .number {{ text-align: right; font-family: monospace; }}
        dl {{ display: grid; grid-template-columns: max-content 1fr; gap: 5px 20px; }}
        dt {{ font-weight: bold; color: #666; }}
        .summary {{ background: #f9f9f9; padding: 15px; border-radius: 5px; }}
    </style>
</head>
<body>
    <h1>{report.title}</h1>
    <p class="timestamp">Generated: {report.timestamp.strftime('%Y-%m-%d %H:%M:%S')}</p>
    
    <h2>Environment</h2>
    <dl>{env_html}</dl>
    
    <h2>Results</h2>
    <table>
        <thead>
            <tr>
                <th>Benchmark</th>
                <th>Iterations</th>
                <th>Min (ms)</th>
                <th>Avg (ms)</th>
                <th>Max (ms)</th>
                <th>Throughput</th>
                <th>Memory (KB)</th>
            </tr>
        </thead>
        <tbody>
            {results_html}
        </tbody>
    </table>
    
    <h2>Summary</h2>
    <div class="summary">
        <dl>
            {"".join(f"<dt>{k}</dt><dd>{v}</dd>" for k, v in report.summary.items())}
        </dl>
    </div>
</body>
</html>
        """
        
        return html


class MarkdownReportGenerator(ReportGenerator):
    """Generate Markdown reports.
    
    Usage:
        generator = MarkdownReportGenerator()
        md = generator.generate(report)
    """
    
    def generate(self, report: BenchmarkReport) -> str:
        """Generate Markdown report."""
        lines = [
            f"# {report.title}",
            "",
            f"*Generated: {report.timestamp.strftime('%Y-%m-%d %H:%M:%S')}*",
            "",
            "## Environment",
            "",
        ]
        
        for key, value in report.environment.items():
            lines.append(f"- **{key}**: {value}")
        
        lines.extend([
            "",
            "## Results",
            "",
            "| Benchmark | Iterations | Min (ms) | Avg (ms) | Max (ms) | Throughput | Memory (KB) |",
            "|-----------|------------|----------|----------|----------|------------|-------------|",
        ])
        
        for result in report.results:
            lines.append(
                f"| {result.name} | {result.iterations} | "
                f"{result.min_ms:.3f} | {result.avg_ms:.3f} | {result.max_ms:.3f} | "
                f"{result.throughput:.0f}/s | {result.memory_kb:.1f} |"
            )
        
        lines.extend([
            "",
            "## Summary",
            "",
        ])
        
        for key, value in report.summary.items():
            lines.append(f"- **{key}**: {value}")
        
        return "\n".join(lines)
