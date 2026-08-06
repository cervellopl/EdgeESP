# EdgeESP — hardware

## The display drives the whole design

The board in your photo is an **MCUFRIEND 3.6" TFTLCD shield for Arduino Mega 2560**:
480×320, 16-bit 8080 parallel bus, on-board microSD, 4-wire resistive touch panel,
AMS1117 3.3 V regulator (U2), and a 74-series buffer (U3).

That 16-bit bus is the single most consequential fact about this project. It needs
**21 GPIOs before anything else is connected** (16 data + WR + RD + RS + CS + RST).
Two consequences follow:

1. **Use an ESP32-S3, not a classic ESP32.** The classic ESP32 has roughly 26 usable
   GPIOs once flash and the input-only pins are excluded — not enough for the bus plus
   GPS, SD, I²C, buttons and battery sense. The S3 also has the **LCD_CAM peripheral**,
   which drives an i80 parallel bus over DMA. A full 480×320 frame costs ~8 ms of bus
   time and almost no CPU. On a classic ESP32 the same bus is emulated over I²S and is
   both slower and pin-hungrier.

2. **Use a module with quad (not octal) PSRAM** — `ESP32-S3-WROOM-1-N8`, `N16R2`, or
   plain `N8R2`. On octal-PSRAM parts (`N16R8`) GPIO33–37 are consumed by the PSRAM
   bus, and this design needs those five pins for LCD_D11–D15. `platformio.ini` already
   sets `board_build.arduino.memory_type = qio_qspi` to match.

### Buttons are on one ADC pin for the same reason

Five GPIOs for five buttons simply do not exist after the bus is wired, so the buttons
sit on a resistor ladder feeding a single ADC input. Five resistors, no extra IC. The
ladder also doubles as the deep-sleep wake source.

---

## ⚠️ Level shifting — check this before you power anything

The shield is designed for a **5 V** Arduino Mega. The resistor packs visible on the
back (RP1–RP11) exist to knock 5 V logic down to the panel's 3.3 V. **Which topology
they use decides whether you can drive the shield directly from the ESP32-S3.**

Measure with the shield unpowered, using a multimeter in continuity/resistance mode:

| What you measure | Topology | What it means |
|---|---|---|
| ~33–100 Ω from a Mega data pin to the corresponding panel pin, and **no** resistor from that panel pin to GND | Series only | ✅ Drive directly from 3.3 V. Nothing to do. |
| ~1–10 kΩ in series **and** a second resistor from the panel pin to GND | Divider | ⚠️ A 3.3 V drive arrives at the panel as ~2.0–2.4 V. Marginal. |

If it is a divider, pick one:

- **Bypass the lower leg.** Lift or desolder the pull-down resistor pack(s) and short
  the series resistors. Tedious but permanent and correct.
- **Drive the bus at 5 V** through a `74LVC245`/`74HCT245` bank (three of them for
  21 lines), powered at 5 V with the ESP32 side at 3.3 V. Adds parts and delay but is
  the textbook answer.
- **Try it first.** Many people run these shields straight off 3.3 V logic and they
  work — ~2.2 V still clears V<sub>IH</sub> on most of these panels. If you get garbage
  pixels, a blank screen, or intermittent corruption at 20 MHz, drop
  `LCD_BUS_FREQ_HZ` to 10 MHz before concluding the shifting is wrong.

Power the shield's **3V3 pin directly** from your regulator and leave its 5 V pin
unconnected — there is no reason to boost to 5 V just to drop it back down in the
on-board AMS1117.

---

## Pin map

All of these live in `include/config.h`; change them there, not in the source.

### LCD 16-bit bus

| Shield signal | Mega shield pin | ESP32-S3 GPIO |
|---|---|---|
| LCD_D0 – LCD_D7   | D22 – D29 | 9, 10, 11, 12, 13, 14, 15, 16 |
| LCD_D8 – LCD_D15  | D30 – D37 | 17, 18, 21, 33, 34, 35, 36, 37 |
| LCD_WR  | A1 | 8 |
| LCD_RD  | A0 | 47 |
| LCD_RS  | A2 | 7 |
| LCD_CS  | A3 | 6 |
| LCD_RST | A4 | 5 |
| Backlight | — | 48 (PWM, LEDC ch 7) |
| 3V3 / GND | power header | 3V3 rail / GND |

> Shield revisions differ. **Buzz out the header with a continuity tester against the
> panel flex before soldering.** The mapping above is the common MCUFRIEND Mega layout,
> not a guarantee about your specific board.

### Everything else

