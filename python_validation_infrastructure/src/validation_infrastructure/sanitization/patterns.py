"""Regex-based pattern matching validators."""

from __future__ import annotations

import re
from typing import Any, Dict, List, Optional, Pattern, Set, Union

from validation_infrastructure.core.base import (
    BaseValidator,
    ValidationContext,
    ValidationIssue,
    ValidationResult,
)


class PatternValidator(BaseValidator[str]):
    """Validates strings against regex patterns."""

    def __init__(
        self,
        pattern: Union[str, Pattern[str]],
        flags: int = 0,
        full_match: bool = False,
        message: Optional[str] = None,
        code: str = "pattern_mismatch",
    ):
        """
        Initialize pattern validator.
        
        Args:
            pattern: Regex pattern string or compiled pattern
            flags: Regex flags (re.IGNORECASE, etc.)
            full_match: Whether to match entire string or just find pattern
            message: Custom error message
            code: Error code for validation failures
        """
        super().__init__(name="PatternValidator", error_code=code)
        
        if isinstance(pattern, str):
            self.pattern = re.compile(pattern, flags)
        else:
            self.pattern = pattern
        
        self.full_match = full_match
        self.custom_message = message
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        if self.full_match:
            match = self.pattern.fullmatch(value)
        else:
            match = self.pattern.search(value)
        
        if match:
            return ValidationResult.success(value)
        
        message = self.custom_message or f"Value does not match pattern: {self.pattern.pattern}"
        return ValidationResult.from_error(
            message,
            code=self.error_code,
            value=value,
        )


class EmailValidator(BaseValidator[str]):
    """Validates email addresses."""

    # RFC 5322 compliant email pattern (simplified)
    EMAIL_PATTERN = re.compile(
        r"^(?:[a-z0-9!#$%&'*+/=?^_`{|}~-]+(?:\.[a-z0-9!#$%&'*+/=?^_`{|}~-]+)*"
        r'|"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21\x23-\x5b\x5d-\x7f]'
        r'|\\[\x01-\x09\x0b\x0c\x0e-\x7f])*")'
        r"@(?:(?:[a-z0-9](?:[a-z0-9-]*[a-z0-9])?\.)+"
        r"[a-z0-9](?:[a-z0-9-]*[a-z0-9])?"
        r"|\[(?:(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}"
        r"(?:25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?"
        r"|[a-z0-9-]*[a-z0-9]:"
        r"(?:[\x01-\x08\x0b\x0c\x0e-\x1f\x21-\x5a\x53-\x7f]"
        r"|\\[\x01-\x09\x0b\x0c\x0e-\x7f])+)\])$",
        re.IGNORECASE,
    )
    
    # Simpler pattern for common cases
    SIMPLE_EMAIL_PATTERN = re.compile(
        r"^[a-zA-Z0-9._%+-]+@[a-zA-Z0-9.-]+\.[a-zA-Z]{2,}$"
    )

    def __init__(
        self,
        strict: bool = False,
        allowed_domains: Optional[Set[str]] = None,
        blocked_domains: Optional[Set[str]] = None,
        check_mx: bool = False,
    ):
        """
        Initialize email validator.
        
        Args:
            strict: Use strict RFC 5322 validation
            allowed_domains: Set of allowed email domains
            blocked_domains: Set of blocked email domains
            check_mx: Check if domain has MX records (requires dnspython)
        """
        super().__init__(name="EmailValidator", error_code="invalid_email")
        self.strict = strict
        self.allowed_domains = allowed_domains
        self.blocked_domains = blocked_domains
        self.check_mx = check_mx
        
        self.pattern = self.EMAIL_PATTERN if strict else self.SIMPLE_EMAIL_PATTERN
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        email = value.strip().lower()
        
        # Pattern match
        if not self.pattern.match(email):
            return ValidationResult.from_error(
                "Invalid email format",
                code="invalid_email_format",
                value=value,
            )
        
        # Extract domain
        _, domain = email.rsplit("@", 1)
        
        # Check allowed domains
        if self.allowed_domains and domain not in self.allowed_domains:
            return ValidationResult.from_error(
                f"Email domain '{domain}' is not allowed",
                code="domain_not_allowed",
                value=value,
            )
        
        # Check blocked domains
        if self.blocked_domains and domain in self.blocked_domains:
            return ValidationResult.from_error(
                f"Email domain '{domain}' is blocked",
                code="domain_blocked",
                value=value,
            )
        
        # Check MX records
        if self.check_mx:
            if not self._has_mx_record(domain):
                return ValidationResult.from_error(
                    f"Email domain '{domain}' has no mail server",
                    code="no_mx_record",
                    value=value,
                )
        
        return ValidationResult.success(email)
    
    def _has_mx_record(self, domain: str) -> bool:
        """Check if domain has MX records."""
        try:
            import dns.resolver
            dns.resolver.resolve(domain, "MX")
            return True
        except Exception:
            return False


