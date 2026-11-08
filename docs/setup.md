# Setup guide

This guide covers which module goes where, configuring and flashing, wiring both ends,
flight controller and ground software settings, and a first bench test. Every build
option is listed in [configuration.md](configuration.md).

## 1. Which module goes where

The link is symmetric for data, so it works either way round. The roles differ in
**when each module transmits**:

* The **master** sets the frame timing and hop sequence. It transmits every frame
  (about every 35 ms with the default preset), whether or not anyone answers.
* The **slave** only ever transmits in reply to a master packet it has just received.
  When it can't hear the master it stays silent and searches.

**Recommended: master on the ground station, slave on the craft.**

| | Ground station | Craft |
|---|---|---|
| Role | **master** (`master` or `master_usb`) | **slave** (`slave`) |
| Serial port | USB-serial adapter on the UART, or the USB mod | UART to the flight controller |
| Why | Constant beacon from the ground, where power and heat don't matter | Radio-silent until it hears the ground station, and silent when out of range or powered up on the bench alone |

Reasons to prefer this:

* The craft never transmits blind. A craft out of range, or powered without the ground
  station, puts nothing on air.
* The ground station is usually powered first and stays on. The craft can be plugged
  in and power-cycled freely; the slave acquires the master within one pass through
  the channel list (with the default preset, about 0.5 s on EU868 and about 1.4 s on
  FCC915).
* The USB option is most useful on the ground unit (no USB-serial adapter needed), and
  either role can use it.

Swapping the roles also works; nothing else changes.

**One master and one slave per pair.** Two masters, or two slaves, never connect. Each
pair needs its own binding phrase. One master with several slaves is not supported.

## 2. Configure

Edit `link_defines.txt` in the repository root. It is shared by every build, so both
modules get the same link settings:

```
-DMY_BINDING_PHRASE="my-own-secret-phrase"
-DRegulatory_Domain_EU_868
```

Settings that may differ between the two ends (baud rate, power, serial pins, ...) go
in an optional per-build file named `link_defines_<env>.txt`. It overrides the shared
value for that build only. Example: the craft's flight controller wants 57600 baud,
and the ground unit uses USB:

`link_defines_slave.txt`
```
-DLINK_UART_BAUD=57600
```

The ground unit is built as `master_usb` and needs no override, because USB has no baud
rate. Link settings (binding phrase, domain, preset and other radio parameters) are
refused in per-build files, so the two ends can't end up incompatible. See
[configuration.md](configuration.md) for the full list.

## 3. Build and flash

```
pio run -e master -t upload        # ground (or: master_usb)
pio run -e slave  -t upload        # craft
```

`-t upload` uses an ST-Link on the module's SWD pads and writes the ELRS R9M bootloader
plus the firmware. A factory FrSky module may be read-protected; do a full chip erase
first (STM32CubeProgrammer, or `st-flash erase`).

Once a module has the ELRS R9M bootloader, you can update it from the radio instead:
copy `.pio/build/<env>/firmware.elrs` to the SD card and flash it as an external ELRS
module.

**Always connect the antenna before powering a module.** Transmitting without an
antenna can damage the power amplifier, and the master transmits as soon as it boots.

## 4. Wiring

### Power

Power each module the way it's powered in a radio's module bay. On the craft, use a
supply that matches your module's input voltage spec, not the flight controller's 3.3 V
rail.

### UART (default: USART1)

| R9M pad | Connect to |
|---|---|
| PA10 (module RX) | TX of the flight controller / USB-serial adapter |
| PA9 (module TX) | RX of the flight controller / USB-serial adapter |
| GND | GND |

The UART uses 3.3 V logic, 8N1, at `LINK_UART_BAUD` (default 115200). Only TX/RX are
used, so turn off hardware flow control (RTS/CTS) on the other side.

For the ground end, any 3.3 V USB-serial adapter works (CP2102, CH340, FT232). The
ground software then opens the adapter's port at the ground module's `LINK_UART_BAUD`.

### USB (envs `master_usb` / `slave_usb`)

This needs a hardware mod, because on the R9M the STM32's USB pins drive the LEDs:

