# keyboard-firmware-update Specification

## Purpose

Defines the keyboard's custom I²C update service, dedicated updater partition, and constrained recovery behavior for updating application and bootloader images.

## Requirements

### Requirement: Keyboard update service has a stable I²C entry
The keyboard application service SHALL remain at 7-bit I²C address `0x2a`; the dedicated updater service SHALL use 7-bit address `0x48`. The bootloader SHALL select the updater after an address-only probe to `0x48` during its bounded startup window; without that probe it SHALL continue normal boot.

#### Scenario: MainApplication requests updater after reset
- **WHEN** MainApplication sends an address-only write probe to `0x48` during the bootloader's update-entry window
- **THEN** the bootloader launches the updater and the updater responds at `0x48`

#### Scenario: No update probe is received
- **WHEN** the startup window expires without an update probe and a valid application exists
- **THEN** the bootloader follows its watchdog policy and starts the keyboard application

#### Scenario: Application requests updater directly
- **WHEN** MainApplication requests updater entry through the running keyboard application
- **THEN** the application transfers control to the verified updater without entering the factory ROM bootloader

### Requirement: Application and updater report their own version
Each service SHALL support the one-byte `VERSION` command `0xf2` at its own I²C address and return five bytes: `0xf2`, major, minor, patch, ACK (`0x06`). The keyboard application service uses `0x2a`; the updater service uses `0x48`. A version response from one address SHALL NOT be presented as the version of the other image.

#### Scenario: Keyboard application version is queried
- **WHEN** MainApplication sends `VERSION` to the keyboard application's `0x2a` service
- **THEN** the response identifies the keyboard-application version

#### Scenario: Updater version is queried
- **WHEN** MainApplication sends `VERSION` to the updater's `0x48` service
- **THEN** the response identifies the updater-application version

#### Scenario: Version query has no valid response
- **WHEN** a service does not respond with a complete valid `VERSION` response
- **THEN** MainApplication reports that service version as unavailable and does not substitute a cached or other-image version

### Requirement: Updater command framing is bounded and checked
The updater SHALL use one checked write frame per I²C transaction and expose distinct ACK, NACK, and BUSY statuses. It SHALL bound request size and inter-byte time, and SHALL NOT require factory-ROM synchronization or claim factory-ROM compatibility.

#### Scenario: Valid and invalid checked frames
- **WHEN** the updater receives a command frame
- **THEN** it accepts only a command/complement pair and XOR checksum that validate, and rejects a frame over 266 bytes or with an inter-byte gap over 1000 ms

#### Scenario: Status polling and version exception
- **WHEN** the host polls after STOP or sends the one-byte `VERSION` command `0xf2`
- **THEN** the updater returns ACK `0x79`, NACK `0x1f`, or BUSY `0x76`, while `VERSION` returns `[0xf2, major, minor, patch, 0x06]`

### Requirement: Updater command payloads are fixed and bounded
The updater SHALL support GET `0x00`, GET_ID `0x02`, READ `0x11`, WRITE `0x31`, ERASE `0x44`, BEGIN `0xf3`, CHECK `0xf4`, and RUN `0xf5`. BEGIN carries target, image size, and CRC32; CHECK carries target; RUN has no payload and requires a successful CHECK. Multi-byte fields SHALL be little-endian except GET_ID bytes `04 35`.

#### Scenario: Bounded image operations
- **WHEN** the host sends BEGIN, ERASE, WRITE, READ, CHECK, or RUN
- **THEN** each command is decoded using its command-specific fixed-length fields, and WRITE/READ data lengths are between 1 and 256 bytes

#### Scenario: Read and check response
- **WHEN** READ or CHECK completes
- **THEN** READ returns ACK, length:u16le, data, XOR, and CHECK returns ACK, Flash-CRC32:u32le, vector-valid:u8, XOR

#### Scenario: Valid command frame
- **WHEN** the updater receives a supported command and frame with valid complement and checksum
- **THEN** it accepts the frame and returns the command's defined response

#### Scenario: Malformed or timed-out frame
- **WHEN** a frame has an invalid complement/checksum or exceeds its inter-frame timeout
- **THEN** the updater rejects it without starting an erase or write operation

