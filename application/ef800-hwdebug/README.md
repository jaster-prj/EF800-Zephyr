# EF800 hardware debug

Direct instrument tests are available as `uart fts3-test uart1|uart3` (9600
8N1, request `11 0d 0a`) and `uart elta3-test uart1|uart3` (1200 7O2, full
`S14`/`B`/`Q` measurement exchange). Both capture response lines as raw hex and use physical
instruments, not the simulator. The separate `uart test port1|port2` command
is a 115200 8O2 TX/RX loopback test. The current hwdebug image occupies
`0x08000000`; restore the second-level bootloader only when finished with
hardware-debug testing.

## FMC SRAM design inputs

The external SRAM is the schematic-fitted `IS61WV5128FBLL-10TLI`. Its
organization is 512K × 8, so the usable byte range is 512 KiB. The SRAM
address pins are byte-addressed; connect the FMC asynchronous SRAM/NOR
controller to **Bank 1 / NE1** at `0x60000000`, with an 8-bit data bus. The
resulting device window is `[0x60000000, 0x60080000)`. Do not treat unused
space in the FMC bank window as installed SRAM.

`schema/EF800_NEW.kicad_sch` is the wiring authority. The connected SRAM bus
uses these STM32L496ZG package pins (all GPIOs use FMC **AF12**, per the
datasheet Alternate Function table beginning on page 98):

| SRAM/FMC signal | STM32 pin (package pin) |
| --- | --- |
| A0-A5 | PF0-PF5 (10-15) |
| A6-A9 | PF12-PF15 (50, 53-55) |
| A10-A11 | PG0-PG1 (56-57) |
| A12-A15 | PG2-PG5 (87-90) |
| A16-A18 | PD11-PD13 (80-82) |
| D0-D1 | PD14-PD15 (85-86) |
| D2-D3 | PD0-PD1 (114-115) |
| D4-D7 | PE7-PE10 (58-60, 63) |
| NE1, NOE, NWE | PD7 (123), PD4 (118), PD5 (119) |
| NBL0 | PE0 (141) |

This is a byte-wide connection: use NBL0 and leave NBL1 inactive. These pins
match the schematic's `FMC_A*`, `FMC_D*`, `FMC_NE1`, `FMC_NOE`, `FMC_NWE`, and
`FMC_NBL0` nets.

The EF800 board clock tree configures HCLK to 80 MHz (`boards/ef800/ef800.dts`),
giving an FMC HCLK period of 12.5 ns. The fitted `-10` SRAM timing grade
specifies up to 8 ns access at 3.3 V ±10% (10 ns outside that supply range)
and a 10 ns minimum write cycle. Use asynchronous SRAM mode, disable address
and data multiplexing. The initial hwdebug tuple was ADDSET=1, ADDHLD=1,
DATAST=2, BUSTURN=0. At the board's normal 80 MHz HCLK this provided a
12.5 ns address setup phase and 25 ns data phase. Subsequent hwdebug timing
experiments and the current test tuple are recorded below. No synchronous
burst/clocked-SRAM timing is used. FMC bank 1 is selected by NE1; its device
window is `[0x60000000, 0x60080000)`.

This is a datasheet/schematic-derived starting configuration, not evidence
that the board SRAM is operational. Validate the whole range with bounded
patterns and alias checks on STM32L496 hardware before declaring it usable.
The SRAM datasheet also requires at least 150 μs power-up initialization
before access.

## SRAM bring-up procedure and current result

Build `ef800-hwdebug`, verify the ST-LINK target is STM32L496xx (device ID
`0x461`), and program `build/ef800-hwdebug/zephyr/zephyr.bin` at
`0x08000000`. The image uses UART4 at 115200 8N1 for the command console;
connect to the board's CP2102 serial port and issue `sram test`. Each run
checks boundary bytes, four constant full-range patterns, full-range walking
data bits (and complements), complementary checkerboards, four deterministic
pseudorandom full-range passes, an address-sensitive pattern, and
power-of-two address alias points. A failure prints the phase/address/
expected/actual value and ends with `region NOT declared usable`.

This command is an engineering diagnostic, not a production qualification
certificate. Even a pass covers only the attached unit, current supply,
temperature, clock/timing configuration, and one test run. Production release
requires repeated cold/warm-start runs on representative units across the
specified voltage and temperature envelope, long-duration testing, validation
of actual FMC timing and signal integrity, and stress testing with the intended
concurrent bus/peripheral workload. Record configuration, unit identity,
environment, run count, duration, and all failures. Do not integrate SRAM into
production firmware until those qualification results are reviewed and the
release gate is explicitly closed.

