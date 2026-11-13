# Design

## Context

EF800Application stages packages in the MTP FWU directory and controls the keyboard over I²C on PB6/PB7. The keyboard application already has an application-level I²C interface at 7-bit address `0x2a`; retain it for application diagnostics and updater-only self-update. The dedicated updater uses address `0x48`. Each service has the same `VERSION` query but reports its own running image version. UART remains dedicated to debug-key input and scancode/event output. This design uses a custom updater in user Flash, not the factory system-ROM bootloader. The L431 has 256 KiB Flash with 2 KiB pages and 64 KiB SRAM.

## Goals / Non-Goals

**Goals:**
- Keep live keyboard status/version queries and the existing L496 MTP update path.
- Keep an independently executable updater available while the keyboard application or first-stage bootloader is being replaced.
- Stream update data directly into the selected fixed Flash destination; do not buffer a full image in keyboard RAM or a staging partition.
- Reuse familiar AN4221 I²C framing where practical while defining a bounded custom command set and flash policy.
- Preserve watchdog/recovery behavior in the small first-stage keyboard bootloader.

**Non-Goals:**
- Entering or programming through the STM32 factory ROM bootloader.
- Making the custom updater protocol wire-compatible with AN4221 or CubeProgrammer.
- Updating the updater while it is executing; updater self-update is performed by the keyboard application.

## Decisions

1. **Reserve a permanent updater partition.** Use bootloader `0x08000000..0x08007fff` (32 KiB), application `0x08008000..0x08037fff` (192 KiB), and updater `0x08038000..0x0803ffff` (32 KiB). Define fixed partitions in the board DTS and application linker configuration. Assert exact boundaries and ensure every image fits its region. The updater partition is never an updater write target.

2. **Keep service addresses and versions distinct.** The keyboard application service remains at `0x2a`; the updater service is at `0x48`. Both accept the `VERSION` query and return their own major/minor/patch version, so EF800Application can identify the responding image without relying on cached package metadata. The application service additionally handles status and updater-only update requests.

3. **Route updater entry through a short boot window.** On reset, the bootloader performs watchdog/reset-state handling, initializes an I²C target at `0x48`, and listens for EF800Application's address-only write probe for a bounded startup window (nominally 50 ms after it is ready). A probe selects the updater; otherwise a valid keyboard application starts normally. The keyboard application can also enter the updater directly through its I²C control path. Verify the controller/target driver's callback behavior for a zero-payload address transaction.

4. **Use one checked frame per I²C write transaction.** A normal request is `[command, bitwise-not(command), payload..., xor]`, with XOR of all preceding bytes as the final byte. Each frame ends with STOP; the host polls a separate read transaction until it gets ACK `0x79`, NACK `0x1f`, or BUSY `0x76`. A command frame with only the command pair has the XOR byte immediately after the pair. `VERSION` is the documented one-byte exception `0xf2`, returning five bytes `[0xf2, major, minor, patch, 0x06]`. Reject an inter-byte gap over 1000 ms and requests over 266 bytes.

   Reuse GET `0x00`, GET_ID `0x02`, READ_MEMORY `0x11`, WRITE_MEMORY `0x31`, and ERASE `0x44` codes. Add custom BEGIN `0xf3` (target:u8, image-size:u32le, image-CRC32:u32le), CHECK `0xf4` (target:u8), and RUN `0xf5` (no payload). ERASE payload is target:u8, mode:u8 (0=whole target partition, 1=one page), and page:u16le only for block mode. WRITE payload is target:u8, target-relative offset:u32le, data-length:u16le, then 1..256 data bytes. READ payload is target:u8, offset:u32le, length:u16le (1..256). All integer fields are little-endian. GET response is ACK, protocol-version:u8, command-count:u8, supported-command bytes, XOR. GET_ID is ACK, length:u8, device-ID big-endian bytes `04 35`, XOR. READ response is ACK, length:u16le, data, XOR. CHECK response is ACK, Flash-CRC32:u32le, vector-valid:u8, XOR. A successful command-only result is ACK followed by its XOR byte; NACK/BUSY are a single status byte.

   BEGIN selects a fixed target and declared image length/CRC. ERASE page IDs are absolute L431 pages checked against that target; partition erase iterates only its pages. WRITE offsets must be contiguous from zero, the active target must match, Flash programming is padded with `0xff` to 8-byte granularity, and CRC covers only the declared image bytes. READ/CHECK are bounded to the selected target. GET advertises supported custom commands. Do not require ROM sync `0x7f`, ROM identity, mass erase, or unrestricted memory access.

