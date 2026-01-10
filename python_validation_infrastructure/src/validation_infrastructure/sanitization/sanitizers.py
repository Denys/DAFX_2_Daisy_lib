"""Data sanitization classes for cleaning and normalizing input."""

from __future__ import annotations

import html
import re
import unicodedata
from abc import ABC, abstractmethod
from typing import Any, Callable, Dict, Generic, List, Optional, Set, TypeVar, Union
from urllib.parse import quote, unquote

T = TypeVar("T")


class Sanitizer(ABC, Generic[T]):
    """Abstract base class for sanitizers."""

    @abstractmethod
    def sanitize(self, value: Any) -> T:
        """Sanitize the input value."""
        ...
    
    def __call__(self, value: Any) -> T:
        """Make sanitizer callable."""
        return self.sanitize(value)
    
    def then(self, other: Sanitizer[Any]) -> ChainedSanitizer:
        """Chain this sanitizer with another."""
        return ChainedSanitizer([self, other])


class ChainedSanitizer(Sanitizer[Any]):
    """Chains multiple sanitizers in sequence."""

    def __init__(self, sanitizers: List[Sanitizer[Any]]):
        self.sanitizers = sanitizers
    
    def sanitize(self, value: Any) -> Any:
        """Apply all sanitizers in sequence."""
        result = value
        for sanitizer in self.sanitizers:
            result = sanitizer.sanitize(result)
        return result
    
    def then(self, other: Sanitizer[Any]) -> ChainedSanitizer:
        """Add another sanitizer to the chain."""
        return ChainedSanitizer(self.sanitizers + [other])


class StringSanitizer(Sanitizer[str]):
    """Comprehensive string sanitizer with multiple options."""

    def __init__(
        self,
        strip: bool = True,
        lowercase: bool = False,
        uppercase: bool = False,
        titlecase: bool = False,
        normalize_unicode: Optional[str] = "NFC",  # NFC, NFD, NFKC, NFKD
        remove_control_chars: bool = True,
        collapse_whitespace: bool = False,
        remove_extra_whitespace: bool = False,
        max_length: Optional[int] = None,
        truncate_with: str = "...",
        replace_patterns: Optional[Dict[str, str]] = None,
        allowed_chars: Optional[Set[str]] = None,
        remove_chars: Optional[Set[str]] = None,
        escape_html: bool = False,
        unescape_html: bool = False,
    ):
        self.strip = strip
        self.lowercase = lowercase
        self.uppercase = uppercase
        self.titlecase = titlecase
        self.normalize_unicode = normalize_unicode
        self.remove_control_chars = remove_control_chars
        self.collapse_whitespace = collapse_whitespace
        self.remove_extra_whitespace = remove_extra_whitespace
        self.max_length = max_length
        self.truncate_with = truncate_with
        self.replace_patterns = replace_patterns or {}
        self.allowed_chars = allowed_chars
        self.remove_chars = remove_chars
        self.escape_html = escape_html
        self.unescape_html = unescape_html
    
    def sanitize(self, value: Any) -> str:
        """Sanitize string value."""
        if value is None:
            return ""
        
        result = str(value)
        
        # Unicode normalization
        if self.normalize_unicode:
            result = unicodedata.normalize(self.normalize_unicode, result)
        
        # Remove control characters
        if self.remove_control_chars:
            result = "".join(
                char for char in result
                if unicodedata.category(char)[0] != "C" or char in "\n\r\t"
            )
        
        # Strip whitespace
        if self.strip:
            result = result.strip()
        
        # Collapse multiple whitespace to single space
        if self.collapse_whitespace:
            result = re.sub(r"\s+", " ", result)
        
        # Remove extra whitespace (leading/trailing per line)
        if self.remove_extra_whitespace:
            lines = result.split("\n")
            result = "\n".join(line.strip() for line in lines)
        
        # Case transformations
        if self.lowercase:
            result = result.lower()
        elif self.uppercase:
            result = result.upper()
        elif self.titlecase:
            result = result.title()
        
        # Character filtering
        if self.allowed_chars:
            result = "".join(c for c in result if c in self.allowed_chars)
        
        if self.remove_chars:
            result = "".join(c for c in result if c not in self.remove_chars)
        
        # Pattern replacements
        for pattern, replacement in self.replace_patterns.items():
            result = re.sub(pattern, replacement, result)
        
        # HTML escaping
        if self.escape_html:
            result = html.escape(result)
        elif self.unescape_html:
            result = html.unescape(result)
        
        # Truncation
        if self.max_length and len(result) > self.max_length:
            truncate_len = self.max_length - len(self.truncate_with)
            result = result[:truncate_len] + self.truncate_with
        
        return result