On 2026-11-06, the pristine hwdebug build passed and generated DeviceTree
showed FMC Bank 1 enabled with the AF12 pins and configured timing; the link
map placed the image in its normal internal FLASH/RAM regions and did not
place the external SRAM in ordinary linker RAM. The selected probe
`066FFF504982494867074032` identified device `0x461` before programming.
The SRAM test did **not** pass: a comprehensive run reported data-pattern
mismatches (including `0x60001467`, expected `0xff`, read `0x00`), an
address-pattern mismatch at `0x60000000`, and an address-alias mismatch at
`0x60000200` (expected `0x0a`, read `0xa5`). Earlier invocations also failed
at boundary/data-pattern locations. Therefore the external memory is not
validated or available for firmware use. FMC timing, board connectivity,
and physical SRAM behavior require further diagnosis; no MainApplication
integration is authorized by these results.

Timing experiments (all on the confirmed STM32L496 target) did not fix the
failures:

| HCLK | ADDSET | ADDHLD | DATAST | Result |
| ---: | ---: | ---: | ---: | --- |
| 40 MHz | 1 | 1 | 2 | Data-pattern failures and `0x200`/base alias |
| 20 MHz | 1 | 1 | 2 | Boundary, data, address-pattern failures and alias |
| 20 MHz | 1 | 1 | 8 | Data-pattern failures and alias |
| 20 MHz | 8 | 4 | 8 | Boundary, data, address-pattern failures and alias |
| 20 MHz | 15 | 8 | 15 | Boundary, data, address-pattern failures and alias |

For the last trial, the long scan initially hit the hwdebug watchdog; the
test was updated to feed the watchdog during full-range loops and then
completed, still reporting failures. The persistent
`0x60000200`/base alias (expected `0x0a`, read `0xa5`) and varied read values
show that the SRAM bus is responding, but lowering HCLK and extending data
and address phases alone do not make it reliable. Keep the external-memory
release gate closed while the FMC pin configuration/address path and board
signals are investigated. A non-resetting ST-LINK register read also failed
to get the core ID; no further register reads were attempted.

Three consecutive complete runs at the final 20 MHz / ADDSET=8 / ADDHLD=4 /
DATAST=8 setting had a stable signature: all three read `0x63` at base where
the address pattern expected `0x69`, and all three read the base tag `0xa5`
at `0x60000200` where the alias test expected `0x0a`. Data mismatches were
consistently in the `0x60001400` neighborhood; `0xff` and `0x55` were read
reciprocally at failed locations, while the exact locations varied slightly
between runs. This is repeatable failure behavior, not a passing/stable
memory configuration.

The extra-slow 20 MHz / 15 / 8 / 15 configuration was also run three times.
All three retained the exact base read `0x63` (expected `0x69`) and the
`0x200` alias (`0xa5` instead of `0x0a`). The reciprocal `0xff`/`0x55`
mismatches repeated around `0x60001400` (including the same `0x60001414`
location), though other failing bytes varied. Increasing the waits this far
did not improve the stable failure signature.

AN4761 review found another setup requirement that the initial bring-up had
missed: its STM32L4 FMC examples enable the VDDIO2 supply before setting up
FMC GPIOs. The hwdebug image now enables VDDIO2 in a `PRE_KERNEL_1` hook,
before the FMC driver's `POST_KERNEL` initialization. FMC pin slew was also
changed from the observed low-speed setting to very-high speed as AN4761
recommends. The new build was flashed after confirming ST-LINK
`066FFF504982494867074032` detected Device ID `0x461` (STM32L496xx).

With VDDIO2 enabled and the FMC pins at slew setting 3, `sram test` reported
PF15 and PG1 in AF12 as expected, but the test still failed: base readback
remained `0x63` instead of `0x69`, and aliases at offsets `0x200` and `0x800`
still read the base tag. Thus the missing supply and default-low GPIO slew
were real configuration omissions, but correcting them has not yet validated
the SRAM. The active trial remains HCLK 20 MHz with ADDSET/ADDHLD/DATAST
15/8/15; the region remains unavailable.

On 2026-11-08, the updated hwdebug image was built and programmed after the
selected ST-LINK reported device ID `0x461` (STM32L496xx). The expanded test
passed once on the attached board, including full-range walking-bit,
checkerboard, and four pseudorandom passes. This is a successful diagnostic
run, but it does not establish environmental margins or production
qualification; the external-memory release gate remains closed pending the
qualification work above.

### 80 MHz verification

The 20 MHz PLL override was removed from the hwdebug overlay. The generated
configuration reports `CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC=80000000`, the
generated PLL settings are HSE 25 MHz with `mul-n=32` and `div-r=2`, and FMC
timing is ADDSET/ADDHLD/DATAST 1/1/2. On 2026-11-08, the image was flashed at
`0x08000000` after the selected L496 probe reported device ID `0x461`. UART4
reported SYSCLK 80 MHz, and `sram test` passed full 512 KiB boundary, data,
walking-bit, checkerboard, pseudorandom, address, and alias checks. This
confirms the diagnostic and configured FMC access at the normal 80 MHz clock;
it does not replace environmental or production qualification.
