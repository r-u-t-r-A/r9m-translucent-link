# r9m-translucent-link

Firmware that turns two FrSky R9M modules (STM32F103 + SX1276, 900/868 MHz) into a
bidirectional **transparent serial link**: bytes in on one end come out of the other,
complete and in order. Use it for MAVLink (ArduPilot), MSP (Betaflight) or any other
byte stream.

* **Small and fixed at build time:** about 22-24 KB of flash (about 33-35 KB with USB).
  No Lua, no handset/CRSF, no menus, no settings stored on the module.
* **Reliable:** each direction resends a payload until the other end acknowledges it,
  so radio loss costs latency rather than bytes.
* **End-to-end flow control:** a payload is only acknowledged once it fits in the
  receiver's output buffer, so a slow port slows the sender instead of losing data.
* **Reboot-safe:** each boot draws a random session ID, so either side resynchronises
  cleanly when the other one restarts.
* **MAVLink mode** (`LINK_MAVLINK=1`): `RADIO_STATUS` flow control and RSSI reporting,
  like a SiK telemetry radio, so ArduPilot throttles itself to the link's rate.
* **Frequency hopping** using the ExpressLRS sub-GHz channel plans.
* **Serial port:** a UART, or (with a small hardware mod) the STM32's own USB port as a
  virtual COM port.

> **Status:** build-tested, and the protocol logic is unit-tested, but it has not yet
> run on real R9M hardware. Bench-test before you rely on it: two modules, two
> USB-serial adapters, a script that streams data and compares it on both sides.

## Documentation

* **[Setup guide](docs/setup.md):** which module goes on the ground and which on the
  craft, wiring, ArduPilot/Betaflight and ground software settings, a first bench test,
  troubleshooting.
* **[Configuration reference](docs/configuration.md):** every option for
  `link_defines.txt`, the build environments, and which settings must match on both
  modules.

## Quick start

1. Edit `link_defines.txt`: set `MY_BINDING_PHRASE` and one `Regulatory_Domain_…`.
   This file is shared by both modules. Settings that differ per end (baud, power,
   pins) go in an optional `link_defines_<env>.txt`, e.g. `link_defines_slave.txt`.