class URLValidator(BaseValidator[str]):
    """Validates URLs."""

    URL_PATTERN = re.compile(
        r"^(?:(?:https?|ftp)://)"  # Scheme
        r"(?:(?:[A-Z0-9](?:[A-Z0-9-]{0,61}[A-Z0-9])?\.)+"  # Domain
        r"(?:[A-Z]{2,6}\.?|[A-Z0-9-]{2,}\.?)|"  # TLD
        r"localhost|"  # localhost
        r"\d{1,3}\.\d{1,3}\.\d{1,3}\.\d{1,3}|"  # IPv4
        r"\[?[A-F0-9]*:[A-F0-9:]+\]?)"  # IPv6
        r"(?::\d+)?"  # Port
        r"(?:/?|[/?]\S+)$",  # Path
        re.IGNORECASE,
    )

    def __init__(
        self,
        allowed_schemes: Optional[Set[str]] = None,
        require_tld: bool = True,
        allow_localhost: bool = False,
        allow_ip: bool = False,
        max_length: int = 2083,  # IE URL limit
    ):
        """
        Initialize URL validator.
        
        Args:
            allowed_schemes: Allowed URL schemes (default: http, https)
            require_tld: Require top-level domain
            allow_localhost: Allow localhost URLs
            allow_ip: Allow IP addresses in URLs
            max_length: Maximum URL length
        """
        super().__init__(name="URLValidator", error_code="invalid_url")
        self.allowed_schemes = allowed_schemes or {"http", "https"}
        self.require_tld = require_tld
        self.allow_localhost = allow_localhost
        self.allow_ip = allow_ip
        self.max_length = max_length
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        url = value.strip()
        
        # Length check
        if len(url) > self.max_length:
            return ValidationResult.from_error(
                f"URL exceeds maximum length of {self.max_length}",
                code="url_too_long",
                value=value,
            )
        
        # Pattern match
        if not self.URL_PATTERN.match(url):
            return ValidationResult.from_error(
                "Invalid URL format",
                code="invalid_url_format",
                value=value,
            )
        
        # Parse URL
        from urllib.parse import urlparse
        parsed = urlparse(url)
        
        # Check scheme
        if parsed.scheme.lower() not in self.allowed_schemes:
            return ValidationResult.from_error(
                f"URL scheme '{parsed.scheme}' is not allowed",
                code="scheme_not_allowed",
                value=value,
            )
        
        # Check localhost
        if parsed.hostname and parsed.hostname.lower() == "localhost":
            if not self.allow_localhost:
                return ValidationResult.from_error(
                    "Localhost URLs are not allowed",
                    code="localhost_not_allowed",
                    value=value,
                )
            return ValidationResult.success(url)
        
        # Check IP address
        if parsed.hostname:
            if self._is_ip_address(parsed.hostname):
                if not self.allow_ip:
                    return ValidationResult.from_error(
                        "IP addresses in URLs are not allowed",
                        code="ip_not_allowed",
                        value=value,
                    )
                return ValidationResult.success(url)
        
        # Check TLD
        if self.require_tld and parsed.hostname:
            if "." not in parsed.hostname:
                return ValidationResult.from_error(
                    "URL must have a top-level domain",
                    code="no_tld",
                    value=value,
                )
        
        return ValidationResult.success(url)
    
    def _is_ip_address(self, hostname: str) -> bool:
        """Check if hostname is an IP address."""
        import socket
        try:
            socket.inet_aton(hostname)
            return True
        except socket.error:
            pass
        try:
            socket.inet_pton(socket.AF_INET6, hostname.strip("[]"))
            return True
        except socket.error:
            pass
        return False


class PhoneValidator(BaseValidator[str]):
    """Validates phone numbers."""

    # E.164 format
    E164_PATTERN = re.compile(r"^\+[1-9]\d{1,14}$")
    
    # Common formats
    COMMON_PATTERNS: Dict[str, Pattern[str]] = {
        "us": re.compile(r"^(?:\+1)?[2-9]\d{2}[2-9]\d{6}$"),
        "international": re.compile(r"^\+?[1-9]\d{6,14}$"),
        "flexible": re.compile(r"^[\d\s\-\(\)\+\.]+$"),
    }

    def __init__(
        self,
        format: str = "international",  # "e164", "us", "international", "flexible"
        normalize: bool = True,
        country_code: Optional[str] = None,
    ):
        super().__init__(name="PhoneValidator", error_code="invalid_phone")
        self.format = format
        self.normalize = normalize
        self.country_code = country_code
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        phone = value.strip()
        
        # Normalize (remove common separators)
        if self.normalize:
            phone = re.sub(r"[\s\-\(\)\.]", "", phone)
        
        # Select pattern
        if self.format == "e164":
            pattern = self.E164_PATTERN
        else:
            pattern = self.COMMON_PATTERNS.get(self.format, self.COMMON_PATTERNS["flexible"])
        
        if not pattern.match(phone):
            return ValidationResult.from_error(
                f"Invalid phone number format (expected {self.format})",
                code="invalid_phone_format",
                value=value,
            )
        
        return ValidationResult.success(phone)


