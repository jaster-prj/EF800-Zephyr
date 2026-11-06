# mtp-filesystem-access Specification

## Purpose

TBD - Define safe, bounded access to approved EF800 filesystems over MTP.

## Requirements

### Requirement: MTP exposes only approved filesystem roots
The system SHALL present `/EF800:/data`, `/EF800:/plugins`, and `/EF800:/fwu` as three distinct MTP storages. It SHALL NOT expose `/EF800:/.configuration`, the volume root, or any other filesystem path through MTP.

#### Scenario: Host enumerates EF800 storage
- **WHEN** a Windows or Linux host opens an MTP session
- **THEN** it can browse the Data, Plugins, and FWU storage roots and cannot browse private configuration or unrelated volume content

#### Scenario: Required directory is absent at startup
- **WHEN** FAT mounts successfully and one of the three approved directories does not exist
- **THEN** MainApplication creates that directory before MTP becomes available or reports MTP unavailable without exposing a broader fallback root

### Requirement: Each MTP storage has a bounded initial object capacity
The system SHALL support at most 12 simultaneously indexed files and directories in each exposed MTP storage for the initial release. Objects in one storage SHALL NOT consume the object capacity of another storage, and capacity exhaustion SHALL fail safely without corrupting FAT or returning a successful partial upload.

#### Scenario: Storage contains 12 objects
- **WHEN** an MTP session indexes 12 valid objects beneath one exposed root
- **THEN** all 12 objects are available to the host

#### Scenario: Storage exceeds 12 objects
- **WHEN** an exposed root contains or receives more objects than its configured capacity
- **THEN** the system reports a bounded error or diagnostic, preserves filesystem integrity, and does not represent an incomplete upload as successful

#### Scenario: Other storage has spare capacity
- **WHEN** Plugins reaches its 12-object capacity and Data remains below capacity
- **THEN** Data remains usable up to its independent capacity

### Requirement: MTP and MainApplication share mounted FAT safely
While MTP is selected, MainApplication SHALL retain filesystem ownership and MTP SHALL access QSPI only through Zephyr filesystem APIs. FAT operations from USB and application threads SHALL be serialized by a reentrant filesystem configuration, and raw MSC disk access SHALL remain inactive.

#### Scenario: Host reads while EF800 accesses another file
- **WHEN** the host downloads a completed data file while MainApplication accesses a different file
- **THEN** both operations use the mounted filesystem without unmounting FAT or exposing raw sectors

#### Scenario: Same object would be modified concurrently
- **WHEN** host and firmware operations would write or consume the same incomplete object concurrently
- **THEN** the system prevents consumption as a completed object and preserves a valid filesystem state

### Requirement: MTP session shutdown handles incomplete transfers
Switching away from MTP SHALL disconnect the host, cancel the current MTP session, close open transfer state, and remove an incomplete upload when runtime cleanup is possible. A subsequent MTP session SHALL rebuild its object view from the current filesystem state.

#### Scenario: User switches during upload
- **WHEN** an upload is in progress and the user selects None or USB DebugConsole
- **THEN** the transfer is cancelled, the USB context is shut down, and the incomplete open upload is not treated as a valid completed file

#### Scenario: Firmware creates a file during an existing session
- **WHEN** MainApplication completes a new file after the host's object table was built
- **THEN** a newly opened MTP session discovers the completed file without requiring an EF800 restart

### Requirement: Plugin uploads preserve startup-only activation
Packages uploaded to `/EF800:/plugins` SHALL NOT be loaded or hot-replaced in the running MainApplication. Existing package validation and discovery SHALL process them only through the startup package lifecycle.

#### Scenario: Host uploads a plugin
- **WHEN** the host completes a plugin package upload during an MTP session
- **THEN** the running plugin set is unchanged and the package is considered for validation and discovery on a subsequent startup

### Requirement: Update packages are validated before use
The system SHALL preserve `/EF800:/fwu/EF800_FW.bin` and `/EF800:/fwu/EF800_KEYBOARD.pkg` as the firmware and keyboard update paths. Bootloader and MainApplication SHALL reject incomplete, corrupt, or wrong-target packages and SHALL NOT program an update merely because an MTP object with the expected filename exists.

#### Scenario: Firmware upload is interrupted by power loss
- **WHEN** power is lost before `EF800_FW.bin` is completely transferred
- **THEN** the next boot validates and rejects the incomplete image without programming it

#### Scenario: Keyboard package is invalid
- **WHEN** `EF800_KEYBOARD.pkg` fails size, integrity, format, or target validation
- **THEN** MainApplication does not program the keyboard controller and reports the validation failure

### Requirement: MTP interoperates with supported host platforms
The MTP personality SHALL enumerate and transfer files with native Windows MTP support and representative Linux `libmtp` desktop integration. Qualification SHALL cover both transfer directions, nested directories, boundary-sized files, cancellation, disconnect, reconnect, and interrupted transfers on the physical STM32L496 target.

#### Scenario: Windows host transfers a file
- **WHEN** a supported Windows host copies a valid file to or from an exposed storage
- **THEN** the operation completes with identical file content and the device remains available for subsequent operations

#### Scenario: Linux host transfers a file
- **WHEN** a supported Linux host using `libmtp` through the qualified desktop integration copies a valid file to or from an exposed storage
- **THEN** the operation completes with identical file content and the device remains available for subsequent operations

#### Scenario: Host disconnects unexpectedly
- **WHEN** the USB cable is removed during or after a transfer
- **THEN** MainApplication continues running, FAT remains mountable and consistent, and a later connection can establish a new MTP session
