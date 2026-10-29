# virtual-ef800-platform Specification

## Purpose

Provide a behavioral EF800 simulation on Zephyr `native_sim` so MainApplication, display, keyboard, peripheral, and plugin-supervision behavior can be tested interactively and headlessly without physical EF800 hardware, while preserving the boundary between behavioral evidence and hardware qualification.

## Requirements

### Requirement: Run EF800 behavior as a native simulator application
The system SHALL provide a buildable and runnable Zephyr `native_sim` application that executes the production `ef800-main` composition root and its MainApplication domain, menu, plugin, synchronization, and thread-supervision behavior while substituting required physical EF800 devices with Zephyr-compatible emulated or simulated peripherals.

#### Scenario: Start the virtual EF800
- **WHEN** a developer builds and starts the virtual EF800 target
- **THEN** the application initializes without an STM32 MCU or physical EF800 and exposes its virtual front-panel interface

#### Scenario: Run concurrent plugin behavior
- **WHEN** MainApplication starts a plugin operation on a worker thread
- **THEN** the virtual target uses Zephyr scheduling and synchronization primitives and keeps the MainApplication UI path independently schedulable

### Requirement: Keep physical and native-sim production source composition aligned
The physical `ef800` and native-sim builds SHALL consume one shared production source manifest, with target differences limited to explicitly selected configuration, devicetree, and platform-adapter sources.

#### Scenario: A shared application source changes
- **WHEN** a production MainApplication source is added to the shared source manifest
- **THEN** both the physical EF800 and native-sim builds include that source without editing two application wrappers

#### Scenario: Native-only adapter is selected
- **WHEN** `application/ef800-main` is built for `native_sim`
- **THEN** native peripheral adapters are selected while physical STM32 adapters remain excluded from the native image

### Requirement: Present a live virtual EF800 LCD
The virtual EF800 SHALL maintain the observable state of a 240 x 64 monochrome EF800 display and SHALL publish complete, consistently framed display updates produced through the production T6963 driver operating against the emulated GPIO bus and configurable virtual T6963 device. The display framebuffer SHALL use one bit per pixel, and the virtual device SHALL perform text/graphics composition and dirty-region rendering before publishing a changed snapshot.

#### Scenario: MainApplication changes the display
- **WHEN** MainApplication writes or clears EF800 display content
- **THEN** the virtual front panel renders the resulting 240 x 64 state through the production T6963 path without requiring physical GPIO or a T6963 controller

#### Scenario: MainApplication changes only one display region
- **WHEN** a text cell, graphics byte, custom character, cursor, attribute, or display-mode state changes
- **THEN** the virtual T6963 model refreshes the affected packed one-bit pixels and preserves unrelated framebuffer pixels

#### Scenario: Client receives a partial display message
- **WHEN** a host client receives an incomplete or malformed display update
- **THEN** the client rejects that update and retains the last complete display state

### Requirement: Accept virtual keypad press and release input
The virtual EF800 SHALL expose the complete canonical EF800 user-facing key
set through distinct press and release operations. The set SHALL contain the
59 unique keys represented by the existing keyboard map: 54 occupied matrix
positions and 5 special keys. Duplicate aliases and unused raw matrix
positions SHALL NOT be exposed as separate user-facing controls. Accepted
input SHALL continue through the normal MainApplication keyboard translation,
parser, and application-handling path rather than directly mutating menu or
plugin state.

The browser front panel SHALL preserve held-key state for pointer/touch and
keyboard input, SHALL permit multiple simultaneous held keys, and SHALL expose
held state through an inverted visual treatment and an accessible pressed
state. Key state SHALL be released on the corresponding release event or by
release-all when control is released, the browser loses focus, the controller
disconnects, or the simulator connection is lost.

#### Scenario: User operates a canonical browser key
- **WHEN** the controlling user presses and releases any one of the 59
  canonical browser keys
- **THEN** the gateway sends the corresponding key-down and key-up operations
  through the normal simulator keyboard path and MainApplication receives the
  resulting make and break behavior

#### Scenario: User holds a virtual key
- **WHEN** the controlling user presses a browser key without releasing it
- **THEN** the key remains logically held, its browser control is rendered with
  inverted pressed styling and an accessible pressed state, and supported
  repeat behavior follows the virtual keyboard policy

