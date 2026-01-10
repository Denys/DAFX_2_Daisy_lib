"""File upload validation including size, type, and content verification."""

from __future__ import annotations

import hashlib
import mimetypes
import os
from dataclasses import dataclass
from pathlib import Path
from typing import Any, BinaryIO, Callable, Dict, List, Optional, Set, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
    ValidationSeverity,
)
from validation_infrastructure.errors.exceptions import FileValidationError


@dataclass
class FileInfo:
    """Information about a file being validated."""

    filename: str
    size: int
    content_type: Optional[str] = None
    extension: Optional[str] = None
    checksum: Optional[str] = None
    metadata: Dict[str, Any] = None
    
    def __post_init__(self):
        if self.metadata is None:
            self.metadata = {}
        
        if self.extension is None:
            self.extension = Path(self.filename).suffix.lower()


# Magic bytes for file type detection
FILE_SIGNATURES: Dict[str, List[bytes]] = {
    "image/jpeg": [b"\xff\xd8\xff"],
    "image/png": [b"\x89PNG\r\n\x1a\n"],
    "image/gif": [b"GIF87a", b"GIF89a"],
    "image/webp": [b"RIFF"],
    "image/bmp": [b"BM"],
    "image/tiff": [b"\x49\x49\x2a\x00", b"\x4d\x4d\x00\x2a"],
    "application/pdf": [b"%PDF"],
    "application/zip": [b"PK\x03\x04"],
    "application/gzip": [b"\x1f\x8b"],
    "application/x-rar": [b"Rar!"],
    "application/vnd.ms-excel": [b"\xd0\xcf\x11\xe0"],
    "audio/mpeg": [b"\xff\xfb", b"\xff\xfa", b"ID3"],
    "audio/wav": [b"RIFF"],
    "audio/ogg": [b"OggS"],
    "video/mp4": [b"\x00\x00\x00"],
    "video/webm": [b"\x1a\x45\xdf\xa3"],
}


def detect_file_type(content: bytes) -> Optional[str]:
    """Detect file type from content using magic bytes."""
    for mime_type, signatures in FILE_SIGNATURES.items():
        for sig in signatures:
            if content.startswith(sig):
                return mime_type
    return None