class HTMLSanitizer(Sanitizer[str]):
    """Sanitizer for HTML content to prevent XSS attacks."""

    # Safe tags that are allowed by default
    DEFAULT_ALLOWED_TAGS: Set[str] = {
        "a", "abbr", "acronym", "b", "blockquote", "br", "code",
        "div", "em", "h1", "h2", "h3", "h4", "h5", "h6", "hr",
        "i", "li", "ol", "p", "pre", "span", "strong", "sub",
        "sup", "table", "tbody", "td", "th", "thead", "tr", "u", "ul",
    }
    
    # Safe attributes that are allowed by default
    DEFAULT_ALLOWED_ATTRS: Dict[str, Set[str]] = {
        "*": {"class", "id", "title"},
        "a": {"href", "rel", "target"},
        "img": {"src", "alt", "width", "height"},
        "table": {"border", "cellpadding", "cellspacing"},
    }
    
    # Dangerous patterns to remove
    DANGEROUS_PATTERNS: List[str] = [
        r"javascript:",
        r"vbscript:",
        r"data:",
        r"on\w+\s*=",  # Event handlers
        r"<script[^>]*>.*?</script>",
        r"<style[^>]*>.*?</style>",
    ]

    def __init__(
        self,
        allowed_tags: Optional[Set[str]] = None,
        allowed_attrs: Optional[Dict[str, Set[str]]] = None,
        strip_tags: bool = False,
        strip_comments: bool = True,
        escape_unknown_tags: bool = True,
    ):
        self.allowed_tags = allowed_tags or self.DEFAULT_ALLOWED_TAGS
        self.allowed_attrs = allowed_attrs or self.DEFAULT_ALLOWED_ATTRS
        self.strip_tags = strip_tags
        self.strip_comments = strip_comments
        self.escape_unknown_tags = escape_unknown_tags
        
        # Compile patterns for performance
        self._dangerous_patterns = [
            re.compile(p, re.IGNORECASE | re.DOTALL)
            for p in self.DANGEROUS_PATTERNS
        ]
    
    def sanitize(self, value: Any) -> str:
        """Sanitize HTML content."""
        if value is None:
            return ""
        
        result = str(value)
        
        # Remove dangerous patterns first
        for pattern in self._dangerous_patterns:
            result = pattern.sub("", result)
        
        # Strip HTML comments
        if self.strip_comments:
            result = re.sub(r"<!--.*?-->", "", result, flags=re.DOTALL)
        
        if self.strip_tags:
            # Remove all HTML tags
            result = re.sub(r"<[^>]+>", "", result)
        else:
            # Process tags
            result = self._process_tags(result)
        
        return result
    
    def _process_tags(self, content: str) -> str:
        """Process and filter HTML tags."""
        # Simple tag processing - for production use consider bleach or similar
        tag_pattern = re.compile(r"<(/?)(\w+)([^>]*)>", re.IGNORECASE)
        
        def process_tag(match: re.Match) -> str:
            closing = match.group(1)
            tag_name = match.group(2).lower()
            attrs = match.group(3)
            
            if tag_name not in self.allowed_tags:
                if self.escape_unknown_tags:
                    return html.escape(match.group(0))
                return ""
            
            # Process attributes
            if attrs and not closing:
                attrs = self._process_attrs(tag_name, attrs)
            
            return f"<{closing}{tag_name}{attrs}>"
        
        return tag_pattern.sub(process_tag, content)
    
    def _process_attrs(self, tag_name: str, attrs: str) -> str:
        """Process and filter tag attributes."""
        allowed = self.allowed_attrs.get(tag_name, set())
        allowed = allowed | self.allowed_attrs.get("*", set())
        
        attr_pattern = re.compile(r'(\w+)=["\']([^"\']*)["\']')
        
        valid_attrs = []
        for match in attr_pattern.finditer(attrs):
            attr_name = match.group(1).lower()
            attr_value = match.group(2)
            
            if attr_name in allowed:
                # Sanitize attribute value
                attr_value = html.escape(attr_value)
                valid_attrs.append(f'{attr_name}="{attr_value}"')
        
        return " " + " ".join(valid_attrs) if valid_attrs else ""


