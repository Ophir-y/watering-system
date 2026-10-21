# Shopping list

Sized for **6 plants** (the pin map supports 8). Prices are rough single-unit online prices in USD and vary a lot by seller. You already have the ESP32 boards.

## Core (needed whichever way you deliver the water)

| Part | Qty | Notes | ≈ USD |
|---|---|---|---|
| ESP32 DevKit V1 (ESP32-WROOM-32, 4 MB flash) | 1 | Already owned. Any 38/30-pin WROOM-32 DevKit with the pins in [wiring.md](wiring.md). | – |
| Capacitive soil moisture sensor v2.0 | 6 + 2 spare | Buy boards whose timer chip is marked **TLC555** (or 7555): they run at 3.3 V. Avoid **NE555** boards: that chip needs 4.5 V and gives garbage at 3.3 V. | 1–2 each |
| ADS1115 16-bit ADC breakout | 2 | 4 inputs each. One with ADDR→GND (0x48), one with ADDR→VDD (0x49). Much more accurate than the ESP32's own ADC, and works while Wi-Fi is on. | 2–4 each |
| SHT40 / SHT41 / SHT45 temperature + humidity breakout | 1 | I²C 0x44. The firmware's driver speaks the **SHT4x** command set; SHT3x, DHT and AHT sensors are not supported. | 3–6 |
| BH1750 light sensor (GY-302 board) | 1 | I²C 0x23 (ADDR pin low/unconnected). | 1–2 |
| DS3231 real-time clock module + CR2032 | 1 | Keeps time through power cuts. On the common **ZS-042** board, remove the charging resistor (or the diode next to it) when using a non-rechargeable CR2032, or fit an LIR2032 instead. | 2–3 |
| microSD card module (SPI) + 8–32 GB microSD card | 1 | Format the card FAT32. The common blue module with a regulator and level shifter works from **5 V** (its outputs are 3.3 V); a bare 3.3 V module works from 3V3. | 1–2 + card |
| Load cell + HX711 amplifier board | 1 | **5 kg** cell: the full bottle with its valve, holder and top plate weighs about 2.5 kg. Needed for closed-loop dosing, leak and refill detection. | 3–6 |
| Non-contact liquid level sensor **XKC-Y25-NPN** (5–12 V) | 1 | Independent low-water cut-off, in place of a float switch: it sticks to the outside of the bottle, so the cap needs no second hole. Its output pulls low while there is water behind it and lets go when the level drops below it; a broken wire also reads as "empty" (fail-safe). Buy the **NPN** version, not PNP. Plus one 1N5819 diode for its output ([wiring](wiring.md#reservoir)). | 4–8 |
| Water leak sensor board (LM393 comparator with DO pin) | 1 | Lies flat under the plants / drip trays. DO goes LOW when wet. | 1 |
| Active buzzer 5 V + NPN transistor (2N2222 / BC547) + 1 kΩ | 1 | Alarm for critical alerts. | 1 |
| 12 V DC power supply, 3 A | 1 | Valves draw more than pumps (2 A is enough with pumps). Plus a DC barrel socket. | 6–10 |
| Buck converter 12 V → 5 V (MP1584 or LM2596 module) | 1 | Powers the ESP32 (via its 5V/VIN pin) and the 5 V parts. Set the output to 5.0 V before connecting anything. | 1–2 |
| Inline fuse holder + 3 A fuse (2 A with pumps) | 1 | On the 12 V input. | 1–2 |
| Logic-level N-MOSFET modules | 1 per outlet | **D4184 / AOD4184** or **LR7843** modules switch fully at 3.3 V. Do **not** use IRF520 modules: they barely turn on at 3.3 V and overheat. | 1 each |
| Flyback diodes 1N5819 (or 1N4007) | 1 per pump/valve | Across every pump or valve coil, if your MOSFET module does not already have one. | – |
| Resistors and capacitors | – | 10 kΩ (gate pull-downs, input pull-ups), 1 MΩ (ADS1115 input pull-downs), 100 nF (ADS inputs and sensor supplies). | 2 |
| Perfboard or terminal-block breakout, Dupont/JST leads, 3-core cable for sensors, heat-shrink, enclosure | – | Use screw terminals for anything that leaves the box. | 10–20 |

**Core total: roughly 50–90 USD.**

## Water delivery

The reservoir is a **2 L Coca-Cola bottle** standing upside down on the scale, with the **master valve fitted to its cap** and one valve per plant below it. How to build it: [assembly.md](assembly.md#reservoir-the-coca-cola-bottle). The firmware also supports pumps, set per plant (*Outlet: Pump / Gravity valve*), so you can switch or mix later.

### Gravity from the bottle (this build)

| Part | Qty | Notes | ≈ USD |
|---|---|---|---|
| 2 L Coca-Cola bottle with its cap | 1 + 1 spare | Standard 28 mm screw cap. Holds about 1.8 L once the base is cut off. Rinse it well: leftover sugar feeds mould. | – |
| Small nylon bulkhead hose-barb fitting with nut and 2 rubber washers | 1 + 1 spare | Goes through the centre of the cap. Its flange must sit flat inside the cap, so pick a small one (M8–M10 or G⅛ thread). The barb should fit the master valve's inlet. | 1–2 |
| 12 V normally-closed **direct-acting** ("zero pressure", "0–x MPa") solenoid valve | 6 + 1 master | The head here is only 0.3–1 m of water. The cheap ½" plastic valves are usually **pilot-operated** and need at least 0.02 MPa (≈ 2 m of water): from a shelf they open weakly or not at all, and may weep. Check the listing says minimum pressure **0 MPa**. The master valve hangs from the cap, so pick a light one with ¼" barbs (under ~100 g). | 6–12 each |
| Tubing (¼" / 6–8 mm), barbed tees, drip emitters, small inline filter, small hose clamps or zip ties | – | Master valve → filter → manifold of tees → plant valves → pots. | 10 |
| Bottle holder: a plastic plant pot about 12 cm across | 1 | Holds the bottle neck-down on the scale; the neck and valve go through a hole cut in its bottom. | 1–3 |
| Opaque tape, spray paint or a fabric sleeve | – | The bottle is clear, and algae grows in light. | – |
| A sturdy shelf | 1 | Bottle cap 30–100 cm above the pots. More height gives more flow. | – |

How it differs from pumps: silent and cheap, but the flow falls as the bottle empties (the scale measures every dose anyway, and timed doses are corrected for the water height), and a valve stuck open drains the bottle. That is why the **master valve** is mandatory (water needs both valves open) and why the scale's stuck-open detection matters. The upside of a small reservoir: the most a stuck valve or a split tube can release is about 1.8 L. Expect to refill every few days; the dashboard forecasts when.

### Peristaltic pumps (alternative)

| Part | Qty | Notes | ≈ USD |
|---|---|---|---|
| 12 V DC peristaltic dosing pump | 6 | Look for 12 V, 2–5 ml/s ("Kamoer NKP" class or generic dosing pumps). | 8–15 each |
| Silicone tubing to fit the pump (often 2×4 mm or 3×5 mm), 5 m | 1 | Plus drip stakes or simply a weighted tube end at each pot. | 5 |
| High-side switch for the pump rail | 1 | P-MOSFET (AOD4185 or IRF9540N) + NPN (2N2222) + 1 kΩ + 2 × 10 kΩ. The firmware turns it on only while a dose runs; see [wiring.md](wiring.md). | 2 |

Why you might switch: a peristaltic pump delivers an accurate volume, primes itself, and **cannot leak or siphon when unpowered**, because the rollers pinch the tube shut. The worst failure (a pump stuck on) is bounded by the rail switch, the hardware timer and the scale. With pumps, the bottle can stand upright and the pump tubes go in through the top; the cap needs no valve.

## Not needed for v1

- Your Seeeduino boards. They could become remote sensor nodes later.
- An external antenna, display or keypad: the dashboard covers them.
