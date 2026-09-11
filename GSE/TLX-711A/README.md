# TLX-711A Virtual Display

`application/tlx-711a` emulates the parallel T6963C-compatible controller in
the Toshiba TLX-711A module. It captures the EF800 LCD bus and emits packed
`240 x 64` framebuffer updates over the selected Zephyr console UART.

The framebuffer payload is 1,920 raw bytes: 64 rows of 30 bytes. In each byte,
bit 0 represents the leftmost pixel of the byte. The dummy converts the EF800
T6963 graphics layout of 40 controller bytes per row and 6 pixels per byte into
this packed 240-pixel framebuffer. A packet is delimited as:

```text
FB: [<1920 raw framebuffer bytes>]\r\n
```

The bytes inside the brackets are not encoded or converted. The `FB: [` prefix
and `]\r\n` suffix allow diagnostic text and framebuffer data to share the CDC
stream. The viewer reads exactly 1,920 bytes between the delimiters and ignores
all other messages. The default CDC line rate is 256000 baud.

Build from the Zephyr container with:

```sh
west build -p always -b nucleo_wb55rg application/application/tlx-711a \
  -d application/build/tlx-711a-wb55
```

Run the host viewer after installing `pyserial`:

```sh
python -m pip install pyserial
python GSE/TLX-711A/virtual_display.py COM9
```

The current overlay targets a Nucleo-WB55RG Arduino header. The Zephyr board
definition resolves the Arduino pins as follows:

```text
D0..D7 = Arduino D0..D7
/WR    = D8
/RD    = D9
/CE    = D10
C/D    = D11
/RESET = D12
FS     = D13
```

The transport is native USB CDC ACM. D10 is PB6, which conflicts with the
default Nucleo ST-Link USART1 TX connection, so the LCD bus takes priority.

SPI1 is disabled by the application overlay because its default Arduino pin
mapping overlaps LCD `/CE`, `C/D`, and `FS`.

The dummy releases D0-D7 as high-impedance inputs and drives them push-pull only
during a controller read cycle. The data pins have no internal pull resistors.
All control signals, including `FS` and active-low `RESET`, are inputs. An
active-low reset resets the emulated controller and clears its virtual RAM.

This first increment handles graphics RAM writes and the standard address/home/
area commands. It reports the controller ready during status reads. Text
character rendering and full read-cycle emulation can be added after capturing
the target firmware's actual T6963C command sequence.
