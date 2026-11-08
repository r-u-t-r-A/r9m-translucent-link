# Configuration reference

All configuration happens at build time. Nothing is stored on the module, and there are
no menus.

## Where settings go

| File | Applies to | Use for |
|---|---|---|
| `link_defines.txt` | every build | binding phrase, domain, radio settings, anything both ends share |
| `link_defines_<env>.txt` (optional) | only that env, e.g. `link_defines_slave.txt` | per-end settings: baud, pins, power, buffers, LEDs |

Format: one `-DNAME` or `-DNAME=value` per line, with no spaces around `=`. Lines
starting with `#` are ignored. A per-env file overrides a setting of the same name from
`link_defines.txt`. Settings marked **shared** below are refused in per-env files,
because the two modules must agree on them.

The defaults live in `src/link_config.h`. `LINK_DEFINES=/path/to/file pio run ...` uses
a different shared file; per-env files are then looked up in that file's directory.

## Build environments

| Env | Role | Serial port |
|---|---|---|
| `master` | master | UART |
| `slave` | slave | UART |
| `master_usb` | master | USB (needs the [hardware mod](setup.md#usb-envs-master_usb--slave_usb)) |
| `slave_usb` | slave | USB (needs the hardware mod) |
| `native` | — | runs the protocol unit tests on the PC (`pio test -e native`) |

The env sets `LINK_ROLE_MASTER` / `LINK_ROLE_SLAVE` and `LINK_USB_SERIAL`. Don't put
those in a defines file.

## Required (shared)

| Define | Description |
|---|---|
| `MY_BINDING_PHRASE="..."` | Identifies your pair. It seeds the hop sequence and the packet CRC, so modules with different phrases ignore each other. Any text; keep it private. |
| `Regulatory_Domain_<X>` | Frequency band and channel plan, exactly one (table below). |

| Domain | Frequencies | Channels |
|---|---|---|
| `Regulatory_Domain_EU_868` | 865.275 – 869.575 MHz | 13 |
| `Regulatory_Domain_FCC_915` | 903.5 – 926.9 MHz | 40 |
| `Regulatory_Domain_AU_915` | 915.5 – 926.9 MHz | 20 |
| `Regulatory_Domain_IN_866` | 865.375 – 866.95 MHz | 4 |
| `Regulatory_Domain_EU_433` | 433.1 – 434.45 MHz | 3 |
| `Regulatory_Domain_AU_433` | 433.42 – 434.42 MHz | 3 |
| `Regulatory_Domain_US_433` | 433.25 – 438.0 MHz | 8 |
| `Regulatory_Domain_US_433_WIDE` | 423.5 – 438.0 MHz | 20 |

The 433 MHz domains only make sense on 433 MHz hardware; the R9M is a 868/915 MHz
module.

## Radio (shared)

| Define | Default | Values | Description |
|---|---|---|---|
| `LINK_PRESET` | `1` | `0`–`4` | Modulation preset, see below. `0` = custom, using the four settings that follow. |
| `LINK_SF` | (preset) | `6`–`12` | LoRa spreading factor. Custom only. |
| `LINK_BW_KHZ` | (preset) | `125`, `250`, `500` | LoRa bandwidth. Custom only. |
| `LINK_CR` | (preset) | `5`–`8` | LoRa coding rate 4/x. Custom only. |
| `LINK_PKT_LEN` | (preset) | `8`–`128` | Packet length in bytes, including 7 bytes of header and CRC. Custom only. |
| `LINK_PREAMBLE_LEN` | `8` | ≥ `6` | LoRa preamble length in symbols. |
| `LINK_TURNAROUND_US` | `1000` | µs | Time budgeted per frame between receiving a packet and the reply being on air. |
| `LINK_GUARD_US` | `1000` | µs | Extra idle time per frame for hopping. |

| Preset | Modulation | Packet | Frame | Payload, each direction |
|---|---|---|---|---|
| `1` (default) | SF6 BW500 CR4/5 (ELRS 200 Hz modulation) | 64 B | 35 ms | ~1620 B/s |
| `2` | SF7 BW500 CR4/5 | 64 B | 59 ms | ~960 B/s |
| `3` | SF8 BW500 CR4/7 | 48 B | 110 ms | ~370 B/s |
| `4` | SF9 BW500 CR4/7 | 32 B | 145 ms | ~170 B/s |

Higher presets trade throughput for range. Both directions get the listed rate at the
same time. Frame length = 2 × time on air + 2 × `LINK_TURNAROUND_US` + `LINK_GUARD_US`,
computed at build time. Each module transmits for about half of every frame.

Custom-mode limits are checked when you build. Combinations with a symbol time over
16 ms (SF11 at BW125, SF12 at BW125/250) are rejected, because the driver doesn't enable
low-data-rate optimisation. Narrower bandwidths are more sensitive to crystal offset
between the two modules; the slave corrects for it, but BW500 is the tested choice in
ExpressLRS.

## Per-module settings

These may differ between the two ends; put them in `link_defines_<env>.txt` when they
do.

### Serial port

| Define | Default | Description |
|---|---|---|
| `LINK_UART_BAUD` | `115200` | UART baud rate, 8N1. Ignored in USB builds. Anything the STM32 USART supports; it doesn't have to match the other module. |
| `LINK_UART_RX_PIN` | `PA10` | UART RX pin (data into the module). |
| `LINK_UART_TX_PIN` | `PA9` | UART TX pin (data out of the module). |
| `LINK_USB_IGNORE_DTR` | `0` | USB builds: `1` treats the port as open even if the host never sets DTR. Data then waits until it is read, instead of being discarded while no program has the port open. |
| `LINK_DROP_INPUT_WHEN_DISCONNECTED` | `1` | `1`: input that arrives while the radio link is down is discarded, and the input buffer is cleared when the link goes down, so the far end doesn't get a burst of stale data on reconnect. `0`: keep queueing (up to `LINK_TX_FIFO_SIZE`). |

The RX/TX pins must belong to the same USART on the STM32F103:

| USART | RX / TX | Notes on the R9M |
|---|---|---|
| USART1 | `PA10` / `PA9` | **Recommended.** The WiFi backpack mod pads. |
| USART2 | `PA3` / `PA2` | Check your board for what's connected there. |
| USART3 | `PB11` / `PB10` | The S.Port line, through a buffer; not usable as full duplex without rework. |

`PB7`/`PB6` (USART1 remap) are not available: they carry I2C to the power DAC.

### Power

| Define | Default | Values | Description |
|---|---|---|---|
| `LINK_POWER` | `PWR_100mW` | `PWR_10mW`, `PWR_25mW`, `PWR_50mW`, `PWR_100mW`, `PWR_250mW`, `PWR_500mW`, `PWR_1000mW` | Transmit power. At `PWR_250mW` and above the fan output (PB9) is switched on; the R9M needs the fan mod there. Out-of-range values are rejected when you build. |

Use `PWR_10mW` for bench tests with the modules close together. Running the two ends at
different powers is allowed.

### Buffers

| Define | Default | Description |
|---|---|---|
| `LINK_TX_FIFO_SIZE` | `2048` | Bytes buffered between serial input and the radio. Covers bursts from the flight controller or ground software. Power of 2. |
| `LINK_RX_FIFO_SIZE` | `1024` | Bytes buffered between the radio and serial output. A packet is only acknowledged once it fits here, so a slow port holds back the far end instead of losing data. Power of 2. |

RAM use with the defaults is about 5.6 KB (UART) or 7.7 KB (USB) of the 20 KB available,
so both buffers can be raised. The power-of-2 rule and the 32768 maximum are checked
when you build; RAM overflow is reported by the linker.

### Timing and link state

| Define | Default | Description |
|---|---|---|
| `LINK_TIMEOUT_MS` | `1000` | Link counts as down (red LED, input dropping) after this long without a valid packet. |
| `LINK_SLAVE_COAST_MS` | `1500` | Slave only: after losing the master, keep hopping blind for this long before parking on the sync channel to search. Longer rides out short fades; shorter re-acquires faster after a master reboot. |

### LEDs

| Define | Default | Description |
|---|---|---|
| `LINK_LED_RED_PIN` | `PA11` (UART), `UNDEF_PIN` (USB) | Red LED pin; `UNDEF_PIN` disables it. In USB builds PA11/PA12 are the USB lines, and using them is rejected when you build. |
| `LINK_LED_GREEN_PIN` | `PA12` (UART), `UNDEF_PIN` (USB) | Green LED pin. |

## Fixed in `platformio.ini`

| Flag | Value | Why |
|---|---|---|
| `HSE_VALUE` | `12000000U` | The R9M's 12 MHz crystal; `src/clock_r9m.cpp` derives 72 MHz from it. |
| `VECT_TAB_OFFSET` | `0x4000U` | The app starts after the 16 KB bootloader. |
| `RXBuffSize` | `128` | Radio driver receive buffer, the upper limit for `LINK_PKT_LEN`. |
| `SERIAL_RX_BUFFER_SIZE` / `SERIAL_TX_BUFFER_SIZE` | `256` | Arduino UART buffers. |
| `HWSERIAL_NONE` | — | Stops the core creating its own `Serial` on USART1. |