#### Scenario: User holds multiple keys
- **WHEN** the controlling user holds a modifier and another canonical key at
  the same time
- **THEN** both keys remain held independently and the simulator receives the
  corresponding overlapping key-down operations until each key is released

#### Scenario: Front panel loses control with held keys
- **WHEN** the active controller releases control, loses browser focus, closes,
  disconnects, or loses the simulator connection while keys are held
- **THEN** the gateway sends release-all, clears its held-key set, and the
  browser clears all pressed styling and accessible pressed states

### Requirement: Support interactive and automated operation
The virtual EF800 SHALL support an interactive mode synchronized closely enough to wall-clock time for human input and a deterministic scriptable mode that can inject input, observe display state, control virtual peripherals, manage native-sim plugin packages, and assert application outcomes.

#### Scenario: Developer operates the front panel
- **WHEN** the virtual EF800 is started for manual use
- **THEN** display updates, debounce, key holds, plugin deadlines, package status, virtual-device state, and periodic status behavior progress at a human-usable real-time rate

#### Scenario: Automated scenario drives the device
- **WHEN** an automated client submits a declared sequence of key, package, restart, and peripheral events
- **THEN** it can observe deterministic acknowledgements and resulting display, plugin registry, device, or application state without GUI interaction

#### Scenario: Browser uploads a package
- **WHEN** the browser uploads a valid native-sim plugin package
- **THEN** the gateway reports installation and restart-required state without changing the currently running plugin registry

### Requirement: Connect the container simulation to a host front panel
The virtual EF800 SHALL use its documented, versioned binary transport between the dedicated simulator container and one web-gateway client on the private Compose network, and the gateway SHALL expose the interactive front panel to a localhost browser without graphical display forwarding or a host Python GUI process.

#### Scenario: Front panel connects after simulator startup
- **WHEN** a compatible host front panel connects to the simulator endpoint
- **THEN** it receives the current display state and can submit supported keypad operations

#### Scenario: No front panel is connected
- **WHEN** the simulator runs headlessly for an automated test
- **THEN** MainApplication continues to run and the test interface remains usable without a desktop window

#### Scenario: Simulator starts after the gateway
- **WHEN** the gateway starts before the simulator endpoint is available
- **THEN** it keeps serving the browser interface, reports the simulator as disconnected, and reconnects with bounded backoff

#### Scenario: Browser connects after gateway startup
- **WHEN** a browser opens the localhost front panel
- **THEN** it receives the latest complete display state and current simulator connection status without connecting directly to the simulator TCP endpoint

### Requirement: Present the EF800 front panel in a browser
The web front panel SHALL render the complete 240 x 64 monochrome display,
expose the 59 canonical EF800 keys in a keyboard-like layout, and communicate
live state and input through a bounded WebSocket interface. The browser SHALL
not present duplicate aliases such as `return` in addition to `enter`, or
unused raw matrix positions as actionable controls.

#### Scenario: Display frame arrives from the simulator
- **WHEN** the gateway accepts a complete valid LCD frame
- **THEN** it retains that frame and broadcasts its sequence, pixels, and available logical text state to connected browsers

#### Scenario: User operates the browser keypad
- **WHEN** the controlling browser presses, holds, and releases a supported
  canonical key
- **THEN** the gateway sends distinct semantic key-down and key-up operations
  through the normal simulator keyboard path and the browser marks the key as
  pressed only while it is held

#### Scenario: Browser renders the full key inventory
- **WHEN** a user opens the front panel
- **THEN** the page presents 59 actionable canonical key controls, including
  text, editing, navigation, power, and modifier keys, in a keyboard-like
  arrangement

### Requirement: Browser file operations exercise MainApplication MTP
The Virtual EF800 browser SHALL provide a file view for the MainApplication MTP-visible Data, Plugins, and FWU storages. Browser listing, download, and drag-and-drop upload operations SHALL be performed by an in-process software USB host through the native-sim virtual UHC/bus/UDC and the production MainApplication MTP class. The browser gateway SHALL NOT implement these operations by directly reading or writing the simulator filesystem.

