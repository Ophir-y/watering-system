# Shopping list

PlantCare has two kinds of hardware: a **plant node** for every plant, and one **main station**. Prices are rough AliExpress/Amazon figures.

## Plant node (one per plant)

Each node is battery powered and stands next to its pot with its own bottle of water. It needs no wires to anything else.

| Part | Qty | ≈ USD | Notes |
|---|---|---|---|
| **Board:** ESP32-C3 SuperMini, Seeed XIAO ESP32-C3 or XIAO ESP32-S3 | 1 | 2–7 | Any of the three. Each has its own firmware build. On the **SuperMini, remove the red power LED**: it alone would drain the batteries in a few weeks. |
| Capacitive soil moisture sensor v2.0, **TLC555** chip | 1 | 1–2 | Avoid boards with an NE555: it does not work at 3.3 V. Read by the board's own ADC; there is no ADS1115. |
| Mini submersible pump, 3–5 V DC, about 100–250 mA | 1 | 1–2 | The common "DC 3-5 V micro submersible pump". It sits inside the bottle. |
| Silicone or PVC tube to fit the pump outlet (usually 7–8 mm outer diameter) | ~1 m | 1 | From the bottle to the pot. |
| Logic-level N-MOSFET: **IRLB8721**, IRLZ44N (TO-220) or AO3400A (SOT-23 on a breakout) | 1 | 0.5 | Switches the pump. **Not a ready-made MOSFET module:** those tie their input and output grounds together, which would short out the current-sense resistor. |
| 1N5819 Schottky diode | 1 | 0.1 | Across the pump (flyback). |
| 1 Ω resistor, ¼ W | 1 | – | In the pump's ground wire, so the node can feel the pump's current. |
| 100 kΩ resistor | 1 | – | Gate pull-down: the pump stays off while the board boots. |
| 1 MΩ resistors + 100 nF capacitor | 2 + 1 | – | Battery voltage divider. |
| 3×AA battery holder with switch and lid | 1 | 1 | **Alkaline AA**, or 4×AA NiMH rechargeables (use a 4-cell holder). |
| Bottle or jar with a lid | 1 | – | The plant's reservoir. Anything from a 0.5 L jar to a 2 L bottle; it only has to fit the pump through its neck or lid. |
| Small box, perfboard or a few screw terminals, 3-core cable for the sensor | – | 1–2 | |

**About 5–8 USD per plant** plus the board (you already own the boards).

### How long the batteries last

The pump draws current only for the seconds it runs. Between check-ins the node sleeps, the soil sensor is unpowered, and the radio is off. A rough budget for 3×AA alkaline (about 2000 mAh usable down to 3.3 V):

| Where the energy goes | Per day |
|---|---|
| Light sleep, ~0.2 mA (board-dependent) | ~4.8 mAh |
| 288 check-ins (every 5 min), ~40 ms awake each | ~0.3–1 mAh |
| Pump: a plant drinking 150 ml/day at 1.5 ml/s, ~180 mA | ~5 mAh |
| **Total** | **~10–11 mAh** |

That comes to roughly **4–8 months per set**, depending mostly on how thirsty the plant is and on your board's real sleep current. The [bench spike](../../firmware/tools/spike/README.md) measures the real sleep current; use its formula with your numbers. Checking in every 10 minutes instead of 5 saves a little. The dashboard shows each node's battery and warns at 3.6 V; below 3.3 V the node refuses to start the pump.

## Main station (one)

It runs the dashboard, keeps the history and alerts, and talks to every node by radio. It drives nothing that moves water, so all of this is optional except the board.

| Part | Qty | ≈ USD | Notes |
|---|---|---|---|
| ESP32 DevKit V1 (WROOM-32) | 1 | already owned | Powered from any USB phone charger. |
| USB charger, 5 V 1 A | 1 | 3 | |
| *Optional:* SHT4x temperature/humidity sensor (I²C 0x44) | 1 | 3–6 | Room climate: nodes water a little sooner in warm, dry air. |
| *Optional:* BH1750 light sensor (I²C 0x23) | 1 | 1–2 | For the light recommendations. |
| *Optional:* DS3231 RTC module + CR2032 | 1 | 2–3 | Keeps the time without internet. Without it, NTP or the dashboard sets the clock. |
| *Optional:* microSD module + card (FAT32) | 1 | 1–2 + card | Long-term history. Without it the last day is kept in memory. |

## Not needed any more

The single-board build (one ESP32 driving eight outlets from one shared bottle on a scale) needed a 12 V supply, a buck converter, a fuse, a load cell with HX711, a level sensor, two ADS1115s, solenoid valves, a leak sensor and a buzzer. None of them are needed now: each plant has its own small bottle, so a fault can only ever empty that one bottle into its own pot.