### Requirement: Keyboard updater partition layout is fixed
The L431 image layout SHALL reserve bootloader `0x08000000..0x08007fff`, application `0x08008000..0x08037fff`, and updater `0x08038000..0x0803ffff`. Each image SHALL fit its assigned partition, and update requests SHALL NOT write outside their authorized target.

#### Scenario: Images fit the revised partition map
- **WHEN** keyboard images are built and package limits are checked
- **THEN** bootloader, application, and updater load addresses and binary sizes fit their respective 32/192/32 KiB regions

#### Scenario: Address crosses a target boundary
- **WHEN** a write or erase request crosses the selected partition boundary or targets an unauthorized region
- **THEN** the updater rejects it without modifying the out-of-range region

### Requirement: Updater explicitly erases and directly programs selected targets
The updater SHALL accept explicit erase-partition or erase-block requests for only the keyboard application or bootloader, then program bounded image chunks directly to that target. It SHALL enforce 2 KiB erase-page and 8-byte programming alignment and SHALL NOT cache the complete image or write its own partition.

#### Scenario: Valid application or bootloader update
- **WHEN** a valid session selects an authorized target, erases only its mapped pages, and sends aligned image chunks
- **THEN** the updater writes only within that target and reports progress/result for each operation

#### Scenario: Updater self-write is attempted
- **WHEN** the updater receives an erase or write request targeting its own partition
- **THEN** it rejects the request without changing the updater image

### Requirement: Update package integrity is checked before and after programming
MainApplication SHALL validate package metadata, target, size, and source CRC before transfer. The updater SHALL validate session bounds, calculate integrity while streaming, and provide a final Flash check/readback result; success SHALL NOT be reported unless the complete programmed image matches the declared integrity and its vector table is valid.

#### Scenario: Complete image passes checks
- **WHEN** all chunks are written and the final Flash CRC/readback and vector checks pass
- **THEN** MainApplication reports success and requests startup of the verified target

#### Scenario: CRC or readback fails
- **WHEN** streamed data or Flash readback differs from the declared image
- **THEN** the update is reported as failed and the independent updater remains available for retry

### Requirement: Keyboard application updates only the updater partition
The keyboard application SHALL implement the custom update framing with a restricted policy that permits erase/write/check operations only for the updater partition. The application SHALL verify the installed updater before jumping to it.

#### Scenario: Updater image is refreshed from the application
- **WHEN** MainApplication selects the updater target while the keyboard application is servicing the update protocol
- **THEN** the application updates and checks only `0x08038000..0x0803ffff` and does not erase its executing application image

#### Scenario: Invalid updater image is supplied
- **WHEN** updater package validation, write, or final verification fails
- **THEN** the application does not jump to the invalid updater and remains available for a retry

### Requirement: Interrupted target updates remain recoverable
The updater SHALL remain independent of the application and bootloader targets it programs. An interrupted target write MAY leave that target partial, but SHALL NOT corrupt the updater partition, and the host SHALL be able to retry through the updater-entry path.

#### Scenario: Application or bootloader transfer is interrupted
- **WHEN** power or I²C communication is lost while the updater writes the application or bootloader
- **THEN** the target may be partial, but MainApplication can re-enter the intact updater and retry the selected target

#### Scenario: Updater refresh is interrupted
- **WHEN** power or I²C communication is lost while the keyboard application writes the updater partition
- **THEN** the application and watchdog bootloader remain intact and the application can retry the updater-only update

### Requirement: Keyboard bootloader preserves watchdog and normal-boot behavior
The keyboard bootloader SHALL handle watchdog/recovery state, select the updater on the update probe, and otherwise launch a valid application. It SHALL NOT receive packages or write image data.

#### Scenario: Normal boot
- **WHEN** no updater probe is received and the application is valid
- **THEN** the bootloader applies its watchdog policy and starts the application

#### Scenario: Updater requested
- **WHEN** the address-only updater probe is received in the startup window
- **THEN** the bootloader transfers control to the updater without launching the application

#### Scenario: Application is invalid
- **WHEN** watchdog/recovery policy determines the application cannot safely start
- **THEN** the bootloader preserves a recovery route to the valid updater or reports a defined recovery failure without attempting an invalid application jump