6. **Make flash destinations target-derived and restrictive.** The updater accepts only keyboard-application or keyboard-bootloader target IDs. The map is bootloader pages 0–15, application pages 16–111, and updater pages 112–127. Erase-partition expands to every page in the selected partition; erase-block accepts only page IDs inside it. Write addresses must be within the active target, in order, and aligned to the L431's 8-byte Flash programming granularity. Reject mass erase, updater-partition writes, arbitrary host ranges, gaps, overlaps, and writes before a valid erase/update session.

7. **Stream and verify without a full-image cache.** EF800Application validates the stored package header/target/length/CRC before starting. The updater validates the declared session and each command before erasing, then consumes bounded chunks directly into the selected Flash range while computing CRC. Final check/readback verifies the complete programmed image and vector table before success. A failed or interrupted transfer can leave its selected target partial; the independent updater remains intact for retry. The package source remains in EF800Application storage and can be retransmitted.

8. **Update the updater only from the keyboard application.** The keyboard application exposes the same framing at `0x48` with a strict updater-only target policy, allowing EF800Application to erase/write/check only `0x08038000..0x0803ffff`. The application verifies the installed updater before a later jump to it. Since the application, not the updater, performs this operation, the executing image is not erased in place.

9. **Keep bootloader responsibilities narrow.** The bootloader validates retained/watchdog state, starts or accounts for the watchdog according to existing recovery policy, chooses the updater on the probe, and otherwise launches a valid application. It contains no package receiver, image staging buffer, or flash image writer. Hardware tests must show the watchdog remains serviced during updater operations; the updater feeds an already-running IWDG as required.

10. **Keep host staging and diagnostics.** MTP remains the package staging interface. EF800Application reports protocol phase, selected target, erase/write/check progress, and errors. Probe-based SWD remains a controlled bootstrap/recovery route, not evidence of a qualified no-SWD updater.

## Risks / Trade-offs

- [The application partition shrinks by 32 KiB] -> Enforce the 192 KiB limit in linker configuration, package generation, and image verification.
- [A failed stream partially modifies its selected partition] -> Keep updater independent, make retries idempotent, validate package metadata before erase, and test every interruption point.
- [An updater self-update can be interrupted] -> Execute it from the keyboard application, verify it after writing, preserve normal app/bootloader execution, and allow the application to retry the updater-only target.
- [An address-only I²C probe may not produce the expected target callback] -> Prove this behavior on native tests and hardware before relying on the startup handoff; use a zero-length write/STOP sequence explicitly.
- [A custom implementation can diverge from AN4221 details] -> Treat AN4221 only as framing reference; publish custom command definitions and test transaction boundaries, timeouts, checksums, ACK/NACK/BUSY, and alignment.
- [The updater or application may miss an active IWDG deadline] -> Feed the watchdog during bounded erase/write/check loops and qualify the actual inherited-watchdog behavior on hardware.

## Migration Plan

1. Update the L431 partition map and linker/build targets; build and verify bootloader, application, and updater addresses and sizes.
2. Define the package target IDs and the custom AN4221-style protocol, including exact address-probe and timeout behavior; add protocol/parser tests before enabling Flash mutation.
3. Implement the updater app with explicit target-partition/page erase, direct chunk writes, check/readback, CRC, vector validation, watchdog service, and deterministic errors.
4. Implement bootloader update selection and keyboard-application updater-only self-update. Preserve normal application startup and fallback/retry paths.
5. Integrate EF800Application's MTP package runner, use the custom service at `0x48`, and retain fresh keyboard version/status diagnostics.
6. Run native package/protocol/partition/watchdog/interruption tests and pristine-build all keyboard images; verify the updated image contract and all L496 targets.
7. Bootstrap matching L431 images only after verifying the authorized probe reports device ID `0x435`. Test normal startup, address-probe entry, application and bootloader update, updater self-update, interrupted transfer recovery, and watchdog behavior.
8. Demonstrate package updates without SWD and record image hashes, traces, recovery outcomes, and remaining qualification limits. Keep MTP qualification distinct from successful host-side copy.

The package CRC detects transfer corruption but does not authenticate firmware. The build tasks verify the updater's actual size within 32 KiB and application size within 192 KiB; hardware tasks prove zero-data address-probe handling and startup timing before qualification.
