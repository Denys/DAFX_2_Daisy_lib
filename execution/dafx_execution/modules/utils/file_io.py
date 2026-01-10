"""File I/O utilities for common formats.

Provides consistent, error-handling interfaces for reading/writing
JSON, YAML, and CSV files.
"""

from __future__ import annotations

import csv
import json
from pathlib import Path
from typing import Any

import yaml

from dafx_execution.core import get_logger, ResourceError


logger = get_logger(__name__)


def read_json(path: Path) -> Any:
    """Read JSON file.
    
    Args:
        path: Path to JSON file.
    
    Returns:
        Parsed JSON data.
    
    Raises:
        ResourceError: If file cannot be read or parsed.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except FileNotFoundError:
        raise ResourceError(
            f"JSON file not found: {path}",
            resource_type="file",
            resource_path=path,
        )
    except json.JSONDecodeError as e:
        raise ResourceError(
            f"Invalid JSON in {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )


def write_json(
    path: Path,
    data: Any,
    indent: int = 2,
    ensure_ascii: bool = False,
) -> None:
    """Write data to JSON file.
    
    Args:
        path: Output path.
        data: Data to serialize.
        indent: JSON indentation level.
        ensure_ascii: If True, escape non-ASCII characters.
    
    Raises:
        ResourceError: If file cannot be written.
    """
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, indent=indent, ensure_ascii=ensure_ascii)
        logger.debug("Wrote JSON file", path=str(path))
    except Exception as e:
        raise ResourceError(
            f"Failed to write JSON to {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )


def read_yaml(path: Path) -> Any:
    """Read YAML file.
    
    Args:
        path: Path to YAML file.
    
    Returns:
        Parsed YAML data.
    
    Raises:
        ResourceError: If file cannot be read or parsed.
    """
    try:
        with open(path, "r", encoding="utf-8") as f:
            return yaml.safe_load(f)
    except FileNotFoundError:
        raise ResourceError(
            f"YAML file not found: {path}",
            resource_type="file",
            resource_path=path,
        )
    except yaml.YAMLError as e:
        raise ResourceError(
            f"Invalid YAML in {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )


def write_yaml(
    path: Path,
    data: Any,
    default_flow_style: bool = False,
) -> None:
    """Write data to YAML file.
    
    Args:
        path: Output path.
        data: Data to serialize.
        default_flow_style: YAML flow style setting.
    
    Raises:
        ResourceError: If file cannot be written.
    """
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8") as f:
            yaml.safe_dump(data, f, default_flow_style=default_flow_style)
        logger.debug("Wrote YAML file", path=str(path))
    except Exception as e:
        raise ResourceError(
            f"Failed to write YAML to {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )


def read_csv(
    path: Path,
    has_header: bool = True,
    delimiter: str = ",",
) -> list[dict[str, str]] | list[list[str]]:
    """Read CSV file.
    
    Args:
        path: Path to CSV file.
        has_header: If True, first row is treated as header.
        delimiter: Field delimiter character.
    
    Returns:
        List of dicts (if header) or list of lists.
    
    Raises:
        ResourceError: If file cannot be read.
    """
    try:
        with open(path, "r", encoding="utf-8", newline="") as f:
            if has_header:
                reader = csv.DictReader(f, delimiter=delimiter)
                return list(reader)
            else:
                reader = csv.reader(f, delimiter=delimiter)
                return list(reader)
    except FileNotFoundError:
        raise ResourceError(
            f"CSV file not found: {path}",
            resource_type="file",
            resource_path=path,
        )
    except csv.Error as e:
        raise ResourceError(
            f"CSV error in {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )


def write_csv(
    path: Path,
    data: list[dict[str, Any]] | list[list[Any]],
    fieldnames: list[str] | None = None,
    delimiter: str = ",",
) -> None:
    """Write data to CSV file.
    
    Args:
        path: Output path.
        data: Data to write (list of dicts or list of lists).
        fieldnames: Column names (required for list of dicts).
        delimiter: Field delimiter character.
    
    Raises:
        ResourceError: If file cannot be written.
    """
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with open(path, "w", encoding="utf-8", newline="") as f:
            if data and isinstance(data[0], dict):
                # Dict mode
                names = fieldnames or list(data[0].keys())
                writer = csv.DictWriter(f, fieldnames=names, delimiter=delimiter)
                writer.writeheader()
                writer.writerows(data)
            else:
                # List mode
                writer = csv.writer(f, delimiter=delimiter)
                if fieldnames:
                    writer.writerow(fieldnames)
                writer.writerows(data)
        logger.debug("Wrote CSV file", path=str(path))
    except Exception as e:
        raise ResourceError(
            f"Failed to write CSV to {path}: {e}",
            resource_type="file",
            resource_path=path,
            cause=e,
        )