2. Flash the **ground station** module as master and the **craft** module as slave
   (the craft then only transmits in reply to the ground station; see the
   [setup guide](docs/setup.md#1-which-module-goes-where)):

   ```
   pio run -e master -t upload        # ground station (master_usb with the USB mod)
   pio run -e slave  -t upload        # craft
   ```

3. Fit the antennas, wire the serial ports, power both modules, and wait for solid
   green on both.

## Flashing

* **ST-Link** (`-t upload`): writes the ExpressLRS R9M bootloader at `0x08000000` and
  the firmware at `0x08004000`, using `st-flash`. A factory FrSky module may be
  read-protected and need a full chip erase first (STM32CubeProgrammer, or
  `st-flash erase`).
* **From the radio:** once a module has the ELRS R9M bootloader, flash
  `.pio/build/<env>/firmware.elrs` the same way as an ExpressLRS R9M update
  (EdgeTX/OpenTX "flash external ELRS").

## Serial port

### UART (envs `master`, `slave`)

USART1: **PA10 = RX into the module, PA9 = TX out of the module**, 3.3 V logic, 8N1,
115200 baud by default (`LINK_UART_BAUD`). These are the pads the R9M WiFi backpack mod
uses. For another USART, set `LINK_UART_RX_PIN` / `LINK_UART_TX_PIN`.

### USB (envs `master_usb`, `slave_usb`)

The module appears as a USB CDC serial port (`/dev/ttyACM*`, a COM port on Windows).
There's no baud rate to match, and USB flow control holds the PC back when the radio
link falls behind, so nothing overflows on the input side.

On the R9M, the STM32's USB pins drive the two LEDs, so this needs a hardware mod:

| Signal | Connect to |
|---|---|
| USB D− | PA11, with the red LED disconnected |
| USB D+ | PA12, with the green LED disconnected, **plus a 1.5 kΩ pull-up to 3.3 V** |
| GND | GND |
| VBUS (5 V) | module supply, only if your board's regulator accepts 5 V |

The F103 has no internal USB pull-up. At boot the firmware briefly drives D+ low so
that the PC re-enumerates the device after a reset. In USB builds the LEDs are off,
unless you move them to free pins with `LINK_LED_RED_PIN` / `LINK_LED_GREEN_PIN`.

Data from the air is only delivered while a program has the port open (DTR set);
otherwise it is discarded, like a cable with nothing on the other end. While the port is
open, a program that reads slowly holds the link back instead of losing bytes. For host
software that never sets DTR, build with `-DLINK_USB_IGNORE_DTR=1`; data then waits
until it is read.

## LEDs

| LED | Meaning |
|---|---|
| solid green | link up |
| red, slow blink | link down |
| red, fast blink (slave) | parked on the sync channel, searching for the master |
| red/green alternating | radio chip not found |

## Radio presets (`LINK_PRESET`)

| # | Modulation | Packet | Frame | Payload, each direction |
|---|---|---|---|---|
| 1 (default) | SF6 BW500 CR4/5 (ELRS 200 Hz modulation) | 64 B | 35 ms | ~1600 B/s |
| 2 | SF7 BW500 CR4/5 | 64 B | 59 ms | ~940 B/s |
| 3 | SF8 BW500 CR4/7 | 48 B | 110 ms | ~360 B/s |
| 4 | SF9 BW500 CR4/7 | 32 B | 145 ms | ~165 B/s |

Both directions get this rate at the same time. Round-trip latency is about 1-2 frames
when no resends are needed. All other options (power, pins, buffers, timeouts, custom
LoRa parameters) are listed in the [configuration reference](docs/configuration.md).

## Usage notes

* **ArduPilot:** build both modules with `-DLINK_MAVLINK=1`, then set
  `SERIALn_PROTOCOL=2` and `SERIALn_BAUD` = `LINK_UART_BAUD`/1000. Without MAVLink mode,
  ArduPilot sends faster than the link carries and the ground station can't connect.
  Lower stream rates still leave more room for parameters and commands.
* **Betaflight:** enable MSP on the UART. The configurator works, but slowly, because
  every MSP request costs a round trip.
* Bytes are only lost if a port feeds data faster than the link carries it for longer
  than the 2 KB input buffer lasts. By default, input that arrives while the link is
  down is discarded (`LINK_DROP_INPUT_WHEN_DISCONNECTED`), so you don't get a burst of
  stale data on reconnect.
* A reboot has about a 1-in-256 chance of drawing the same session ID as before. In
  that case one payload around the reboot can be lost or repeated.

## How it works

Each frame, the master hops to the next channel and sends one packet, the slave
answers on the same channel, and both move on. Every master packet carries its hop
index, so the slave locks on from any single packet. If the slave loses the master it
keeps hopping blind for `LINK_SLAVE_COAST_MS`, then parks on the sync channel, which the
master visits once per pass through the channel list. All radio SPI access happens in
`loop()`; the DIO0 interrupt only sets a flag.

Packet format and ARQ: `src/link_arq.h`. Timing and radio: `src/link_main.cpp`.

### Clock

The Arduino `bluepill_f103c8` board definition assumes an 8 MHz crystal and sets the
PLL to x9. With the R9M's 12 MHz crystal, that runs the F103 at 108 MHz (its maximum is
72 MHz) and the radio SPI at 13.5 MHz (the SX1276 maximum is 10 MHz). It also makes USB
impossible. `src/clock_r9m.cpp` sets 12 MHz x6 = 72 MHz, with 48 MHz for USB and 9 MHz
SPI.

## Tests

```
pio test -e native
```

These simulate the frame exchange with packet loss, bit corruption, a slow serial port
and hundreds of reboots on either side, and check that both byte streams arrive exactly.
The MAVLink mode tests check the frame tracking, whole-frame dropping on overflow, and
the `RADIO_STATUS` encoding against frames generated with pymavlink.

## Origin and licence

The radio driver (`lib/SX127xDriver`, `lib/SX12xxDriverCommon`, `lib/RFAMP`,
`lib/SPIEx`), the R9M power-amp DAC driver (`lib/DAC`), the frequency-hopping tables
and sequence (`lib/FHSS`, reduced), `src/hal_stm32.cpp`, the linker scripts and
`bootloader/r9m_bootloader.bin` come from [ExpressLRS](https://github.com/ExpressLRS/ExpressLRS)
3.x (`3.x.x-maintenance`, commit `7dee9eed`). The R9M pin map in `include/target_r9m.h`
comes from its `Frsky_TX_R9M.h` target. ExpressLRS is GPL-3.0, so this project is too
(see `LICENSE`). Parts of the SX127x register definitions are MIT-licensed, from
LoRaLib (`lib/SX127xDriver/license.txt`).

Nothing here enforces EU 868 MHz duty-cycle or LBT rules. Operating legally where you
are is your responsibility.
