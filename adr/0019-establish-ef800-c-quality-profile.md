# Establish a Practical C Quality Profile for EF800-Owned Code

- Status: accepted
- Date: 2026-11-11
- Supersedes: None

## Context

EF800 firmware is implemented in C across multiple Zephyr images and shared modules. Future changes and a planned structural refactor need a consistent, useful coding-quality baseline. Zephyr, CMSIS, vendor, and generated code are not maintained by EF800 and should be identified separately from EF800-owned findings. Architecture-specific handoff and framework integration may need narrow, documented tool suppressions. No particular MISRA edition or formal compliance claim is required; tools should be selected for practical feedback and compatibility with the repository's actual Zephyr configurations.

## Decision

EF800-owned production C and EF800-authored C test code shall follow a documented project style and quality profile based on repository conventions, compiler diagnostics, and useful automated checks. Actionable findings shall be fixed or explicitly dispositioned. Existing findings may be baselined with scope and rationale; new findings shall not be silently introduced. Suppressions must be narrow, documented, and reviewable; broad or undocumented suppressions are not permitted.

The quality scope includes EF800-maintained C compiled into supported firmware images and their tests, including shared modules and EF800 board integration. Third-party and generated code must be explicitly identified separately from EF800-owned findings. Checks should use target-appropriate compiler options and source configuration where supported. Tool limitations and unanalyzed configurations must be reported; they must not be represented as successful analysis. Candidate tools require a practical target-aware pilot.

## Consequences

- New EF800-owned C code has a consistent, practical quality baseline, including tests.
- The repository needs maintained tool configuration, source ownership/exclusion inventory, finding baseline, and suppression record.
- Some framework and architecture-specific code may need narrowly scoped, reviewed tool suppressions.
- Quality claims are limited to analyzed owned sources and configurations; native simulation does not replace target validation.
- Cppcheck is selected for explicit EF800-source findings in configured compile databases; target compiler built-ins are captured from the real compiler, and third-party-only diagnostics are excluded from EF800 findings. The repeatable runner is wired into CI, but the current full run fails on existing EF800-owned findings whose dispositions/remediation remain in progress.
- CScout CC1/CC2/CC3 is required as an automated gate over explicit EF800-owned translation units with a reviewed baseline. Zephyr/vendor sources are not metric targets. The current pilot has not produced usable target metrics, so the CI integration remains open and must not be described as passing.