class SQLSanitizer(Sanitizer[str]):
    """Sanitizer to prevent SQL injection attacks."""

    # Characters that need escaping in SQL
    SQL_ESCAPE_CHARS: Dict[str, str] = {
        "'": "''",
        "\\": "\\\\",
        "\x00": "\\0",
        "\n": "\\n",
        "\r": "\\r",
        "\x1a": "\\Z",
    }
    
    # Dangerous SQL keywords/patterns
    DANGEROUS_SQL_PATTERNS: List[str] = [
        r"\bUNION\b",
        r"\bSELECT\b.*\bFROM\b",
        r"\bINSERT\b.*\bINTO\b",
        r"\bUPDATE\b.*\bSET\b",
        r"\bDELETE\b.*\bFROM\b",
        r"\bDROP\b",
        r"\bTRUNCATE\b",
        r"\bALTER\b",
        r"\bEXEC\b",
        r"\bEXECUTE\b",
        r"--",
        r"/\*.*\*/",
        r";\s*$",
    ]

    def __init__(
        self,
        escape_quotes: bool = True,
        remove_dangerous_patterns: bool = True,
        remove_comments: bool = True,
        max_length: Optional[int] = None,
        dialect: str = "generic",  # generic, mysql, postgresql, sqlite
    ):
        self.escape_quotes = escape_quotes
        self.remove_dangerous_patterns = remove_dangerous_patterns
        self.remove_comments = remove_comments
        self.max_length = max_length
        self.dialect = dialect
        
        self._dangerous_patterns = [
            re.compile(p, re.IGNORECASE | re.DOTALL)
            for p in self.DANGEROUS_SQL_PATTERNS
        ]
    
    def sanitize(self, value: Any) -> str:
        """Sanitize value for safe SQL usage."""
        if value is None:
            return ""
        
        result = str(value)
        
        # Remove comments
        if self.remove_comments:
            result = re.sub(r"--.*$", "", result, flags=re.MULTILINE)
            result = re.sub(r"/\*.*?\*/", "", result, flags=re.DOTALL)
        
        # Remove dangerous patterns
        if self.remove_dangerous_patterns:
            for pattern in self._dangerous_patterns:
                result = pattern.sub("", result)
        
        # Escape quotes and special characters
        if self.escape_quotes:
            for char, escaped in self.SQL_ESCAPE_CHARS.items():
                result = result.replace(char, escaped)
        
        # Truncate
        if self.max_length:
            result = result[:self.max_length]
        
        return result
    
    @staticmethod
    def parameterize(query: str, params: Dict[str, Any]) -> tuple[str, List[Any]]:
        """
        Convert named parameters to positional for safer queries.
        
        Example:
            query = "SELECT * FROM users WHERE name = :name AND age > :age"
            safe_query, values = SQLSanitizer.parameterize(query, {"name": "John", "age": 18})
            # safe_query: "SELECT * FROM users WHERE name = ? AND age > ?"
            # values: ["John", 18]
        """
        values: List[Any] = []
        
        def replacer(match: re.Match) -> str:
            param_name = match.group(1)
            if param_name in params:
                values.append(params[param_name])
                return "?"
            return match.group(0)
        
        safe_query = re.sub(r":(\w+)", replacer, query)
        return safe_query, values