| Function | ESP32-S3 GPIO | Notes |
|---|---|---|
| GPS RX (← module TX) | 1 | UART1 |
| GPS TX (→ module RX) | 2 | |
| GPS 1PPS | 42 | optional |
| SD SCK / MISO / MOSI / CS | 39 / 40 / 41 / 38 | shield pins D52 / D50 / D51 / D53 |
| I²C SDA / SCL | 43 / 44 | freed by running the console on native USB |
| Button ladder | 4 | ADC1, also the wake pin |
| Battery sense | 3 | ADC1, 100 k / 100 k divider |
| Buzzer | 0 | shares the BOOT pad; set `-1` to disable |
| Spare | 45, 46 | strapping pins, leave free |

GPIO3 is a strapping pin (JTAG source select). A battery divider holds it near 2.1 V at
boot, which selects the built-in USB-JTAG — harmless. If you would rather not rely on
that, move battery sense to GPIO45 and give up one spare.

### Button ladder

```
 3V3 ──[10k]──┬────────────► GPIO4 (ADC)
              │
   LAP   ─────┼──[   0R ]── GND      ≈    0 mV
   DOWN  ─────┼──[  1k0 ]── GND      ≈  300 mV
   UP    ─────┼──[  2k2 ]── GND      ≈  595 mV
   ENTER ─────┼──[  4k7 ]── GND      ≈ 1055 mV
   BACK  ─────┴──[ 10k  ]── GND      ≈ 1650 mV
   (idle)                            ≈ 3300 mV
```

Add 100 nF from the ADC node to GND. Thresholds are in `src/input/Buttons.cpp`; if your
resistors are E24 rather than E12 values, print `analogReadMilliVolts()` and adjust.

---

## GPS

Any u-blox module with a UART works: **NEO-M8N** (cheap, 10 Hz single-GNSS),
**NEO-M9N** (concurrent GNSS, much better under trees), or **MAX-M10S** (lowest power).
The firmware is UBX-only — it disables NMEA entirely and lives on a single `NAV-PVT`
message at 5 Hz, which carries fix quality, time, position, height, ground speed,
heading and accuracy in one 92-byte frame.

Configuration is automatic: it probes for the current baud rate, tries the modern
`CFG-VALSET` path (M9/M10) and falls back to legacy `CFG-PRT`/`CFG-RATE`/`CFG-NAV5`/
`CFG-MSG` (M8). Dynamic model 4 (automotive) is the closest stock profile to cycling.

Give the module a proper antenna and a ground plane. GPS quality dominates every number
this device reports, and a 15 zł ceramic patch with no ground plane will make even
perfect firmware look broken.

---

## Sensors

**BME280** on I²C (address 0x76 or 0x77) gives barometric altitude, ascent and
temperature. Everything degrades to GPS altitude if the chip is absent, but you want it:
GPS vertical accuracy is 2–3× worse than horizontal, and ascent totals computed from it
are close to fiction. The firmware leashes the barometric reference to GPS MSL with a
~10-minute time constant, so it tracks weather drift without mistaking a climb for a
pressure change.

Mount it where it sees ambient air but not direct sun or your own airflow.

### Magnetometer — optional, but the compass page wants one

The compass page works without any extra hardware, from GPS course over ground.
**GPS course only exists while you are moving.** Below about 5 km/h it is noise, and
stopped it does not exist at all — which is precisely when someone looks at a compass.

Adding a **QMC5883L or HMC5883L** on the same I²C bus fixes that. The ubiquitous "GY-271"
module is about 5 zł; note that the silkscreen almost always says HMC5883L while the chip
on it is actually a QMC5883L, so the firmware detects both (0x0D and 0x1E) and uses
whichever answers. It also becomes the heading source for the wind dial and the
off-course arrow when you are stopped.

Two honest limits:

- **No tilt compensation.** That needs an accelerometer this design does not have.
  Readings are correct while the unit is roughly level — which a bar mount is — and
  degrade as you tilt it. Holding it up to sight along will read wrong.
- **It must be calibrated, in the case, on the bike.** A magnetometer sitting next to a
  Li-ion cell, a switching regulator and a steel handlebar reads garbage until the
  hard-iron offset is measured. Menu → **Calibrate compass**, then turn the whole unit
  slowly through a full circle. The firmware refuses to save below 75 % coverage of the
  circle rather than store a calibration that points the wrong way, and it shows NAN
  rather than a heading until one exists.

Set `MAG_DECLINATION_DEG` in `config.h` for where you ride — GPS bearings are true north
and the magnetometer reads magnetic, so without it the needle and the bearing-to-start
disagree by exactly that angle (about +6.2° in central Poland). If the heading reads a
constant number of degrees off, put that number in `COMPASS_MOUNT_OFFSET_DEG`.

Mount it as far from the battery, the regulator and the LCD ribbon as the case allows.

---

## ⚠️ ANT+ is not available — the one real gap vs. an Edge 520

The Edge 520 talks **ANT+**. ANT+ needs a Nordic nRF5x radio running the ANT
stack, or a licensed ANT chip. **The ESP32 cannot do ANT+**, and no amount of firmware
changes that.