| USB | R9M |
|---|---|
| D− | PA11, with the red LED disconnected |
| D+ | PA12, with the green LED disconnected, plus a **1.5 kΩ pull-up to 3.3 V** |
| GND | GND |

The module shows up as a USB serial port (`/dev/ttyACM*` on Linux, a COM port on
Windows). The baud rate the software picks doesn't matter. In USB builds the LEDs are
off, unless you move them to free pins (`LINK_LED_RED_PIN` / `LINK_LED_GREEN_PIN`).

## 5. Flight controller settings

### ArduPilot (MAVLink)

For the TELEM port the slave is wired to (`n` = the SERIAL port number):

| Parameter | Value |
|---|---|
| `SERIALn_PROTOCOL` | `2` (MAVLink 2) |
| `SERIALn_BAUD` | the craft module's `LINK_UART_BAUD` / 1000, e.g. `115` or `57` |
| `BRD_SERn_RTSCTS` (if the port has it) | `0`, flow control off |

The link carries about **1.6 KB/s in each direction** with the default preset, which is
less than ArduPilot's default telemetry rates on a fast port. Lower the stream rates for
that MAVLink channel (`SRn_*` parameters), and also check the rates the ground software
requests (Mission Planner: Config → Planner → Telemetry rates). If data is produced
faster than the link carries it, the 2 KB input buffer fills and bytes are dropped; the
MAVLink parser skips damaged messages, but telemetry gets choppy.

### Betaflight / INAV (MSP)

* Ports tab: enable **Configuration/MSP** on the UART the slave is wired to, with the MSP
  baud equal to the craft module's `LINK_UART_BAUD` (default 115200).
* Connect the configurator to the ground module's port. Every MSP request costs a radio
  round trip (2-3 frames), so the configurator works but is slow. Avoid high-rate tabs.

### Anything else

The link is a plain byte pipe: no framing is added or expected, and bytes arrive in
order. Data is buffered and sent as fast as the link allows. A short pause in the
stream doesn't flush anything; partial packets are sent right away.

## 6. Ground software

* **UART + adapter:** open the adapter's port at the ground module's `LINK_UART_BAUD`.
* **USB:** open the module's port; any baud rate. The module only delivers data while a
  program has the port open. If the software connects but nothing arrives, it probably
  doesn't set DTR; rebuild the ground module with `-DLINK_USB_IGNORE_DTR=1` in its
  `link_defines_master_usb.txt`.

## 7. First bench test

Do this before trusting the link on a craft.

1. Fit both antennas. For the bench, set `-DLINK_POWER=PWR_10mW` in `link_defines.txt`
   (keeping both ends the same is simplest), and keep the modules at least a metre
   apart. Two modules right next to each other at 100 mW can overload each other's
   receivers.
2. Power the master. Its red LED blinks slowly: link down, transmitting.
3. Power the slave. Its red LED blinks fast while it searches, then both modules go
   solid green within a second or two.
4. Connect both ends to a PC (two USB-serial adapters, or USB builds) and open a
   terminal on each port. Text typed on one side appears on the other.
5. For a proper test, stream a large file or random data both ways at once and compare
   byte for byte. Then repeat while power-cycling one module, and while shielding an
   antenna. Expect pauses, but no corruption: after a reboot, the data in flight from
   the rebooted side is lost, and the stream resumes cleanly.
6. Remember to set the power back for flying.

## 8. Troubleshooting

| Symptom | Likely cause |
|---|---|
| Red/green alternating | The radio chip didn't respond: hardware fault, or a non-R9M board |
| Never goes green | Modules built with different `link_defines.txt` (phrase, domain, preset), or both built with the same role |
| Slave blinks fast forever | It can't hear the master: master not running, wrong band, antenna issue, too far apart |
| Green, but garbage on one side | Baud mismatch between the module and the device it's wired to |
| Green, data arrives in bursts or is choppy | More data offered than the link carries; lower stream rates or choose a faster preset |
| USB port opens but no data | Host doesn't set DTR; build with `LINK_USB_IGNORE_DTR=1` |
| USB device not detected | D+ pull-up missing, or LEDs still connected to PA11/PA12 |