class PathSanitizer(Sanitizer[str]):
    """Sanitizer for file system paths to prevent path traversal attacks."""

    # Dangerous path components
    DANGEROUS_PATTERNS: List[str] = [
        r"\.\./",
        r"\.\.",
        r"\.\\",
        r"%2e%2e",  # URL encoded ..
        r"%252e%252e",  # Double URL encoded
        r"~",
        r"\$",
    ]
    
    # Characters not allowed in filenames
    INVALID_FILENAME_CHARS: Set[str] = {
        "/", "\\", ":", "*", "?", '"', "<", ">", "|", "\x00",
    }

    def __init__(
        self,
        allow_absolute: bool = False,
        allow_parent_refs: bool = False,
        max_length: int = 255,
        normalize: bool = True,
        allowed_extensions: Optional[Set[str]] = None,
        base_path: Optional[str] = None,
    ):
        self.allow_absolute = allow_absolute
        self.allow_parent_refs = allow_parent_refs
        self.max_length = max_length
        self.normalize = normalize
        self.allowed_extensions = allowed_extensions
        self.base_path = base_path
        
        self._dangerous_patterns = [
            re.compile(p, re.IGNORECASE) for p in self.DANGEROUS_PATTERNS
        ]
    
    def sanitize(self, value: Any) -> str:
        """Sanitize file path."""
        if value is None:
            return ""
        
        result = str(value)
        
        # Normalize path separators
        if self.normalize:
            result = result.replace("\\", "/")
        
        # Remove dangerous patterns
        if not self.allow_parent_refs:
            for pattern in self._dangerous_patterns:
                result = pattern.sub("", result)
        
        # Remove absolute path prefix if not allowed
        if not self.allow_absolute:
            result = result.lstrip("/\\")
            # Remove Windows drive letters
            result = re.sub(r"^[a-zA-Z]:", "", result)
        
        # Remove invalid characters
        result = "".join(
            c for c in result if c not in self.INVALID_FILENAME_CHARS
        )
        
        # Check extension
        if self.allowed_extensions:
            import os
            _, ext = os.path.splitext(result)
            if ext.lower() not in self.allowed_extensions:
                result = result + ".txt"  # Default safe extension
        
        # Truncate
        if len(result) > self.max_length:
            result = result[:self.max_length]
        
        # Resolve against base path
        if self.base_path:
            import os
            result = os.path.normpath(os.path.join(self.base_path, result))
            # Ensure result is still under base_path
            if not result.startswith(os.path.normpath(self.base_path)):
                result = self.base_path
        
        return result
    
    @staticmethod
    def sanitize_filename(filename: str, replacement: str = "_") -> str:
        """Sanitize a filename (not a path)."""
        # Remove path separators
        result = filename.replace("/", replacement).replace("\\", replacement)
        
        # Remove invalid characters
        invalid = set('/\\:*?"<>|')
        result = "".join(replacement if c in invalid else c for c in result)
        
        # Remove leading/trailing dots and spaces
        result = result.strip(". ")
        
        # Ensure not empty
        if not result:
            result = "unnamed"
        
        return result


