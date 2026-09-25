# Security

Report vulnerabilities through GitHub private vulnerability reporting on
https://github.com/wandresrb/termo/security/advisories/new. Do not open a
public issue. Private vulnerability reporting is enabled on the repository, so
the advisory stays between you and the maintainer until a fix is out.

Issues in code that is unchanged from upstream tmux are also reported upstream
by us once a fix is ready; upstream's contact is in their own SECURITY.md.

Every build runs with AddressSanitizer and UndefinedBehaviorSanitizer in CI,
the four external-input parsers (VT sequences, command syntax, formats, styles)
have libFuzzer harnesses under `tests/fuzz/` that nightly runs, and CodeQL,
OpenSSF Scorecard and zizmor report to the Security tab (`docs/ci.md`).
