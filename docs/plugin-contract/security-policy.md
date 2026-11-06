# EF800 Plugin Security Policy

This document is a release gate, not a claim that the current bring-up loader
is production-secure.

## Current Status

The physical EF800 MainApplication now runs compatible external packages in
the isolated unprivileged worker. It scans the normal `Plugins` directory and
the current MainApplication build discovered Elta3 and FTS3XX on the L496.
This is development bring-up, not a production security qualification: the
loader validates package structure, target ELF constraints, image CRC,
descriptor identity, service requirements, and bounded memory allocations,
but there is no package signature or anti-rollback check. CRC is integrity
detection, not authenticity. Do not treat arbitrary user-uploaded packages as
trusted or the image as a production release.

## STM32L496 Bring-up Evidence

On 2026-11-05, a dedicated test-only MainApplication image on the L496 ran
faulting Calculation packages in an unprivileged worker. Tested recoverable
cases included an undefined instruction, unauthorized read and write of the
malloc partition, execute-never fetch from plugin RW memory, an infinite loop
past the shortened Calculation deadline, and faults during Calculation create
and destroy. Each was attributed to its package, cleaned up, and quarantined; a
separate healthy Calculation package completed with a 19-byte result after
recovery. The worker's 2 KiB stack overflow produced an invalid stacked PC, so
attribution was rejected and the device followed the controlled reboot path
instead of claiming plugin-only recovery. Separate target fixtures sent a
reserved-bit malformed request and a stale-generation request; both were
rejected with `-EINVAL`, after which a valid same-sequence log request succeeded.
Measurement lifecycle and UART-route target cases remain untested. A subsequent
software reboot verified the boot-scoped registry reset and stable
inherited-IWDG feeding.

The standard worker-enabled MainApplication uses a 16 KiB allocator and a
2 KiB worker stack, bounds PluginManager to the two current physical packages,
and links at `0x08030000`. Its build uses 247,744 of 262,144 bytes of RAM
(94.5%); on the L496 it booted with the watchdog active and discovered package
IDs 2 and 3. Measurement invocation and UART recovery still require the
unavailable Elta3/FTS3XX instruments.

The two-package PluginManager capacity is an SRAM constraint, not a package
trust policy. Adding installed packages requires a fresh arena/RAM budget and
does not make unsigned packages authentic.

The separate recovery-test image scans a synthetic Shared Data directory and
uses the one-second Calculation deadline. It is not the normal MainApplication
configuration.

These are targeted bring-up observations, not the full fault/lifecycle/UART
acceptance matrix and not package-authenticity evidence. The normal
MainApplication now uses the worker, but production qualification and package
authenticity gates remain open.

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
- [x] MPU/W^X execution and non-executable data test on STM32L496.
- [x] Stack guard and overflow recovery test (unattributable stacking error escalates to controlled reboot).
- [x] Timeout and watchdog recovery test.
- [ ] Fault containment and resource-release test.
- [ ] Power-loss package replacement and rollback test.
- [ ] Anti-rollback test.

Native and bring-up tests may validate policy decisions and malformed inputs,
but they do not substitute for these target security gates.