#### Scenario: Browser lists MainApplication MTP storages
- **WHEN** a browser opens the virtual EF800 file view and the MainApplication MTP session is active
- **THEN** it lists Data, Plugins, and FWU using MTP operations and does not expose the private Configuration storage

#### Scenario: Browser uploads a file by drag and drop
- **WHEN** a user drops a file onto an approved MainApplication MTP storage
- **THEN** the gateway sends the bytes to the in-process host peer, which transfers them through USB MTP into the corresponding FAT root and reports completion only after the MTP operation succeeds

#### Scenario: Browser downloads a file
- **WHEN** a user requests a file from an approved storage
- **THEN** the in-process host peer retrieves it through MTP and the browser receives the completed file without direct filesystem access

#### Scenario: MTP peer or target is unavailable
- **WHEN** the virtual host cannot establish an MTP session or a transfer fails
- **THEN** the browser reports a bounded failure and does not report a partial upload as complete

### Requirement: Separate browser viewing from device control
The web gateway SHALL allow multiple browser sessions to observe display and connection state but SHALL grant keypad control to at most one browser session at a time.

#### Scenario: Viewer acquires control
- **WHEN** no controller is active and a viewer requests control
- **THEN** the gateway grants an explicit controller lease to that browser and reports ownership to all viewers

#### Scenario: Controller disconnects with held keys
- **WHEN** the controlling browser disconnects or releases control while keys may be held
- **THEN** the gateway sends release-all before making control available to another browser

### Requirement: Provide deterministic virtual peripheral behavior
The virtual EF800 SHALL provide the minimum storage, time, battery, serial-peer, display, keyboard, and device-state behaviors required by enabled MainApplication workflows through Zephyr-compatible emulated or simulated peripherals, and SHALL permit tests to configure normal responses, delays, unavailable devices, and bounded failures without modifying production workflow logic.

#### Scenario: Test configures a virtual serial instrument
- **WHEN** an automated scenario assigns a supported response script to a virtual EF800 port
- **THEN** the production port and plugin behavior exchanges bytes with that peer and observes the configured timing and response

#### Scenario: Test injects a peripheral failure
- **WHEN** a scenario marks a virtual peripheral unavailable or configures a bounded operation failure
- **THEN** MainApplication follows its normal degraded or recovery behavior and the failure is observable by the test

### Requirement: Exercise plugin supervision behavior without hardware
The virtual EF800 SHALL permit behavioral tests to run healthy, delayed, non-returning, stale-completion, malformed-request, and host-service-failure plugin scenarios while observing UI responsiveness, deadlines, cleanup, quarantine, navigation, and workspace integrity.

#### Scenario: Plugin operation exceeds its deadline
- **WHEN** a virtual test plugin does not complete before its host-defined deadline
- **THEN** the production supervisor behavior handles the timeout, discards unaccepted output, performs configured cleanup, and reports the resulting quarantine or escalation state

#### Scenario: UI input arrives during plugin work
- **WHEN** a plugin worker is active and the test submits a supported keypad event
- **THEN** the MainApplication UI path remains schedulable and processes or deliberately defers the input according to production policy

#### Scenario: Healthy plugin follows a failed plugin
- **WHEN** one virtual plugin is quarantined after safe cleanup and another valid plugin is selected
- **THEN** the unrelated plugin remains operable if shared virtual resources are in a known-safe state

### Requirement: Discover and assign plugins through generic metadata
The virtual EF800 SHALL expose compatible discovered plugins in port-assignment configuration using plugin-declared names, hardware capabilities, serial defaults, and readiness state. MainApplication SHALL NOT require plugin-specific IDs, names, protocol defaults, or device-builder branches to present or commit an assignment.

#### Scenario: Compatible plugin appears for a port
- **WHEN** a discovered measurement plugin is ready and its required hardware capability is supported by a selected port
- **THEN** the port-assignment menu displays the plugin's declared name as an assignable option

#### Scenario: Plugin assignment configures the port
- **WHEN** the user assigns a discovered plugin to a compatible port
- **THEN** MainApplication obtains serial configuration from the plugin metadata or generic plugin-host contract, initializes the port transport, persists the plugin assignment, and reports the assignment ready only after all steps succeed

