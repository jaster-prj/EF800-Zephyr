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

Build the four firmware targets with:

```sh
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-main -d application/build/ef800-main
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-backup -d application/build/ef800-backup
/opt/venv/bin/west build -p always -b ef800 application/application/ef800-bootloader -d application/build/ef800-bootloader
/opt/venv/bin/west build -p always -b ef800_keyboard application/application/ef800-keyboard -d application/build/ef800-keyboard
```

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
0x08030000 - 0x080FFFFF  main application
```

The 64 KiB gap at `0x08020000` is reserved until the hardware update format is
finalized. The external QSPI flash capacity is deliberately configured in the
board DeviceTree and must be set to the actual fitted part before production
flashing.

See `docs/` for the migration contract and hardware assumptions.
