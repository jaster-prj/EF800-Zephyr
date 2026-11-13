# Firmware Update Hardware Validation

Date: 2026-11-08  
Host: Linux, GVFS `gphoto2` MTP backend  
Target: EF800 Main controller, STM32L496xx (device ID `0x461`)  
Probe: ST-LINK `066FFF504982494867074032`

## Images and tests

The probe identified the STM32L496xx before each SWD operation. The reported
Programmer Voltage was `0.03 V`; per the hardware bring-up instructions, it was
not used as a target power indication. The PCB remained operational and
enumerated as USB device `2fe3:ef87`.

`application/scripts/verify-images.sh` passed:

| Image | Linked address | Size | SHA-256 |
| --- | ---: | ---: | --- |
| Bootloader | `0x08000000` | 61,800 / 65,536 bytes | `785f8f8c190f5a0f25ad6fe1b23d797ad10da3f3a58c81865e20e54a082c14e2` |
| BackupApplication | `0x08010000` | 103,404 / 131,072 bytes | Not recorded in this pass |
| EF800Application (latest diagnostic build) | `0x08030000` | 391,404 / 851,968 bytes | `82ed573b50216ddfc166564ae0ab5a6bc2a7b5017ac6ea04a1ba5576bcd4d7ee` |
| Keyboard application | `0x08008000` | 22,752 / 262,144 bytes | Not recorded in this pass |

At the time of the hardware test, the native Twister suite passed 25
configurations and 165 test cases.

## EF800Application MTP update

The MTP host-side transfer of `EF800_FW.bin` was verified against the local
EF800Application SHA-256 above. UART4 capture after reset showed Bootloader
console initialization (`rc=0`), filesystem mount, image/vector validation,
Main partition erase, program/readback, image verification, cleanup, and final
update result `rc=0`. EF800Application then started. The staged FWU file was
consumed, and a read of `0x08030000` showed a valid vector table.

This is evidence of a successful device-side EF800Application update, not only
an MTP upload.

## Keyboard version and package update

The live `keyboard version` query returned **1.1.1**. Instrumented UART logs
showed the transmitted `0xF1` request and the complete response bytes
`A0 F1 01 01 01`. After the keyboard package attempt, the controller was
recovered by a normal EF800Application reset and again returned version 1.1.1.
Keyboard key-input operation was not separately exercised in this pass.
The first post-update query attempts had timed out; after a EF800Application
reset, startup reported the keyboard-ready byte and the instrumented query
captured the complete response. This makes the earlier timeout intermittent;
it does not explain or qualify the later package-update handshake failure.

The keyboard package was 22,784 bytes, targeted STM32L431 (`0x0435`) at
`0x08008000`, declared version 1.1.1, and passed its CRC check. Its SHA-256 was
`cf5dc28f38be7bffc19932d5e7495504d680a3114c96b28c7d0565be871d535b`. GVFS
reported the same size and hash after staging `EF800_KEYBOARD.pkg` over MTP.

The first device-side attempt failed with `-116` (`ETIMEDOUT`) while awaiting
the application-protocol bootloader response. A diagnostic EF800Application
build was then deployed through the verified MTP/Bootloader path and the
package retry was repeated. The received bytes were `A0 7F AA`, an incomplete/
mixed application response. EF800Application then sent STM32 system-ROM sync
`0x7F` and received `0xA0`, not the ROM ACK `0x79`; ROM entry was therefore
not confirmed. No ROM identification, erase, or programming command was
issued. The package was
removed from the device to prevent automatic retries and retained locally.
No keyboard firmware was flashed over SWD. After a normal L496 shell reboot
with the package absent, keyboard startup (`0xAA`) and the 1.1.1 version query
succeeded again.

## Result

The EF800Application MTP update path passed on hardware. The baseline keyboard
version diagnostic passed, but the previous keyboard package installation
attempt failed before any erase or write because it did not establish system
ROM entry. The system-ROM implementation has since been replaced in source by
the resident keyboard-loader protocol described below. No claim is made that
the replacement path or normal physical key input has been qualified on
hardware.

## Current custom I²C updater software/build evidence

