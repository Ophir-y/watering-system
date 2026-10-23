# Wiring

## Plant node

Every node has the same six connections; only the pin numbers differ between boards. Everything matches `firmware/include/boards.h`.

```
 3×AA (+) ─────┬──────────────────────────┬───────────────► board 5V pin
               │                          │
               │                        1 MΩ
               │                          ├──────────────► BAT pin (ADC)
          ┌────┴────┐                   1 MΩ     100 nF from BAT pin to GND
          │  pump + │  1N5819 across       │
          │  pump − │  the pump (stripe    │
          └────┬────┘  towards +)          │
               │                           │
          MOSFET drain                     │
          MOSFET gate ──── PUMP pin        │       100 kΩ from gate to GND
          MOSFET source ─┬─ SENSE pin (ADC)│
                         │                 │
                        1 Ω                │
                         │                 │
 3×AA (−) ───────────────┴─────────────────┴───────────────► board GND

 soil sensor  VCC ── SOIL PWR pin     (powered only while it is read)
              GND ── GND
              AOUT ── SOIL pin (ADC)
```

- The **pump** is switched on its low side. Its current flows through the 1 Ω resistor, so the voltage on the SENSE pin in millivolts equals the pump current in milliamps.
- The **battery divider** halves the battery voltage for the ADC. Use 1 MΩ resistors so it draws only ~2 µA. The 100 nF capacitor lets the ADC sample such a high-impedance source.
- The **soil sensor** is powered from a GPIO, about 5 mA for 0.3 s per reading, so it draws nothing while the node sleeps.
- The **BOOT button** on the board is the node's only button. Short press: "bottle refilled", and check in now. Hold 5 s: pair again.

### Pins per board

| Connection | ESP32-C3 SuperMini | XIAO ESP32-C3 | XIAO ESP32-S3 |
|---|---|---|---|
| SOIL (sensor AOUT) | GPIO0 | D1 / GPIO3 | D0 / GPIO1 |
| SENSE (top of the 1 Ω) | GPIO1 | D2 / GPIO4 | D1 / GPIO2 |
| BAT (divider middle) | GPIO3 | D0 / GPIO2 | D3 / GPIO4 |
| SOIL PWR (sensor VCC) | GPIO4 | D3 / GPIO5 | D4 / GPIO5 |
| PUMP (MOSFET gate) | GPIO10 | D10 / GPIO10 | D5 / GPIO6 |
| Button | BOOT (GPIO9) | BOOT (GPIO9) | BOOT (GPIO0) |
| LED | GPIO8 (blue, on board) | none | GPIO21 (on board) |
| Battery + | 5V pin | 5V pin | 5V pin |

Analog inputs use ADC1 only (ADC2 does not work while the radio is on). No output sits on a strapping pin, so the pump cannot twitch while the board boots.

**SuperMini:** remove the red power LED (desolder it, or cut its trace). It is always on and would otherwise use more current than everything else together.

**USB and batteries:** switch the battery holder off before plugging in USB to flash or debug.

### If the dry soil reading looks clipped

The ESP32-C3's ADC flattens out above about 2.5 V. Some sensors read up to ~2.6 V in dry air. If the bench spike shows dry readings stuck near 2450–2500 mV, add a divider on the sensor output (100 kΩ from AOUT to the SOIL pin, 330 kΩ from the SOIL pin to GND) and calibrate again. The XIAO S3 reads up to ~3.1 V and does not need it.

## Main station

```
 USB charger ──► ESP32 DevKit (USB)

 ESP32 3V3 ──► SHT4x, BH1750, DS3231, SD module (all optional)
 GPIO21 SDA ── SHT4x, BH1750, DS3231   (4.7 kΩ pull-ups to 3V3 if the modules lack them)
 GPIO22 SCL ── SHT4x, BH1750, DS3231
 GPIO5  SD CS · GPIO18 SCK · GPIO19 MISO · GPIO23 MOSI
 GPIO0  BOOT button (on board): hold 5 s to open the setup hotspot
 GPIO2  status LED (on board)
```

| GPIO | Function | Notes |
|---|---|---|
| 21 / 22 | I²C SDA / SCL | SHT4x 0x44, BH1750 0x23, DS3231 0x68 |
| 5, 18, 19, 23 | microSD CS, SCK, MISO, MOSI | FAT32 card |
| 0 | BOOT button | Hold 5 s: setup hotspot for 15 minutes |
| 2 | Status LED | Heartbeat when fine; 1 Hz warning; 4 Hz critical; double flash = hotspot; solid = stopped |

Put the main station where it reaches every plant. ESP-NOW has about the same range as Wi-Fi; the [range test](../../firmware/tools/spike/README.md#range-test) tells you before you build everything. If the climate sensors are fitted, keep the station in the same room as the plants: it measures the air for all of them.