What this firmware implements instead is the standard **Bluetooth LE** cycling profiles:

| Profile | UUID | Gives you |
|---|---|---|
| Heart Rate | 0x180D / 0x2A37 | bpm |
| Cycling Speed & Cadence | 0x1816 / 0x2A5B | wheel speed, crank cadence |
| Cycling Power | 0x1818 / 0x2A63 | watts, and cadence if the meter sends it |

Essentially every sensor sold since ~2016 is dual-band ANT+/BLE, so in practice this
costs you nothing — but an ANT+-only sensor from 2013 will not connect, ever. If you
need ANT+, the honest answer is to add an nRF52840 module over UART.

---

## Power

### Topology

```
USB-C ──► charger + power path ──┬──► Li-ion cell (1S)
                                 │
                                 └──► buck-boost 3.3 V ──► ESP32-S3, LCD, GPS, sensors
```

A **power-path** charger matters. With a plain TP4056 the load hangs off the battery
terminals, so plugging in a dynamo hub or a power bank mid-ride confuses the charge
termination and the cell never reads full. Use one of:

- **MCP73871** — proper load sharing, 1 A charge, run-while-charging. The right answer.
- **IP5306** module — cheap, integrated, includes a boost; fine if you tolerate its
  auto-shutdown on light loads.
- **TP4056 + ideal-diode load share** (e.g. an FS8205 + Schottky) — works, more parts.

For the rail, a **TPS63020 buck-boost** holds 3.3 V across the entire 3.0–4.2 V cell
range. A plain LDO drops out below ~3.5 V and throws away a third of the battery; a
plain buck cannot reach 3.3 V from a nearly flat cell.

Always include cell protection (DW01 + FS8205, or a protected cell).

### Budget

| Load | Typical |
|---|---|
| ESP32-S3, BLE active, Wi-Fi off | 80–120 mA |
| u-blox M8N, continuous tracking | 25–35 mA |
| Panel logic | ~20 mA |
| Backlight at the default level | 100–150 mA |
| **Total @ 3.3 V** | **~250–330 mA ≈ 1.0 W** |

A 3400 mAh 18650 (≈ 12.5 Wh) gives roughly **9–11 hours** at ~85 % converter efficiency.
Turning the backlight down to `BACKLIGHT_DIM` roughly doubles it.

An Edge 520 gets 15 h from a 500 mAh cell because its transflective memory-in-pixel
display needs no backlight at all. A backlit TFT is the price of the screen you chose —
it is far nicer to read, and it costs battery. That trade is inherent, not a bug.

Wi-Fi is off by default for exactly this reason: it adds ~80 mA average while on, and
the firmware force-stops it after an hour.

### Battery sense

100 k / 100 k divider from the cell to GPIO3, 100 nF to GND. The firmware averages 8
ADC reads, applies a 0.9/0.1 IIR filter, and maps voltage to percent through a Li-ion
discharge curve rather than a straight line — a linear map reads "100 %" for an hour and
then falls off a cliff.

---

## Enclosure and mounting

Not covered by this repo, but the constraints worth knowing:

- The shield is roughly **100 × 65 mm** — noticeably larger than an Edge 520 (49 × 73 mm).
  This is a handlebar-*bag*-sized device, or a big out-front mount.
- Use a **Garmin quarter-turn** mount pattern on the back. The tabs are 27 mm apart on a
  22 mm circle; printable STLs are everywhere and every bike mount on earth accepts them.
- Seal to at least IPX4. A gasketed lid, a membrane vent (Gore or a PTFE patch) so the
  barometer still reads, and conformal coating on the boards.
- Keep the GPS antenna at the top, facing up, away from the LCD's ribbon and the
  switching regulator. The panel and the buck-boost are both loud at GPS frequencies.

---

## Bill of materials

| Item | Notes |
|---|---|
| ESP32-S3-WROOM-1 **N8 / N16R2** dev board | **not** N16R8 — see above |
| MCUFRIEND 3.6" 480×320 Mega TFT shield | the board in the photo |
| u-blox NEO-M8N / M9N / MAX-M10S with antenna | UART, 3.3 V |
| BME280 breakout | I²C, 0x76 |
| QMC5883L / HMC5883L ("GY-271") | I²C, optional — compass at standstill |
| MCP73871 or IP5306 charger with power path | |
| TPS63020 buck-boost, 3.3 V out | |
| 18650 cell + holder, or a 2000–3000 mAh pouch | protected |
| 5 × tactile switches | |
| Resistors 10 k ×3, 4k7, 2k2, 1k, 100 k ×2 | ladder + divider |
| 100 nF ×2, 10 µF ×2 | decoupling |
| microSD card | Class 10, ≤32 GB, FAT32 |
| USB-C breakout | charge + console |