class IPAddressValidator(BaseValidator[str]):
    """Validates IP addresses."""

    def __init__(
        self,
        version: Optional[int] = None,  # 4, 6, or None for both
        allow_private: bool = True,
        allow_loopback: bool = True,
        allow_reserved: bool = True,
    ):
        super().__init__(name="IPAddressValidator", error_code="invalid_ip")
        self.version = version
        self.allow_private = allow_private
        self.allow_loopback = allow_loopback
        self.allow_reserved = allow_reserved
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        import ipaddress
        
        try:
            if self.version == 4:
                ip = ipaddress.IPv4Address(value)
            elif self.version == 6:
                ip = ipaddress.IPv6Address(value)
            else:
                ip = ipaddress.ip_address(value)
        except ValueError:
            return ValidationResult.from_error(
                "Invalid IP address format",
                code="invalid_ip_format",
                value=value,
            )
        
        # Check restrictions
        if not self.allow_loopback and ip.is_loopback:
            return ValidationResult.from_error(
                "Loopback addresses are not allowed",
                code="loopback_not_allowed",
                value=value,
            )
        
        if not self.allow_private and ip.is_private:
            return ValidationResult.from_error(
                "Private addresses are not allowed",
                code="private_not_allowed",
                value=value,
            )
        
        if not self.allow_reserved and ip.is_reserved:
            return ValidationResult.from_error(
                "Reserved addresses are not allowed",
                code="reserved_not_allowed",
                value=value,
            )
        
        return ValidationResult.success(str(ip))


class CreditCardValidator(BaseValidator[str]):
    """Validates credit card numbers using Luhn algorithm."""

    # Card type patterns
    CARD_PATTERNS: Dict[str, Pattern[str]] = {
        "visa": re.compile(r"^4[0-9]{12}(?:[0-9]{3})?$"),
        "mastercard": re.compile(r"^5[1-5][0-9]{14}$"),
        "amex": re.compile(r"^3[47][0-9]{13}$"),
        "discover": re.compile(r"^6(?:011|5[0-9]{2})[0-9]{12}$"),
        "jcb": re.compile(r"^(?:2131|1800|35\d{3})\d{11}$"),
        "diners": re.compile(r"^3(?:0[0-5]|[68][0-9])[0-9]{11}$"),
    }

    def __init__(
        self,
        allowed_types: Optional[Set[str]] = None,
        check_luhn: bool = True,
        normalize: bool = True,
    ):
        super().__init__(name="CreditCardValidator", error_code="invalid_credit_card")
        self.allowed_types = allowed_types
        self.check_luhn = check_luhn
        self.normalize = normalize
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        # Normalize (remove spaces and dashes)
        card_number = re.sub(r"[\s\-]", "", value)
        
        # Check if only digits
        if not card_number.isdigit():
            return ValidationResult.from_error(
                "Credit card number must contain only digits",
                code="invalid_characters",
                value=value,
            )
        
        # Check length
        if len(card_number) < 13 or len(card_number) > 19:
            return ValidationResult.from_error(
                "Invalid credit card number length",
                code="invalid_length",
                value=value,
            )
        
        # Identify card type
        card_type = self._identify_card_type(card_number)
        
        if self.allowed_types and card_type not in self.allowed_types:
            return ValidationResult.from_error(
                f"Card type '{card_type or 'unknown'}' is not allowed",
                code="card_type_not_allowed",
                value=value,
            )
        
        # Luhn check
        if self.check_luhn and not self._luhn_check(card_number):
            return ValidationResult.from_error(
                "Credit card number failed checksum validation",
                code="luhn_check_failed",
                value=value,
            )
        
        result = ValidationResult.success(card_number if self.normalize else value)
        result.metadata["card_type"] = card_type
        return result
    
    def _identify_card_type(self, number: str) -> Optional[str]:
        """Identify the card type from the number."""
        for card_type, pattern in self.CARD_PATTERNS.items():
            if pattern.match(number):
                return card_type
        return None
    
    def _luhn_check(self, number: str) -> bool:
        """Validate using Luhn algorithm."""
        digits = [int(d) for d in number]
        odd_digits = digits[-1::-2]
        even_digits = digits[-2::-2]
        
        total = sum(odd_digits)
        for d in even_digits:
            d = d * 2
            if d > 9:
                d = d - 9
            total += d
        
        return total % 10 == 0


