## ADDED Requirements

### Requirement: USB MSD composition selects target-supported availability
The composition catalog SHALL define mutually exclusive USB MSD target selections `USBMSD0` and `USBMSD1`. The native-sim target SHALL default to `USBMSD0`, which represents an unavailable MainApplication MSD controller. The physical EF800 target SHALL default to `USBMSD1`, which preserves MainApplication USB MSD availability.

#### Scenario: Native-sim uses USBMSD0
- **WHEN** MainApplication initializes for `native_sim` without a stored compatible override
- **THEN** the effective composition selects `USBMSD0` and excludes USB MSD from MainApplication's Port 4 choices and runtime

#### Scenario: Physical EF800 defaults to USBMSD1
- **WHEN** MainApplication initializes for the physical EF800 target without a stored compatible override
- **THEN** the effective composition selects `USBMSD1` and USB MSD remains selectable at Settings / Port Assignment / Port 4

#### Scenario: Composition record contains a target-incompatible token
- **WHEN** a composition record contains a recognized USB MSD token that is not supported by the current build target
- **THEN** the target does not activate that token and uses its registered target default

### Requirement: Unsupported native-sim MSD assignments are automatically cleared
When `USBMSD0` is effective, MainApplication SHALL clear any persisted Port 4 USB MSD assignment from the active configuration store and SHALL prevent it from being restored or activated during that boot.

#### Scenario: Stale MSD assignment is loaded in native-sim
- **WHEN** MainApplication loads a persisted USB MSD assignment while `USBMSD0` is effective
- **THEN** it clears the assignment from the configuration record, leaves the shared-data volume mounted, and reports the cleanup result

#### Scenario: Assignment cleanup persistence fails
- **WHEN** MainApplication cannot persist the removal of a stale MSD assignment
- **THEN** it still keeps MSD disabled for the current native-sim boot and reports the failure for later recovery

## REMOVED Requirements

### Requirement: Keep the initial mechanism composition-neutral
**Reason**: This change registers the first concrete target-specific composition entries and uses them to gate USB MSD behavior.
**Migration**: Existing unknown COMP-Nr. tokens remain preserved under the profile format's compatibility rules; `USBMSD0` and `USBMSD1` become recognized selections with target-specific defaults.
