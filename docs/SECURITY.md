# Security

Report vulnerabilities through GitHub private vulnerability reporting on
https://github.com/wandresrb/termo/security/advisories/new. Do not open a
public issue.

Issues in code that is unchanged from upstream tmux are also reported upstream
by us once a fix is ready; upstream's contact is in their own SECURITY.md.

Every build runs with AddressSanitizer and UndefinedBehaviorSanitizer in CI,
and the four external-input parsers (VT sequences, command syntax, formats,
styles) have libFuzzer harnesses under `tests/fuzz/`.