The source now implements three fixed L431 regions: bootloader at
`0x08000000..0x08007fff`, keyboard application at
`0x08008000..0x08037fff`, and resident updater at
`0x08038000..0x0803ffff`. The keyboard application keeps its diagnostic and
updater-self-update service at I²C `0x2a`; the independent updater uses `0x48`
and is authorized to erase/program only application or bootloader. The
bootloader listens during a bounded startup interval for an address-only `0x48`
probe and otherwise starts a valid application (or uses updater recovery if
the application is invalid). EF800Application validates staged package metadata,
CRC and vectors, selects the service by package kind, streams bounded chunks,
and asks the serving image to verify Flash CRC/vector state before run or
updater handoff. This is implementation/build evidence only; no I²C transaction
or update has been qualified on the physical L431.

Pristine Zephyr v4.4.2 image records from the current software pass:

| Image | Linked address / partition | Binary size | SHA-256 |
| --- | --- | ---: | --- |
| Keyboard bootloader | `0x08000000` / 32 KiB | 24,084 B | `4fc33cae58017f4a9af8f311f8bf119636979177949128ca82999e9fd74b4308` |
| Keyboard application | `0x08008000` / 192 KiB | 29,512 B | `fa2a14a38d7a73073bd346d829c272da1df023dad46dcdc4ba794f9840699514` |
| Keyboard updater | `0x08038000` / 32 KiB | 23,488 B | `b20fe267ce29ed51aea0d86f60b87c1a76dfdd6c3d4aee7c26f0581868cdac44` |
| EF800Application | `0x08000000` / 832 KiB | 390,264 B | `0c94751985de9ebdd94a55fd99185427d8331e6d8489647e0542275652f261e6` |

Locally generated format-v2 package artifacts (not staged over MTP):

| Package | Size | Declared version | SHA-256 |
| --- | ---: | --- | --- |
| Keyboard application | 29,544 B | 1.1.1 | `c6f71865fc9983c3c964584b3a3bfc373c138fdab7a87a04c415426c6a51e811` |
| Keyboard bootloader | 24,116 B | 1.1.1 (package metadata only) | `65e61e2087da7fd8f5648991051a966ce29c36a26b0894a78585de7d65c20acb` |
| Keyboard updater | 23,520 B | 1.0.0 | `e3f548e4831ae40ac087750ff3312e9f9c0158558547fbc4ccdf9bff6988a180` |

### L431 SWD bootstrap (2026-11-07)

The user confirmed that the NUCLEO-F767ZI Programmer's ST-LINK is connected to
the keyboard-controller SWD. The authorized keyboard probe serial was
`0672FF504955857567234934`. Before each separate programming operation,
STM32CubeProgrammer v2.23.0 connected under reset and reported device ID
`0x435`, device name `STM32L43xxx/STM32L44xxx`, NVM size 256 KiB, and 3.25 V.
The `NUCLEO-F767ZI` board label describes the probe/carrier; the detected target
was the L431. Each operation completed its download and verify successfully:

| Image | Address | Size | Result |
| --- | ---: | ---: | --- |
| Keyboard bootloader | `0x08000000` | 24,084 B | Download verified successfully; reset |
| Keyboard application | `0x08008000` | 29,512 B | Download verified successfully; reset |
| Keyboard updater | `0x08038000` | 23,488 B | Download verified successfully; reset |

The programmed binaries match the SHA-256 values in the table above. These
three SWD writes establish the bootstrap only; updater entry/protocol,
erase/write/check via EF800Application, keyboard UART behavior, interruption
recovery, and no-SWD updating have not yet passed hardware qualification.
Probe identity and target connection must still be checked before any later
SWD write.

After the final reset, the EF800Application UART4 shell responded at 115200 8N1
on `/dev/ttyUSB0`. `keyboard version` returned `Keyboard firmware version
1.1.1`, confirming a normal application-level I²C version transaction after
the SWD bootstrap. This does not exercise key input/output or updater entry.
The documented virtual-key exercise also passed over that UART: `keyboard
debug start`, `keyboard debug press a`, `keyboard debug release a`, and
`keyboard debug stop` returned their expected shell prompts; EF800Application
logged `Keyboard key: 0x61` for the make event. This validates an application
key event over the keyboard UART, not physical matrix input or raw UART
electrical timing.
An attempt to stage the application package with `keyboard upload` returned
`keyboard upload unavailable: storage busy`; `usbmsd get` reported USBMSD0,
and the EF800 USB/MTP device was not enumerated on the host. No package was
staged and no runtime erase/write/update command was issued. QSPI formatting
was not attempted. Custom-updater protocol and update testing therefore remain
blocked until the package is available through mounted storage/MTP.