class FileValidator(BaseValidator[FileInfo]):
    """Validates file uploads."""

    # Default allowed extensions by category
    IMAGE_EXTENSIONS: Set[str] = {".jpg", ".jpeg", ".png", ".gif", ".webp", ".svg", ".bmp"}
    DOCUMENT_EXTENSIONS: Set[str] = {".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".txt"}
    ARCHIVE_EXTENSIONS: Set[str] = {".zip", ".rar", ".7z", ".tar", ".gz"}
    
    # Default size limits (in bytes)
    DEFAULT_MAX_SIZE = 10 * 1024 * 1024  # 10 MB
    DEFAULT_MIN_SIZE = 1  # 1 byte

    def __init__(
        self,
        allowed_extensions: Optional[Set[str]] = None,
        allowed_mime_types: Optional[Set[str]] = None,
        max_size: int = DEFAULT_MAX_SIZE,
        min_size: int = DEFAULT_MIN_SIZE,
        verify_content: bool = True,
        allowed_content_types: Optional[Set[str]] = None,
        disallow_executable: bool = True,
        custom_validators: Optional[List[Callable[[FileInfo, bytes], Optional[str]]]] = None,
    ):
        """
        Initialize file validator.
        
        Args:
            allowed_extensions: Set of allowed file extensions (e.g., {".jpg", ".png"})
            allowed_mime_types: Set of allowed MIME types
            max_size: Maximum file size in bytes
            min_size: Minimum file size in bytes
            verify_content: Whether to verify content matches extension
            allowed_content_types: Additional allowed content types (from detection)
            disallow_executable: Block executable files
            custom_validators: List of custom validation functions
        """
        super().__init__(name="FileValidator", error_code="file_error")
        self.allowed_extensions = allowed_extensions
        self.allowed_mime_types = allowed_mime_types
        self.max_size = max_size
        self.min_size = min_size
        self.verify_content = verify_content
        self.allowed_content_types = allowed_content_types
        self.disallow_executable = disallow_executable
        self.custom_validators = custom_validators or []
        
        # Executable extensions to block
        self._executable_extensions = {
            ".exe", ".bat", ".cmd", ".com", ".msi", ".scr",
            ".ps1", ".vbs", ".js", ".jar", ".sh", ".bash",
            ".app", ".dmg", ".pkg",
        }
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[FileInfo]:
        """
        Validate a file.
        
        Args:
            value: Can be a dict with 'filename', 'content', and optionally 'content_type',
                   a file path (str or Path), a file object, or a FileInfo instance
        """
        ctx = self._create_context(context)
        
        # Parse input
        try:
            file_info, content = self._parse_input(value)
        except Exception as e:
            return ValidationResult.from_error(
                f"Invalid file input: {e}",
                code="invalid_input",
                value=value,
            )
        
        issues: List[ValidationIssue] = []
        warnings: List[ValidationIssue] = []
        
        # Size validation
        if file_info.size < self.min_size:
            issues.append(
                ValidationIssue(
                    message=f"File size {file_info.size} bytes is below minimum {self.min_size}",
                    code="file_too_small",
                    field="size",
                )
            )
        
        if file_info.size > self.max_size:
            issues.append(
                ValidationIssue(
                    message=f"File size {file_info.size} bytes exceeds maximum {self.max_size}",
                    code="file_too_large",
                    field="size",
                )
            )
        
        # Extension validation
        if self.allowed_extensions:
            ext = file_info.extension.lower() if file_info.extension else ""
            if ext not in self.allowed_extensions:
                issues.append(
                    ValidationIssue(
                        message=f"File extension '{ext}' is not allowed",
                        code="extension_not_allowed",
                        field="extension",
                    )
                )
        
        # Executable check
        if self.disallow_executable:
            ext = file_info.extension.lower() if file_info.extension else ""
            if ext in self._executable_extensions:
                issues.append(
                    ValidationIssue(
                        message=f"Executable files are not allowed",
                        code="executable_not_allowed",
                        field="extension",
                    )
                )
        
        # MIME type validation
        if self.allowed_mime_types and file_info.content_type:
            if file_info.content_type not in self.allowed_mime_types:
                issues.append(
                    ValidationIssue(
                        message=f"MIME type '{file_info.content_type}' is not allowed",
                        code="mime_type_not_allowed",
                        field="content_type",
                    )
                )
        
        # Content verification
        if self.verify_content and content:
            detected_type = detect_file_type(content[:1024])
            file_info.metadata["detected_type"] = detected_type
            
            # Check if detected type matches declared type
            if detected_type and file_info.content_type:
                if detected_type != file_info.content_type:
                    warnings.append(
                        ValidationIssue(
                            message=f"Content type mismatch: declared '{file_info.content_type}', detected '{detected_type}'",
                            code="content_type_mismatch",
                            severity=ValidationSeverity.WARNING,
                        )
                    )
            
            # Check against allowed content types
            if self.allowed_content_types and detected_type:
                if detected_type not in self.allowed_content_types:
                    issues.append(
                        ValidationIssue(
                            message=f"Detected content type '{detected_type}' is not allowed",
                            code="content_type_not_allowed",
                        )
                    )
        
        # Calculate checksum
        if content:
            file_info.checksum = hashlib.sha256(content).hexdigest()
        
        # Custom validators
        for validator_func in self.custom_validators:
            error = validator_func(file_info, content or b"")
            if error:
                issues.append(
                    ValidationIssue(
                        message=error,
                        code="custom_validation_failed",
                    )
                )
        
        if issues:
            result = ValidationResult.failure(issues, value=file_info)
        else:
            result = ValidationResult.success(file_info)
        
        result.warnings = warnings
        return result
    
    def _parse_input(self, value: Any) -> tuple[FileInfo, Optional[bytes]]:
        """Parse different input types into FileInfo and content."""
        content: Optional[bytes] = None
        
        if isinstance(value, FileInfo):
            return value, None
        
        if isinstance(value, dict):
            filename = value.get("filename", "unknown")
            content = value.get("content", b"")
            content_type = value.get("content_type")
            
            if isinstance(content, str):
                content = content.encode()
            
            return FileInfo(
                filename=filename,
                size=len(content),
                content_type=content_type,
            ), content
        
        if isinstance(value, (str, Path)):
            path = Path(value)
            content = path.read_bytes()
            content_type, _ = mimetypes.guess_type(str(path))
            
            return FileInfo(
                filename=path.name,
                size=len(content),
                content_type=content_type,
            ), content
        
        if hasattr(value, "read"):
            # File-like object
            content = value.read()
            filename = getattr(value, "name", "unknown")
            content_type = getattr(value, "content_type", None)
            
            return FileInfo(
                filename=filename,
                size=len(content),
                content_type=content_type,
            ), content
        
        raise ValueError(f"Unsupported input type: {type(value)}")


class ImageValidator(FileValidator):
    """Specialized validator for image files."""

    def __init__(
        self,
        max_size: int = 10 * 1024 * 1024,  # 10 MB
        min_size: int = 100,
        allowed_formats: Optional[Set[str]] = None,
        max_width: Optional[int] = None,
        max_height: Optional[int] = None,
        min_width: Optional[int] = None,
        min_height: Optional[int] = None,
        max_aspect_ratio: Optional[float] = None,
        min_aspect_ratio: Optional[float] = None,
    ):
        allowed_extensions = allowed_formats or self.IMAGE_EXTENSIONS
        super().__init__(
            allowed_extensions=allowed_extensions,
            max_size=max_size,
            min_size=min_size,
            verify_content=True,
        )
        self.name = "ImageValidator"
        self.max_width = max_width
        self.max_height = max_height
        self.min_width = min_width
        self.min_height = min_height
        self.max_aspect_ratio = max_aspect_ratio
        self.min_aspect_ratio = min_aspect_ratio
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[FileInfo]:
        # Run base validation first
        result = super().validate(value, context)
        
        if not result.is_valid:
            return result
        
        file_info = result.value
        
        # Try to get image dimensions
        try:
            width, height = self._get_image_dimensions(value)
            file_info.metadata["width"] = width
            file_info.metadata["height"] = height
            file_info.metadata["aspect_ratio"] = width / height if height > 0 else 0
            
            issues: List[ValidationIssue] = []
            
            # Dimension checks
            if self.max_width and width > self.max_width:
                issues.append(ValidationIssue(
                    message=f"Image width {width}px exceeds maximum {self.max_width}px",
                    code="image_too_wide",
                ))
            
            if self.max_height and height > self.max_height:
                issues.append(ValidationIssue(
                    message=f"Image height {height}px exceeds maximum {self.max_height}px",
                    code="image_too_tall",
                ))
            
            if self.min_width and width < self.min_width:
                issues.append(ValidationIssue(
                    message=f"Image width {width}px is below minimum {self.min_width}px",
                    code="image_too_narrow",
                ))
            
            if self.min_height and height < self.min_height:
                issues.append(ValidationIssue(
                    message=f"Image height {height}px is below minimum {self.min_height}px",
                    code="image_too_short",
                ))
            
            # Aspect ratio checks
            aspect_ratio = file_info.metadata["aspect_ratio"]
            if self.max_aspect_ratio and aspect_ratio > self.max_aspect_ratio:
                issues.append(ValidationIssue(
                    message=f"Image aspect ratio {aspect_ratio:.2f} exceeds maximum {self.max_aspect_ratio}",
                    code="aspect_ratio_too_high",
                ))
            
            if self.min_aspect_ratio and aspect_ratio < self.min_aspect_ratio:
                issues.append(ValidationIssue(
                    message=f"Image aspect ratio {aspect_ratio:.2f} is below minimum {self.min_aspect_ratio}",
                    code="aspect_ratio_too_low",
                ))
            
            if issues:
                for issue in issues:
                    result.add_issue(issue)
        
        except Exception:
            # Could not read dimensions - might not be a valid image
            result.warnings.append(ValidationIssue(
                message="Could not read image dimensions",
                code="dimensions_unreadable",
                severity=ValidationSeverity.WARNING,
            ))
        
        return result
    
    def _get_image_dimensions(self, value: Any) -> tuple[int, int]:
        """Get image dimensions using PIL or basic parsing."""
        try:
            from PIL import Image
            
            if isinstance(value, (str, Path)):
                with Image.open(value) as img:
                    return img.size
            elif isinstance(value, dict) and "content" in value:
                import io
                content = value["content"]
                if isinstance(content, str):
                    content = content.encode()
                with Image.open(io.BytesIO(content)) as img:
                    return img.size
        except ImportError:
            pass
        
        raise ValueError("Cannot determine image dimensions (PIL not available)")


class DocumentValidator(FileValidator):
    """Specialized validator for document files."""

    def __init__(
        self,
        max_size: int = 50 * 1024 * 1024,  # 50 MB
        allowed_formats: Optional[Set[str]] = None,
        scan_for_macros: bool = True,
    ):
        allowed_extensions = allowed_formats or self.DOCUMENT_EXTENSIONS
        super().__init__(
            allowed_extensions=allowed_extensions,
            max_size=max_size,
            verify_content=True,
        )
        self.name = "DocumentValidator"
        self.scan_for_macros = scan_for_macros
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[FileInfo]:
        result = super().validate(value, context)
        
        if not result.is_valid:
            return result
        
        # Scan for macros in Office documents
        if self.scan_for_macros:
            file_info = result.value
            ext = file_info.extension.lower() if file_info.extension else ""
            
            if ext in {".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx"}:
                # Basic macro detection (simplified)
                try:
                    if isinstance(value, dict) and "content" in value:
                        content = value["content"]
                        if isinstance(content, str):
                            content = content.encode()
                        
                        # Look for VBA signatures
                        if b"vbaProject" in content or b"_VBA_PROJECT" in content:
                            result.warnings.append(ValidationIssue(
                                message="Document may contain macros",
                                code="macro_detected",
                                severity=ValidationSeverity.WARNING,
                            ))
                except Exception:
                    pass
        
        return result


def validate_upload(
    file_data: Union[Dict[str, Any], str, Path, BinaryIO],
    allowed_extensions: Optional[Set[str]] = None,
    max_size: int = 10 * 1024 * 1024,
    verify_content: bool = True,
) -> ValidationResult[FileInfo]:
    """
    Convenience function for file upload validation.
    
    Example:
        result = validate_upload(
            {"filename": "photo.jpg", "content": file_content},
            allowed_extensions={".jpg", ".png", ".gif"},
            max_size=5 * 1024 * 1024,
        )
    """
    validator = FileValidator(
        allowed_extensions=allowed_extensions,
        max_size=max_size,
        verify_content=verify_content,
    )
    return validator.validate(file_data)
