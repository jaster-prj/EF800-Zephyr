# EF800 Plugin Security Policy

This document is a release gate, not a claim that the current bring-up loader
is production-secure.

## Current Status

User-uploaded executable plugins are **not release-enabled**. The current
development loader validates package structure, target ELF constraints, image
CRC, descriptor identity, service requirements, and bounded memory allocations.
CRC is integrity detection, not authenticity.

## Required Release Controls

### Authenticity

- Packages MUST carry a cryptographic signature over the manifest, descriptor,
  extension metadata, and image.
- The device MUST verify the signature against a provisioned trust anchor before
  loading or executing plugin code.
- Unknown signing keys, revoked keys, altered packages, and unsigned packages
  MUST be rejected before allocation or execution.
- Package verification failures MUST leave the previous valid package and
  application state untouched.

### Target and Memory Isolation

- Application UUID, interface version, target architecture, image size, RAM
  size, stack size, relocations, and descriptor identity MUST be validated.
- Plugin text MUST execute from a dedicated non-writable region.
- Plugin data MUST be non-executable.
- MPU/W^X configuration MUST be enabled and verified on STM32L496 hardware.
- Stack bounds and guard behavior MUST be measured and tested.
- The current native/bring-up RAM loader with MPU disabled MUST NOT be used as
  release evidence.

### Runtime Limits

- Plugin calls MUST execute under a host-defined timeout or watchdog budget.
- A timeout, invalid return, stack violation, memory fault, or illegal access
  MUST terminate the plugin session and release host resources.
- Plugins MUST NOT create threads, access arbitrary files, or retain host
  resource handles after destruction.

### Update and Rollback

- Package replacement MUST be atomic and power-loss safe.
- The last known-good package/assignment state MUST remain recoverable.
- Failed validation or activation MUST not delete a working package.
- Anti-rollback policy and version monotonicity MUST be defined before signed
  packages are enabled.

## Verification Checklist

The release gate is closed until all of these have target evidence:

- [ ] Signed package generation and signature verification.
- [ ] Altered, unsigned, wrong-key, and revoked-package rejection.
- [ ] MPU/W^X execution and non-executable data test on STM32L496.
- [ ] Stack guard and overflow recovery test.
- [ ] Timeout and watchdog recovery test.
- [ ] Fault containment and resource-release test.
- [ ] Power-loss package replacement and rollback test.
- [ ] Anti-rollback test.

Native and bring-up tests may validate policy decisions and malformed inputs,
but they do not substitute for these target security gates.
