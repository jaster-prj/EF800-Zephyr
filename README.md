# EF800 Zephyr workspace

This repository migrates the EF800 firmware from Mbed OS to Zephyr. Builds run
inside the devcontainer so the Zephyr revision and toolchain do not depend on a
developer's host setup.

The devcontainer follows the `zephyr_gartennode_poolmodul` layout:

```text
/workspace/                         west workspace and build root
/workspace/application/             this Git repository
/workspace/application/application/ firmware applications
/workspace/zephyr/                  downloaded Zephyr checkout
```

The container is intended to be reused during development. `west update` is
not run every time a terminal is opened. Run the initialization commands once
after creating the container; later sessions can build directly.

## Reusable workspace

The development image can already contain a populated west workspace. Inspect
it before running any network operation:

```sh
/opt/venv/bin/west topdir
/opt/venv/bin/west config manifest.path
/opt/venv/bin/west config manifest.file
/opt/venv/bin/west list zephyr -f "{path} {revision} {sha}"
```

If the workspace is `/workspace`, the manifest is
`application/manifest-repo/west.yml`, and Zephyr matches the revision in
`manifest-repo/west.yml`, build directly. Do not run `west update` for normal
development sessions.

Only initialize or update a new, incomplete, or manifest-mismatched container:

```sh
if [ -d .west ]; then
  /opt/venv/bin/west config manifest.path application/manifest-repo
  /opt/venv/bin/west config manifest.file west.yml
else
  /opt/venv/bin/west init -l application/manifest-repo
fi
/opt/venv/bin/west update
/opt/venv/bin/west zephyr-export
/opt/venv/bin/python -m pip install -r zephyr/scripts/requirements.txt
```

Build the six firmware targets with:

```sh
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-application -d application/build/ef800-application
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
/opt/venv/bin/west build -p always -b ef800_keyboard application/application/keyboard-application -d application/build/keyboard-application
/opt/venv/bin/west build -p always -b ef800_keyboard application/application/keyboard-bootloader -d application/build/keyboard-bootloader
/opt/venv/bin/west build -p always -b ef800_keyboard application/application/keyboard-updater -d application/build/keyboard-updater
bash application/scripts/verify-images.sh
```

For the temporary, read-only external-flash hardware-debug image:

```sh
/opt/venv/bin/west build -p always -b ef800 application/application/test-hwdebug -d application/build/test-hwdebug
bash application/scripts/verify-test-hwdebug-image.sh
```

This image replaces the second-level bootloader at `0x08000000` and must only
be used on a recoverable engineering target. It provides a USB CDC diagnostic
console and read-only SPI/QSPI/QPI probing with recovery boundaries; see
`docs/hardware-debug-qspi.md`.

The same commands are collected in `.devcontainer/commands.txt`.

The board definitions are currently kept in this mounted application repository
under `boards/` so the first bring-up does not require a second repository.
They can be moved into a shared Zephyr board module once the hardware has been
verified.

## Current layout

The legacy EF800 internal flash contract is retained for the first migration
increment:

```text
0x08000000 - 0x0800FFFF  second-level bootloader
0x08010000 - 0x0802FFFF  backup application
0x08030000 - 0x080FFFFF  EF800 application
```

The current Zephyr contract reserves `0x08028000..0x0802FFFF` for boot
metadata. The captured legacy `BackupApplication.bin` is 128 KiB and uses that
range, so the reservation must not be treated as unused when restoring the
legacy image. The external QSPI flash is configured as the identified
`IS25LP032D-JTLE-TR` 32-Mbit / 4-MiB device. JEDEC ID, SFDP, signal integrity,
erase geometry, and existing FAT contents still require hardware validation
before production flashing.

The connected STM32L496 engineering target was erased and verified blank during
initial recovery preparation, then later used for controlled bootloader and
EF800 Application bring-up. See `docs/stm32l496-target-snapshot.md` for the
historical option-byte values, recovery-image hashes, and erase record. See
`docs/migration-status.md` for current migration state.

See `docs/` for the migration contract and hardware assumptions.

The system architecture is documented under [`architecture/`](architecture/index.md)
using separately maintained arc42 sections, with block views and runtime
diagrams under `architecture/diagram_as_code/`.

For physical target testing with semantic virtual keyboard input, see
`docs/hardware-debug-keyboard.md`.

For the TLX-711A virtual controller and framebuffer viewer, see
`GSE/TLX-711A/README.md` and build `application/tlx-711a`.
