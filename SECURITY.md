# Security Policy

DEUS is an open source project and is currently experimental. Security reports
are welcome for issues affecting the compiler, parser, bytecode format, VM,
host boundary, CLI, or tooling.

Relevant examples include:

- memory corruption, bounds bypasses, or concurrency memory bugs;
- bytecode validation bypasses, VM escape, or capability-boundary bypasses;
- unauthorized filesystem or process access, raw FFI, or unsafe host interaction;
- crashes or exploitable behavior caused by malformed source or `.deusb` input;
- denial of service that bypasses documented execution or resource limits.

The intended security properties include bounded execution, validated bytecode,
explicit capabilities, no arbitrary filesystem access, no arbitrary process
spawning, no raw FFI, no raw pointers exposed to DEUS programs, and no
unrestricted threads. These are intended properties, not a guarantee of
security; violations should be reported as security issues.

## Private reporting

Please report vulnerabilities privately by email to:

- [deuslang@gmail.com](mailto:deuslang@gmail.com)
- [devreskyume@gmail.com](mailto:devreskyume@gmail.com)

Do not publish sensitive details in a public GitHub Issue. Include the affected
version or commit, platform, reproduction steps, and impact when safe to do so.
Please allow maintainers reasonable time to investigate before public
disclosure.

A private disclosure workflow should be reviewed before each stable release.
