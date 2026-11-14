# EF800 Application Acceptance

These tests target the production `ef800-application` image on the STM32L496. They do
not use the former NUCLEO PluginManager prototype.

External package execution is disabled in the default physical configuration.
`plugin-dev.conf` enables the inline compatibility executor only for trusted
development packages; it does not provide userspace isolation. The
measurement-plugin native_sim CTest suite separately builds and executes the
native `.pmp` packages through PluginManager.

The acceptance harness must use the normal EF800 Application console and must
keep the console port closed between scripted accesses. It validates:

- Startup-only plugin discovery when `/EF800:` storage is reachable.
- Built-in-only degraded startup when storage is unavailable.
- Invalid package rejection before execution.
- EF800 hardware-capability filtering during port assignment.
- Versioned external plugin-ID persistence.
- Calculation preview versus confirmed Workspace append.
- Plugin UI/session and logical keyboard behavior.
- USB MSD transition and restart-based rediscovery.

Hardware-dependent tests require the matching EF800 target, normal console,
internal storage, and any required measurement-device simulator. A storage
unavailable result is reported as a hardware blocker and never triggers
formatting, replacement, or repair.

The production image is built with:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b ef800 application/application/ef800-application -d application/build/ef800-application
```

The generic PluginManager unit tests remain in the PluginManager repository.