class PasswordValidator(BaseValidator[str]):
    """Validates password strength."""

    def __init__(
        self,
        min_length: int = 8,
        max_length: int = 128,
        require_uppercase: bool = True,
        require_lowercase: bool = True,
        require_digit: bool = True,
        require_special: bool = True,
        special_chars: str = "!@#$%^&*()_+-=[]{}|;:,.<>?",
        disallow_common: bool = True,
        common_passwords: Optional[Set[str]] = None,
    ):
        super().__init__(name="PasswordValidator", error_code="weak_password")
        self.min_length = min_length
        self.max_length = max_length
        self.require_uppercase = require_uppercase
        self.require_lowercase = require_lowercase
        self.require_digit = require_digit
        self.require_special = require_special
        self.special_chars = special_chars
        self.disallow_common = disallow_common
        self.common_passwords = common_passwords or {
            "password", "123456", "12345678", "qwerty", "abc123",
            "monkey", "1234567", "letmein", "trustno1", "dragon",
            "baseball", "iloveyou", "master", "sunshine", "ashley",
        }
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        issues: List[ValidationIssue] = []
        
        # Length
        if len(value) < self.min_length:
            issues.append(ValidationIssue(
                message=f"Password must be at least {self.min_length} characters",
                code="password_too_short",
            ))
        
        if len(value) > self.max_length:
            issues.append(ValidationIssue(
                message=f"Password must not exceed {self.max_length} characters",
                code="password_too_long",
            ))
        
        # Character requirements
        if self.require_uppercase and not re.search(r"[A-Z]", value):
            issues.append(ValidationIssue(
                message="Password must contain at least one uppercase letter",
                code="missing_uppercase",
            ))
        
        if self.require_lowercase and not re.search(r"[a-z]", value):
            issues.append(ValidationIssue(
                message="Password must contain at least one lowercase letter",
                code="missing_lowercase",
            ))
        
        if self.require_digit and not re.search(r"\d", value):
            issues.append(ValidationIssue(
                message="Password must contain at least one digit",
                code="missing_digit",
            ))
        
        if self.require_special:
            special_pattern = f"[{re.escape(self.special_chars)}]"
            if not re.search(special_pattern, value):
                issues.append(ValidationIssue(
                    message="Password must contain at least one special character",
                    code="missing_special",
                ))
        
        # Common password check
        if self.disallow_common and value.lower() in self.common_passwords:
            issues.append(ValidationIssue(
                message="Password is too common",
                code="common_password",
            ))
        
        if issues:
            return ValidationResult.failure(issues, value=value)
        
        return ValidationResult.success(value)


class SlugValidator(BaseValidator[str]):
    """Validates URL slugs."""

    SLUG_PATTERN = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")

    def __init__(
        self,
        min_length: int = 1,
        max_length: int = 200,
        allow_numbers_only: bool = False,
    ):
        super().__init__(name="SlugValidator", error_code="invalid_slug")
        self.min_length = min_length
        self.max_length = max_length
        self.allow_numbers_only = allow_numbers_only
    
    def validate(
        self,
        value: Any,
        context: Optional[ValidationContext] = None,
    ) -> ValidationResult[str]:
        ctx = self._create_context(context)
        
        if not isinstance(value, str):
            return ValidationResult.from_error(
                f"Expected string, got {type(value).__name__}",
                code="type_error",
                value=value,
            )
        
        slug = value.lower()
        
        if len(slug) < self.min_length:
            return ValidationResult.from_error(
                f"Slug must be at least {self.min_length} characters",
                code="slug_too_short",
                value=value,
            )
        
        if len(slug) > self.max_length:
            return ValidationResult.from_error(
                f"Slug must not exceed {self.max_length} characters",
                code="slug_too_long",
                value=value,
            )
        
        if not self.SLUG_PATTERN.match(slug):
            return ValidationResult.from_error(
                "Invalid slug format (use lowercase letters, numbers, and hyphens)",
                code="invalid_slug_format",
                value=value,
            )
        
        if not self.allow_numbers_only and slug.replace("-", "").isdigit():
            return ValidationResult.from_error(
                "Slug cannot contain only numbers",
                code="slug_numbers_only",
                value=value,
            )
        
        return ValidationResult.success(slug)
