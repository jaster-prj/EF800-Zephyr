## 1. Workspace Measurement Context

- [x] 1.1 Extend `struct ef800_workspace` with current measurement point number and code context.
- [x] 1.2 Extract the last relevant measurement point number and code while loading the selected workspace, ignoring position records.
- [x] 1.3 Update workspace context only after a measurement line is appended successfully, and expose generic next-point context to callers.

## 2. Plugin ABI and Host Services

- [x] 2.1 Extend the versioned measurement ABI with session-context initialization for current code and point number.
- [x] 2.2 Extend the ABI with bounded serialized workspace-record output and generic host append status handling.
- [x] 2.3 Add an explicit host acknowledgement for serialized-record append success/failure so Fixmessung advances only after persistence succeeds.
- [x] 2.4 Pass workspace context into both Fennel1 and Elta3 when sessions open, without exposing filesystem or workspace pointers.
- [x] 2.5 Preserve Elta3 `Up`, `Down`, and `Enter` dispatch through the active plugin transport and keep Fennel1 directional actions explicitly unsupported.

## 3. Plugin-Owned Measurement Workflows

- [x] 3.1 Move code, point-number, staff-height, Save, Discard, and Fixmessung state/input/rendering into Fennel1.
- [x] 3.2 Move the same common measurement workflow into Elta3 while retaining Elta3 instrument navigation actions.
- [x] 3.3 Have plugin Save/Fixmessung request host append of the bounded serialized line; keep Discard entirely inside the plugin.
- [x] 3.4 Make Fixmessung use updated workspace context, increment the point, and start the next measurement without returning to the parent menu.
- [x] 3.5 Keep plugin measurement failures, invalid input, and host append failures diagnosable in the plugin display without corrupting plugin state.

## 4. EF800 Application Integration

- [x] 4.1 Remove host-owned measurement editor and pending-action branches from EF800 Application and render only plugin display models.
- [x] 4.2 Route generic plugin append requests to the selected workspace without interpreting serialized measurement lines.
- [x] 4.3 Verify plugin session lifetime and UART ownership remain single-instance across the complete plugin workflow.
- [x] 4.4 Preserve package-owned instrument behavior and avoid adding Elta3 or Fennel1 protocol-specific branches to EF800 Application.

## 5. Automated Coverage

- [x] 5.1 Add workspace tests proving load extracts the last measurement code/point and append updates the context.
- [x] 5.2 Add plugin workflow tests proving both Fennel1 and Elta3 return complete serialized records only after Save.
- [x] 5.3 Add fixed-measurement tests proving each Fixmessung append advances to a strictly increasing point number and starts the next measurement.
- [x] 5.4 Add plugin-host tests proving Elta3 directional actions still use the plugin transport and Fennel1 rejects unsupported directions without emitting commands.
- [ ] 5.5 Add a native-sim end-to-end Fennel1 scenario and a matching Elta3 scenario covering assignment, metadata entry, measurement, Save, and workspace verification.

## 6. Validation and Documentation

- [x] 6.1 Run plugin package verification and the shared plugin, measurement, persistence, and UI tests.
- [x] 6.2 Run the native-sim test suite and verify the expected workspace records for normal Save and repeated Fixmessung.
- [x] 6.3 Build all required EF800 firmware targets and update migration/status documentation with the completed measurement workflow coverage.
