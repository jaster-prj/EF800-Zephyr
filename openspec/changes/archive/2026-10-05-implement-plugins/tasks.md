## 1. Contract and ABI

- [x] 1.1 Define versioned submenu metadata, logical key-event, action-result, measurement-state, and bounded display-model structures in the public EF800 measurement plugin contract.
- [x] 1.2 Define action identifiers and display/input bounds for measurement packages, including explicit consumed, unsupported, request-back, and error results.
- [x] 1.3 Extend package descriptors and host validation to require compatible UI/input/display contract versions and structure sizes before activation.
- [x] 1.4 Add compatibility, malformed-model, unsupported-action, and contract-boundary tests for the new ABI.

## 2. SDK Package Layout

- [x] 2.1 Create independent `application/plugins/measurement-plugin/elta3` and `application/plugins/measurement-plugin/fennel1` package source, metadata, and build targets.
- [x] 2.2 Move shared protocol/runtime helpers into an SDK-common location that contains no EF800Application or Zephyr dependencies.
- [x] 2.3 Add independent Elta3 and Fennel1 descriptors with stable identities, required services, capabilities, and package-owned UI contract metadata.
- [x] 2.4 Produce separately named and verifiable Elta3 and Fennel1 `.pmp` outputs and preserve independent package discovery when one artifact is missing or invalid.

## 3. Elta3 Plugin Behavior

- [x] 3.1 Implement Elta3 submenu declaration for measurement, up, down, and enter actions inside the Elta3 package.
- [x] 3.2 Move Elta3 logical action handling and directional instrument commands into the Elta3 package session implementation.
- [x] 3.3 Move Elta3 measurement result state, formatting, status text, and bounded display-model generation into the Elta3 package.
- [x] 3.4 Add Elta3 host tests for submenu state, logical keys, directional commands, measurement results, display bounds, and session reset.

## 4. Fennel1 Plugin Behavior

- [x] 4.1 Implement Fennel1 measurement-only submenu declaration inside the Fennel1 package.
- [x] 4.2 Move Fennel1 logical input behavior into the package and return explicit unsupported results for undeclared directional actions.
- [x] 4.3 Move Fennel1 measurement result state, formatting, status text, and bounded display-model generation into the Fennel1 package.
- [x] 4.4 Add Fennel1 host tests for measurement action handling, unsupported input, protocol results, display bounds, and session reset.

## 5. EF800Application Host Integration

- [x] 5.1 Replace host-side device-specific measurement submenu construction with package-declared action metadata and generic session calls.
- [x] 5.2 Forward only host-approved logical keyboard events to the active package while retaining raw keyboard parsing, global keys, back/cancel, and navigation ownership.
- [x] 5.3 Validate package display models and render them in the host-owned LCD body without device-specific formatting branches or direct package drawing access.
- [x] 5.4 Preserve port assignment, UART configuration, port-service lifetime, package creation/destruction ordering, and unavailable-package diagnostics.
- [x] 5.5 Remove `ef800_device_plugin` includes, source-list entries, registry fields, and legacy production dispatch paths from EF800Application and shared host integration.

## 6. Verification and Documentation

- [x] 6.1 Update package SDK documentation, package naming, contract examples, and host-integration guidance for package-owned submenu/input/display behavior.
- [x] 6.2 Add native registry, package, UI, keyboard-dispatch, lifecycle, bounds, and missing-package tests covering the new contract.
- [x] 6.3 Run Elta3 and Fennel1 host builds, package verification, and independent `.pmp` artifact tests.
- [x] 6.4 Run the required EF800Application, backup, bootloader, keyboard, and native test builds with pristine configuration where contract or source lists changed.
- [x] 6.5 Verify image sizes, linked addresses, generated DeviceTree outputs, package discovery, assignment readiness, and session teardown on the target acceptance path.
