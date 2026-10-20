# Wiring

Everything below matches `firmware/include/pins.h`. All logic runs at **3.3 V**; only the pumps/valves (12 V) and a few modules (5 V) use other supplies. All grounds are connected together.

## Pin map (ESP32 DevKit V1 / WROOM-32)

| GPIO | Function | Notes |
|---|---|---|
| 21 | I²C SDA | ADS1115 ×2, SHT4x, BH1750, DS3231. 4.7 kΩ pull-ups to 3V3 if your boards do not have them. |
| 22 | I²C SCL | |
| 5 | SD card CS | |
| 18 | SD card SCK | |
| 19 | SD card MISO | |
| 23 | SD card MOSI | |
| 34 | HX711 DOUT | input-only pin |
| 4 | HX711 SCK | |
| 35 | Float switch | input-only, **10 kΩ pull-up to 3V3**; switch to GND. Closed (LOW) = water present, open (HIGH) = low. |
| 36 (VP) | Leak sensor DO | input-only, **10 kΩ pull-up to 3V3**; LOW = wet. |
| 0 | BOOT button | on the board. Short press: stop all watering. Hold 5 s: open the setup hotspot. |
| 16, 17, 25, 26, 27, 32 | Outlets 1–6 | MOSFET module input, 10 kΩ pull-down to GND on each gate |
| 14, 15 | Outlets 7–8 | These pins toggle briefly at boot; harmless because the rail is off then. |
| 33 | Actuator rail | Pumps: high-side switch on the 12 V pump supply. Valves: the master valve's MOSFET. |
| 13 | Buzzer | via NPN transistor |
| 2 | Status LED | on-board LED |

Avoid GPIO 6–11 (flash), 12 (strapping: must be low at boot) and 1/3 (serial).

## Power

```
 12 V adapter ──[2 A fuse]──┬──────────────────────────► pump/valve rail (via rail switch)
                            │
                            └──► buck converter ──► 5 V ──┬──► ESP32 5V/VIN pin
                                                          ├──► SD module VCC (5 V type)
                                                          └──► buzzer (+)
 ESP32 3V3 pin ──► ADS1115 ×2, SHT4x, BH1750, DS3231, HX711 VCC, moisture sensors,
                   pull-up resistors
 GND everywhere connected (12 V supply, buck, ESP32, modules)
```

The ESP32's 3.3 V regulator supplies about 500 mA, which is plenty here. Keep the 12 V motor currents on their own thick wires back to the supply, not through the ESP32's ground pin.

## Soil moisture sensors → ADS1115

| Sensor | ADS1115 | Input |
|---|---|---|
| Plant 1–4 | 0x48 (ADDR → GND) | A0–A3 |
| Plant 5–8 | 0x49 (ADDR → VDD) | A0–A3 |

Each sensor: **VCC → 3V3**, **GND → GND**, **AOUT → ADS input**. On each ADS input add a **1 MΩ resistor to GND**, so an unplugged sensor reads 0 V and the firmware reports it as disconnected, and a **100 nF capacitor to GND** against noise. Use 3-core cable, twisted if possible; runs of 1–2 m are fine.

## Outlets

### Pumps (option A)

```
            +12 V ──► P-MOSFET source
                         │ gate ── 10 kΩ ── +12 V  (keeps it OFF by default)
                         │ gate ── NPN collector;  NPN emitter → GND
 GPIO33 ── 1 kΩ ── NPN base;  base ── 10 kΩ ── GND
                         │ drain = pump rail (+)
 pump rail (+) ──► pump 1 (+) ... pump 6 (+)      [1N5819 across each pump, stripe to +]
 pump n (−) ──► MOSFET module n  OUT−   (module V+ / V− = pump rail / GND)
 GPIO16/17/25/26/27/32 ──► MOSFET module n  SIG,  10 kΩ SIG → GND
```

A pump runs only when **GPIO33** (rail) **and** its channel pin are high.

### Gravity valves (option B)

Wire the plant valves exactly like the pumps (one MOSFET module each), and the **master valve** on its own MOSFET module driven by **GPIO33**. Put the master valve at the reservoir outlet, before the manifold, so water needs both valves open. A high-side rail switch as for the pumps is optional extra protection.

## Reservoir

- **Load cell → HX711:** red E+, black E−, white A−, green A+ (colours vary; if the reading goes down when you add weight, swap A+ and A−). HX711 **VCC → 3V3**, **DT → GPIO34**, **SCK → GPIO4**.
- **Float switch:** one wire to **GPIO35**, the other to **GND**, plus a 10 kΩ pull-up from GPIO35 to 3V3. Mount it so it is closed with water present.
- **Leak sensor:** VCC → 3V3, GND → GND, DO → **GPIO36**, 10 kΩ pull-up GPIO36 → 3V3.

## Other I²C parts

SHT4x, BH1750 and DS3231 all go on the same SDA/SCL pair, powered from 3V3. Keep the climate sensor away from the electronics box (they warm it) and out of direct sun; put the light sensor where the plants are, facing up.

## Buzzer and LED

GPIO13 → 1 kΩ → NPN base; NPN emitter → GND; collector → buzzer (−); buzzer (+) → 5 V. The status LED is the board's own LED on GPIO2.