The full native Twister suite currently passes 26 configurations / 174 cases;
the focused keyboard-update suite passes 8/8 cases. These tests cover framing,
range/authorization policy, malformed packages, interrupted software stream
state and retry reset. They do not execute target I²C callbacks, Flash erase or
programming, watchdog timing, reset handoff, MTP staging, or recovery after an
actual interrupted write. CRC/XOR checks detect accidental corruption but do
not authenticate firmware; signed packages are not implemented.

The previous factory-ROM attempt and its failed `A0 7F AA` / non-ACK evidence
above remain historical only. No factory-ROM erase/write command was issued,
and that route is superseded by the custom resident updater.

### MTP restoration and custom-updater retry (2026-11-08)

After the authorized full 4 MiB QSPI format, EF800Application reported both FAT
boot sectors verified and COMPOSITION seeded. Formatting erased Configuration,
including the Port 4 assignment. The assignment was restored through the port
UI as builder 6 / connection 8 (USB MTP); EF800Application then enumerated as
`2fe3:ef87`. MTP storage IDs `0x00010001`, `0x00010002`, and `0x00010003` map
to Data, Plugins, and FWU. Zero-byte `.data`, `.plugins`, and `.fmu` markers
were created in those respective roots. The 29,544-byte
`EF800_KEYBOARD.pkg` was staged in FWU and read back byte-for-byte with SHA-256
`c6f71865fc9983c3c964584b3a3bfc373c138fdab7a87a04c415426c6a51e811`.

The updated EF800Application image (390,264 bytes, SHA-256
`0c94751985de9ebdd94a55fd99185427d8331e6d8489647e0542275652f261e6`) was
staged as `EF800_FW.bin`, read back byte-for-byte, and installed through the
existing EF800Application Bootloader path. UART logs confirmed validation,
Main partition erase, program/readback, image verification, cleanup, and
`EF800Application update rc=0`. On the next startup, the custom keyboard updater
again logged `resident updater handoff failed: -5`, followed by update failure
`-5`; no identify, erase, or write phase was logged. A subsequent
`keyboard version` request returned 1.1.1. The package remains in FWU for
diagnosis/retry. Thus MTP staging and EF800Application update passed, but custom
L431 updater entry still fails before any keyboard Flash operation; application
update, recovery, and interruption behavior are not qualified.

The follow-up source fix defers the keyboard application's direct updater
chainload by 20 ms after the acknowledged I²C transaction STOP, allowing the
EF800Application controller to finish `write_read` before the service
unregisters. The chainload now also leaves interrupts disabled for updater
startup and stays in the application if updater-vector validation or I²C target
release fails. This fix builds, but has not yet been flashed or validated on the
L431; the observed `-5` remains the last hardware result.

### Updater-entry path retest (2026-11-08)

The direct application-to-updater route was retested with the L431 application
at `0x08008000`, the diagnostic EF800Application using separate updater
command/read I²C transactions, and a 29,568-byte application package
(`1.1.1`, SHA-256
`83896d3d24505a873c84d6cba5f82ccb73c3d0bf8413e7f25dc3f08b405fae44`). The
package was read back over MTP byte-for-byte before use. On cold EF800Application
startup, UART logs showed the complete 29,536-byte payload streamed and
`keyboard update: transfer and verification complete`. Afterward,
`keyboard version` again returned 1.1.1. This confirms the direct application
handoff, updater transfer, and device-side verification path on hardware; the
version is unchanged because the package intentionally contains the same
version. The package was consumed by the successful run.

The independent bootloader fallback remains unconfirmed. A rebuilt 24,240-byte
bootloader was programmed and verified at `0x08000000` after the explicit L431
probe reported `0x435` / STM32L43xxx. The `keyboard boot-probe` diagnostic
hardware-reset the keyboard, observed an address-only transfer at `0x48`, but
did not receive a valid updater `VERSION` response (`-5`). The diagnostic was
then changed to issue an actual one-byte I²C write containing `0x00` to `0x48`
inside the boot window. After 12 attempts, the controller reported a successful
write, but updater `VERSION` again failed with `-5`. A later `keyboard version`
query returned 1.1.1 after the normal application finished starting. Neither
the address-only transfer nor the one-byte zero write demonstrated updater
entry; no fallback identify/erase/write was observed or attempted. The
bootloader callback/probe timing and interpretation of the zero byte still need
diagnosis; the revised candidate's probe behavior is not qualified.