#### Scenario: Incompatible or unavailable plugin is excluded
- **WHEN** a plugin is missing, invalid, incompatible, unavailable, or requires hardware unsupported by the selected port
- **THEN** the configuration menu does not offer it as a valid assignment and does not create a measurement entry for it

### Requirement: Present assigned measurement plugins as the measurement menu
The virtual EF800 Measurement menu SHALL contain one entry per ready assigned measurement plugin, using each plugin's declared name. Menu entries SHALL NOT be represented primarily as port numbers; MainApplication SHALL resolve the selected plugin to its assigned port and initialized transport internally.

#### Scenario: User opens the Measurement menu
- **WHEN** one or more ready measurement plugins are assigned to compatible ports
- **THEN** the menu lists the assigned plugin names without exposing port-number entries as the menu's user-facing identity

#### Scenario: User selects an assigned plugin
- **WHEN** the user selects a plugin entry from the Measurement menu
- **THEN** MainApplication resolves that plugin's assignment and opens a session using the associated initialized transport

#### Scenario: No ready plugin is assigned
- **WHEN** no discovered measurement plugin has a valid assignment and ready transport
- **THEN** MainApplication reports that no measurement device is ready and does not enter a plugin submenu

### Requirement: Render and execute plugin-owned measurement submenus
The selected measurement plugin SHALL provide its submenu title, action labels, available action bitmap, and plugin-owned display model through the generic plugin contract. MainApplication SHALL render and route those actions without hardcoded knowledge of a specific instrument or plugin ID.

#### Scenario: Plugin declares a measurement submenu
- **WHEN** a selected plugin session provides a valid display model and action bitmap
- **THEN** MainApplication renders the plugin-owned submenu and exposes only the actions declared by that plugin

#### Scenario: User executes the plugin's measure action
- **WHEN** the user selects the plugin-owned Measure action
- **THEN** MainApplication invokes the generic plugin session and displays the returned measurement data

#### Scenario: Measurement is saved to the workspace
- **WHEN** the user completes the plugin-owned measurement workflow and saves a successful measurement
- **THEN** MainApplication appends one measurement entry to the selected workspace using the plugin-produced result and does not require plugin-specific persistence logic

### Requirement: Exercise workspace browsing in the virtual EF800
The virtual EF800 SHALL expose the production workspace menu hierarchy and SHALL support deterministic automated scenarios for mixed workspace-line viewing, chronological paging, line truncation, large-file access, latest-position extraction, and no-workspace or read-failure handling.

#### Scenario: Virtual test opens the workspace viewer
- **WHEN** an automated virtual EF800 test selects `Workspace` and then `Show Measurements`
- **THEN** the observed production display contains workspace lines in file order with the newest visible line at the bottom

#### Scenario: Virtual test scrolls a large workspace by file position
- **WHEN** a virtual test provides a workspace file larger than 4096 bytes and moves up or down through the displayed lines
- **THEN** the production viewer reads the previous or next range using bounded memory, without a file-size failure or ordering inversion

#### Scenario: Virtual test selects current position
- **WHEN** a virtual test selects `Show current Position` for a mixed workspace file
- **THEN** the production display contains the last position record and ignores earlier position records as the current result

#### Scenario: Virtual test verifies truncation and unavailable state
- **WHEN** a virtual test supplies an overlong line or opens the viewer without a selected workspace
- **THEN** the production display truncates the line without wrapping or reports the unavailable state respectively

### Requirement: Preserve the hardware qualification boundary
Virtual EF800 results SHALL be identified as behavioral simulation evidence and SHALL NOT be accepted as proof of STM32 register behavior, ARM executable loading, MPU permissions, CPU exception classification, bootloader handoff, electrical timing, signal integrity, physical QSPI or USB operation, or fitted peripheral wiring.

#### Scenario: Virtual behavioral suite passes
- **WHEN** all virtual EF800 scenarios pass
- **THEN** the project may claim the covered MainApplication and plugin behaviors passed without hardware but retains every applicable hardware-specific release gate

#### Scenario: MPU containment requires qualification
- **WHEN** a requirement depends on preventing an unauthorized memory access or classifying an ARM fault
- **THEN** the result requires target evidence and cannot be closed by substituting a virtual plugin failure event
