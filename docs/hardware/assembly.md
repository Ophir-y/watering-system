# Assembly

## A plant unit

```
            ┌─ node box (board, MOSFET, batteries)
            │
   tube ────┼───────────────╮
            │               │ drip end clipped to the pot rim,
  ┌──────┐  │            ┌──┴──────┐  opposite side from the sensor
  │ lid  │◄─┘ wires      │  pot    │
  │~~~~~~│               │   ║     │ soil sensor, up to its line
  │ pump │  1 mm hole in │   ║     │
  └──────┘  the tube     └─────────┘
   bottle   above the
            water line
```

### The bottle and pump

1. **Any bottle or jar with a lid works.** Choose the size so that the pot and its saucer can take a full bottle without overflowing. That way, even if everything failed at once, the worst case is one bottle of water in its own pot.
2. Put the pump on the bottom of the bottle. Run the tube and the pump wires out through a hole in the lid (or the neck), and leave a small gap so air can get in as the water level drops.
3. **Stop siphoning with a 1 mm hole.** If the pot's drip end is lower than the water in the bottle, water keeps flowing after the pump stops. Pierce a 1 mm hole in the tube just inside the bottle, above the full water line. When the pump stops, air enters there and breaks the siphon. A few drops come out of the hole while pumping; they fall back into the bottle.
4. Clip the tube's end to the pot rim, on the side **away from** the moisture sensor.
5. Dark bottles, or a sleeve of opaque tape, keep algae out.
6. Fill the bottle and note how much you poured in. Enter that as the plant's **Bottle holds** amount.

### The soil sensor

- **Waterproof the top.** The electronics at the top of a capacitive sensor are not sealed. Coat the components and the board edges with conformal coating or clear nail varnish, and slide heat-shrink over the top half.
- Insert **only the probe**, up to the line printed on it, about a third of the way in from the pot edge.
- Not under the drip, and the same depth and spot once calibrated.
- Press the soil gently around the probe; air gaps read as dry.

### The node box

- Mount the board, MOSFET, resistors and battery holder in a small box beside the pot, higher than the bottle's water line, never under the pot or the saucer.
- Keep the battery switch and the BOOT button reachable: the button is how you tell the node "I refilled the bottle".
- Strain-relieve the pump and sensor cables where they leave the box.

## Main station

The ESP32 DevKit on a USB charger, with the optional climate sensors, RTC and SD card wired as in [wiring.md](wiring.md#main-station). Mount the climate sensors away from the charger and direct sun, at about plant height.

## First power-up

1. **Main station first.** Flash it, power it, and run the setup wizard (see [getting started](../getting-started.md#4-first-boot-the-main-station)).
2. **One node, pump not in water yet.** Flash it over USB, unplug USB, switch the batteries on. The LED (if the board has one) flashes twice: not paired. Within a minute the dashboard shows **New plant node found**.
3. **Add it as a plant.** Pick the species and enter the bottle size. At its next check-in the node pairs, gets its settings and flashes once.
4. **Calibrate the sensor** in air and in a glass of water (plant page, *Sensor and node*).
5. **Measure the flow:** fill the bottle, hold the tube in a measuring cup, and on the plant page choose *Run 10 s*. The run happens at the node's next check-in. Enter the amount from the cup and *Save flow*.
6. **Check the safety behaviour:**
   - Lift the pump out of the water during a run. After the pump has learned its current (a few waterings), it should stop and the bottle should show *empty: the pump ran dry*.
   - Unplug the pump: *Pump drew no current*, and the pump stays locked until you clear it.
   - Pull the batteries: the plant shows *Node offline* after three missed check-ins (about 16 minutes).
7. Put the probe and the tube into the pot, fill the bottle, and press the node's button.