Overall, only the direct application-to-updater path passed this retest. The
bootloader `0x48` fallback, bootloader-target update, updater self-update, and
interruption/recovery scenarios remain unqualified. The fallback candidate
bootloader remains installed on the L431; normal application boot and its
1.1.1 version query were confirmed after the failed zero-byte test.

Follow-up isolation narrowed the problem to probe detection rather than updater
image validity or the bootloader's image branch. A temporary, diagnostic-only
bootloader that unconditionally selected the updater was programmed and the
EF800Application `keyboard updater-version` command returned **1.0.0**. That
diagnostic image was then replaced with the probe-based bootloader; normal
application boot and version 1.1.1 were confirmed afterward. A temporary UART
trace build of the probe-based bootloader reported that its I²C probe window
started and expired, with no `write_requested`/address-match trace while the
EF800Application's one-byte `0x00` attempt returned `-5`. The source now latches
the probe directly from `write_requested` rather than waiting for STOP, but that
latest source change has not passed a hardware probe test. Thus the updater jump
and updater service are independently proven; the controller-to-bootloader
probe reaching the target callback remains the blocker. No updater erase/write
command was issued during this isolation.

References: [ST AN4555, STM32L4 bootloader interfaces](https://www.st.com/resource/en/application_note/an4555-getting-started-with-stm32l4-series-and-stm32l4-series-hardware-development-stmicroelectronics.pdf)
and [ST AN2606, system-memory boot mode](https://www.st.com/resource/en/application_note/an2606-introduction-to-system-memory-boot-mode-on-stm32-mcus-stmicroelectronics.pdf).

### JP7 GPIO-strapped updater entry

The I²C probe fallback above was superseded by the JP7 strap. EF800Application
PE0 is configured inactive during ordinary startup and driven low only while
resetting the L431; keyboard PB12 is configured with an internal pull-up and
sampled by the bootloader before its I²C probe window. With JP7 connected, the
new bootloader was programmed to the explicitly identified L431 (`0x435`) and
verified at `0x08000000`. The matching EF800Application image was staged via MTP,
read back byte-for-byte (391,900 bytes; SHA-256
`c687dd75a2204d2e6f2642a19b4d35d170ec9f27a17a24b36e9a2c91d0ee1309`), and
installed with `EF800Application update rc=0`. `keyboard boot-probe` then
reported **Bootloader strap PASS; updater version 1.0.0**. After a cold reboot
with PE0 inactive, the normal keyboard application responded with version
**1.1.1**. This qualifies the JP7 strap handoff and ordinary cold boot on this
hardware; updater erase/write and the remaining update/recovery scenarios are
still untested.

### Keyboard application update through the resident updater

The current keyboard application source was rebuilt for the L431 at
`0x08008000` (29,536-byte payload, SHA-256
`bdfce242d65e3c9abc831323b77b0e749e514386dff8b95ae72107595809ab9c`). A
format-v2 application package for target `0x0435`, declared version 1.1.1,
passed header and payload-CRC validation (29,568 bytes, SHA-256
`3fcf662daac5e053d239a0ed9539d2786391e9b9bef3200010c25cbabb886f9a`) and was
read back byte-for-byte after MTP staging in the FWU storage root. The first
cold reboot did not find it because it had initially been placed in an
extra-nested directory; that attempt performed no keyboard Flash operation.
After staging at the FWU root, cold startup streamed all 29,536 bytes and logged
`keyboard update: transfer and verification complete`. The keyboard then
responded `Keyboard firmware version 1.1.1`. Reading the package back afterward
failed as expected because the updater flow removed it; GVFS's object listing
was stale. This validates application-slot erase/program/check/run via the
resident updater. The package redeployed the existing version, so this confirms
the current build and update path, not a version-number increase.

### Bootloader update and updater-slot update

A bootloader-kind package (target `0x0435`, base `0x08000000`, 25,316-byte
payload; package SHA-256
`a5da291f5850cc429396138fee7659e0e9fa49ce610e13e65b7acaddaedddaba`) was
staged/read back byte-for-byte. EF800Application streamed the payload and logged
`keyboard update: transfer and verification complete`. The keyboard application
then booted and returned version 1.1.1; `keyboard boot-probe` also reached
updater 1.0.0 over JP7, confirming that the newly written resident bootloader
starts and selects the updater strap correctly.

The updater-kind package (target `0x0435`, base `0x08038000`, 23,488-byte
payload; package SHA-256
`e3f548e4831ae40ac087750ff3312e9f9c0158558547fbc4ccdf9bff6988a180`) was
staged/read back byte-for-byte. An early attempt failed at identify before any
Flash command. After gating the application service on the keyboard's UART
ready byte, a later attempt streamed all 23,488 bytes and passed device-side
Flash CHECK, but then timed out querying the updater after a direct app
chainload. The staged package was removed before reboot; a JP7 boot-probe
confirmed the updater slot still booted version 1.0.0.

The post-CHECK handoff was changed to reset through the resident bootloader
with JP7 asserted, ensuring a clean updater startup rather than branching from
the app with stale I²C peripheral state. The retry streamed all 23,488 bytes,
logged `keyboard updater fallback: PB12 strap booted updater 1.0.0`, and ended
with `keyboard update: transfer and verification complete`. EF800Application
removed the package on success. A subsequent cold boot returned the keyboard to
the normal app and the live query reported **1.1.3**.

To exercise the other update direction with a strictly increased version, the
resident updater installed keyboard application 1.1.3 (29,540-byte payload,
package SHA-256
`7e008aedfdea7a014ab4c81ee7a364a1d3e5ca2c773053de78f7ce65ae36f228`). The
device streamed the full payload, verified Flash, logged completion, and
reported version 1.1.3 both immediately and after a cold boot. Thus, on this
hardware, application-to-updater-slot and resident-updater-to-application
erase/write/check paths have now both passed. Production qualification remains
incomplete.

### Interrupted-transfer retry and watchdog evidence (2026-11-08)

Five controlled interruption/reset tests were exercised. The application and
updater packages were read back byte-for-byte over MTP; the bootloader package
was accepted by the DebugConsole binary uploader after its package CRC/format
validation:

* During an application update, EF800Application was cold-reset after the first
  4 KiB write-progress boundary. The package remained staged; the restarted
  EF800Application began a fresh update session, completed the full stream,
  Flash CHECK/RUN, and the keyboard returned to normal startup at version
  1.1.3.
* In a second application update, EF800Application was reset when it logged
  `Flash CHECK started`. The retry again completed CHECK/RUN and normal
  keyboard startup at 1.1.3.
* During updater self-update, EF800Application was reset after the first 4 KiB
  boundary. The keyboard application remained available to service the retry;
  the restarted EF800Application rewrote and checked the updater image, JP7
  booted updater 1.0.0, and the package was removed after success. A subsequent
  cold reset returned to the normal 1.1.3 application.
* During application-partition erase, the keyboard was reset through the JP7
  boot-probe path. EF800Application reported `BEGIN/ERASE failed: -77`; JP7
  confirmed that resident updater 1.0.0 still booted. The retained package was
  then retried after a cold EF800Application reset, passed verification, and
  the keyboard returned to normal startup.
* During a bootloader-target update, EF800Application was cold-reset after the
  first 4 KiB write boundary. The next EF800Application boot could not enter the
  keyboard updater (`keyboard updater entry timed out`, `-5`): the partial
  first-stage bootloader prevented reaching the intact updater partition. The
  explicit L431 ST-LINK (`0672FF504955857567234934`, device ID `0x435`) then
  restored the known-good 25,316-byte bootloader image at `0x08000000`; CubeProgrammer
  verified the write. A full 32 KiB SWD readback matched that image followed by
  erased `0xff` bytes. JP7 again entered updater 1.0.0, and the retained
  bootloader package was retried through the updater to successful CHECK/RUN
  and normal keyboard startup. The live keyboard application remained 1.1.3.

These tests demonstrate recovery after interruption during erase, plus
EF800Application-reset interruption and retry at the application write, CHECK
boundary, and updater-self-update write paths. The bootloader-write test
demonstrates SWD recovery and subsequent updater retry, but also confirms that
the independent updater cannot recover a partially written first-stage
bootloader after reset; SWD restoration was necessary to make the updater
reachable again. Power loss in the middle of an individual Flash operation
was not simulated.

The updater self-update retry ran for more than the configured 10-second
keyboard IWDG interval without a keyboard reset; the keyboard application
services the watchdog in its erase/write loops. This is operational hardware
evidence of watchdog servicing during this update, not a fault-injected
watchdog-expiry test. The native suite passed 26/26 configurations and
174/174 cases after these changes.