class NumericSanitizer(Sanitizer[Union[int, float]]):
    """Sanitizer for numeric values."""

    def __init__(
        self,
        target_type: type = float,
        default: Union[int, float] = 0,
        min_value: Optional[Union[int, float]] = None,
        max_value: Optional[Union[int, float]] = None,
        precision: Optional[int] = None,
        allow_negative: bool = True,
        strip_non_numeric: bool = True,
    ):
        self.target_type = target_type
        self.default = default
        self.min_value = min_value
        self.max_value = max_value
        self.precision = precision
        self.allow_negative = allow_negative
        self.strip_non_numeric = strip_non_numeric
    
    def sanitize(self, value: Any) -> Union[int, float]:
        """Sanitize numeric value."""
        if value is None:
            return self.default
        
        # Already numeric
        if isinstance(value, (int, float)):
            result = value
        else:
            # Convert from string
            str_value = str(value)
            
            if self.strip_non_numeric:
                # Keep only digits, decimal point, and minus sign
                str_value = re.sub(r"[^\d.\-]", "", str_value)
            
            try:
                result = float(str_value) if "." in str_value else int(str_value)
            except (ValueError, TypeError):
                return self.default
        
        # Handle negative values
        if not self.allow_negative and result < 0:
            result = abs(result)
        
        # Apply bounds
        if self.min_value is not None:
            result = max(result, self.min_value)
        if self.max_value is not None:
            result = min(result, self.max_value)
        
        # Apply precision
        if self.precision is not None:
            result = round(result, self.precision)
        
        # Convert to target type
        return self.target_type(result)


class EmailSanitizer(Sanitizer[str]):
    """Sanitizer for email addresses."""

    def __init__(
        self,
        lowercase: bool = True,
        strip_plus_addressing: bool = False,
        normalize_domains: bool = True,
    ):
        self.lowercase = lowercase
        self.strip_plus_addressing = strip_plus_addressing
        self.normalize_domains = normalize_domains
    
    def sanitize(self, value: Any) -> str:
        """Sanitize email address."""
        if value is None:
            return ""
        
        email = str(value).strip()
        
        if "@" not in email:
            return email
        
        local, domain = email.rsplit("@", 1)
        
        # Lowercase
        if self.lowercase:
            local = local.lower()
            domain = domain.lower()
        
        # Strip plus addressing (user+tag@example.com -> user@example.com)
        if self.strip_plus_addressing and "+" in local:
            local = local.split("+")[0]
        
        # Normalize common domain aliases
        if self.normalize_domains:
            domain_map = {
                "googlemail.com": "gmail.com",
            }
            domain = domain_map.get(domain, domain)
        
        return f"{local}@{domain}"


class URLSanitizer(Sanitizer[str]):
    """Sanitizer for URLs."""

    ALLOWED_SCHEMES: Set[str] = {"http", "https", "ftp", "mailto"}

    def __init__(
        self,
        allowed_schemes: Optional[Set[str]] = None,
        default_scheme: str = "https",
        encode_special_chars: bool = True,
        strip_tracking_params: bool = False,
    ):
        self.allowed_schemes = allowed_schemes or self.ALLOWED_SCHEMES
        self.default_scheme = default_scheme
        self.encode_special_chars = encode_special_chars
        self.strip_tracking_params = strip_tracking_params
        
        # Common tracking parameters
        self._tracking_params = {
            "utm_source", "utm_medium", "utm_campaign", "utm_term", "utm_content",
            "fbclid", "gclid", "ref", "source",
        }
    
    def sanitize(self, value: Any) -> str:
        """Sanitize URL."""
        if value is None:
            return ""
        
        from urllib.parse import urlparse, urlunparse, parse_qs, urlencode
        
        url = str(value).strip()
        
        # Add scheme if missing
        if not re.match(r"^\w+://", url):
            url = f"{self.default_scheme}://{url}"
        
        try:
            parsed = urlparse(url)
        except Exception:
            return ""
        
        # Validate scheme
        if parsed.scheme.lower() not in self.allowed_schemes:
            return ""
        
        # Strip tracking parameters
        if self.strip_tracking_params and parsed.query:
            params = parse_qs(parsed.query)
            filtered_params = {
                k: v for k, v in params.items()
                if k.lower() not in self._tracking_params
            }
            query = urlencode(filtered_params, doseq=True)
            parsed = parsed._replace(query=query)
        
        # Encode special characters in path
        if self.encode_special_chars:
            path = quote(unquote(parsed.path), safe="/")
            parsed = parsed._replace(path=path)
        
        return urlunparse(parsed)
