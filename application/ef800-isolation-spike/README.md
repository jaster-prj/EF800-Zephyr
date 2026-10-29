# EF800 isolation spike on NUCLEO-F767ZI

This target validates the EF800 plugin isolation prerequisites on the
STM32F767ZI before using the EF800 STM32L496 target. It exercises:

- ARMv7-M MPU and Zephyr userspace setup;
- execute-XOR-write configuration;
- an unprivileged worker thread;
- RX/RW memory-domain partitions;
- the board's second UART (`USART6`).

The board's ST-LINK virtual COM port is `USART3`. The auxiliary UART is
`USART6`, using the board DTS pins `PG14=TX` and `PG9=RX`, at 115200 8N1.
Connect a 3.3 V USB-UART adapter to those pins and keep its ground connected
to the Nucleo ground.

## Build

Run inside the reusable development container:

```powershell
docker exec -w /workspace EF800_Devcontainer /opt/venv/bin/west build -p always -b nucleo_f767zi application/application/ef800-isolation-spike -d application/build/ef800-isolation-spike-f767
```

The image is linked for the Nucleo's internal flash at `0x08000000`.

## Flash

Use the NUCLEO-F767ZI ST-LINK and verify the detected MCU before flashing:

```powershell
& "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe" -c port=SWD mode=UR reset=HWrst -d "build\ef800-isolation-spike-f767\zephyr\zephyr.bin" 0x08000000 -rst
```

Do not use an EF800 L496 probe or image for this test.

## Expected result

The ST-LINK console should contain output similar to:

```text
EF800 isolation spike
userspace=1 arm_mpu=1 xor_write=1
mpu_regions=<board-dependent> min_align=<board-dependent> stack_guard=1
worker user=1 result=0x5a PASS
```

The auxiliary UART should receive:

```text
EF800 isolation auxiliary UART ready
```

Successful output demonstrates that the application reaches an unprivileged
worker and completes the memory-domain access path. It does not yet prove
EF800 package loading, MPU fault recovery, or external-flash behavior.

## External flash

The spike currently does not access the external flash. The NUCLEO-F767ZI board
DTS does not define a universal external-flash part or wiring. Before adding a
flash read/write gate, record the fitted flash manufacturer/part number, bus
(QSPI or SPI), chip-select, clock, IO pins, reset/write-protect wiring, and
whether the part is mounted on a shield or an adapter. The device-tree node and
JEDEC-compatible binding can then be added without guessing the hardware.
