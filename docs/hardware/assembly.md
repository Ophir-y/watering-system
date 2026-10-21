# Assembly

## Moisture sensors

- **Waterproof the top.** The electronics at the top of a capacitive sensor are not sealed. Coat the components and the board edges with conformal coating or clear nail varnish, and slide heat-shrink over the top half. Moisture there makes readings drift.
- **Only the probe goes in the soil**, up to the line printed on it, never deeper.
- **Same depth, same spot.** Insert it about a third of the way from the pot edge, into the root zone, not touching the pot wall. Once calibrated, do not move it: a different depth or position gives different numbers.
- **Not under the drip.** Put the watering point on the opposite side of the pot so the sensor reads the soil, not the stream of water passing it. If watering is unusually frequent, this is the first thing to check.
- **Firm contact.** Press the soil gently around the probe; air gaps read as dry.

## Reservoir: the Coca-Cola bottle

The bottle stands **upside down** in a holder on the scale. The master valve is fitted to its cap, so water leaves through the cap by gravity. The base is cut off: that open top lets air in as the water drains, and it is where you refill.

```
        ┌ ─ ─ ─ ─ ─ ┐   cut-off base resting on top: keeps dust out, lets air in
        │           │
        │   water   │   2 L bottle, upside down, wrapped against light
        │           ▐▌  level sensor, on the wall just above where the bottle narrows
     ┌──┤           ├──┐
     │   ╲         ╱   │   holder: plant pot with a hole cut in its bottom
  ═══╧════╲_______╱════╧═══   scale top plate with a hole, reaching past the shelf edge
            │     │         cap with the bulkhead fitting
           ┌┴─────┴┐
           │ valve │        master valve, hanging free
           └───┬───┘
               ╰──╮         outlet tube: a loose loop, clipped to the shelf, on to the manifold
```

1. **Prepare the bottle.** Rinse it with hot water until nothing feels sticky: sugar left inside grows mould. Cut the base off just above the feet, about 4 cm from the bottom, and smooth the edge. Keep the base: it becomes the cover.
2. **Fit the cap.** Drill the centre of the cap to the thread size of the bulkhead fitting. Push the fitting through from the inside with a rubber washer under its flange, put the second washer and the nut on the outside, and tighten firmly.
3. **Attach the master valve** directly below the cap, flow arrow pointing away from the bottle: push its inlet onto the fitting's barb (or join them with 2–3 cm of tube) and secure it with a small hose clamp or zip tie. Keep the joint short, so the valve hangs straight and is carried by the bottle.
4. **Leak test.** Screw the cap on, stand the bottle neck-down in a jug, fill it and leave it for an hour. The cap, fitting and valve must not drip. The valve is normally closed, so it holds without power.
5. **Level sensor.** Stick the XKC-Y25 straight onto the plastic at the bottom of the straight part, just above where the bottle narrows toward the neck. It reports "empty" with roughly 250 ml left, which keeps air out of the tubing. With water behind it, its LED should be on; without, off. If it does not switch cleanly, turn its sensitivity screw, if yours has one. It is the independent backup to the scale: either one reporting "empty" stops all watering.
6. **Block the light.** Wrap the bottle in opaque tape or paint it, around the sensor. Leave a thin vertical strip clear if you want to see the level.
7. **Holder and scale.** Cut a hole in the bottom of the plant pot just big enough for the cap and valve to pass through, so the bottle's shoulder rests on the pot. Mount the load cell between two rigid plates (plywood or 3 mm aluminium), bolted at opposite ends of the bar so it can flex; the arrow on the cell shows the direction of force. Let the top plate reach past the edge of the shelf, cut a matching hole there, and stand the pot over it. The valve then hangs in free air below.
8. **Nothing else touches the weighed part.** The bottle, pot, valve and top plate must not touch the shelf or wall. The outlet tube, the valve's two wires and the sensor cable all hang from it: give each one a loose loop and clip it to the shelf (not the scale) a few cm away. Each then always carries the same small weight, which the tare removes. A tight tube or wire changes the reading every time it is touched.
9. **Fill and cover.** Fill to about 2 cm below the cut edge and rest the cut-off base on top as a cover. It must not seal: if no air gets in, the bottle dents inward and the flow stops.

**Refilling:** pour from a jug into the top while the bottle stays in place; the valve, tube and wires stay connected. The scale logs the refill by itself.

Calibrate the scale in the dashboard with the empty bottle, cap and valve in place ([getting started, step 6](../getting-started.md#6-reservoir-scale)).

## Leak sensor

Lay it flat where water would collect first if something leaks: under the bottle's valve or in the lowest drip tray. When it gets wet, every outlet closes and watering stays stopped until you resume from the dashboard.

## Tubing

### Pumps

- Put the pump inlet tubes at the bottom of the reservoir, weighted, but above any sediment.
- Run each outlet tube to its pot and fix the end with a drip stake.
- A peristaltic pump blocks flow when stopped, so there is no siphon. If you ever use **submersible** pumps instead, the outlet must end above the reservoir's water level or have an anti-siphon hole, otherwise the reservoir drains through the pot after the pump stops.

### Gravity

- Bottle cap → **master valve** → small filter → manifold (tees) → one valve per plant → tube to the pot.
- Fix the filter, manifold and plant valves to the shelf or wall. Only the master valve hangs from the bottle.
- Every drip outlet must be lower than the cap. Keep the drop (cap to drip outlet) the same for all plants if you can, and enter it in each plant's outlet settings.
- Fill the bottle and open each valve once by hand (*plant page → Run 10 s*) to push the air out of the lines.

## Electronics box

- Keep 12 V wiring and the MOSFETs on one side and the ESP32, ADS1115s and sensor terminals on the other.
- Use screw terminals for everything that leaves the box, and label them with the channel numbers.
- Leave the BOOT button reachable. A short press is the physical "stop all watering" button.
- Mount the box higher than the plants and the reservoir, never under them.

## First power-up checklist

1. With the pumps/valves **disconnected**, power up and check the status LED: solid while booting, then a short flash every 3 s.
2. Join the **PlantCare-XXXX** hotspot (password `plantcare`) and open `http://192.168.4.1`. Run the setup wizard.
3. Add one plant and calibrate its sensor: dry point in the air, wet point in a glass of water.
4. Calibrate the scale.
5. Connect one pump/valve, put its tube into a measuring cup, and use *Run 10 s* on the plant page to measure the flow.
6. Test the interlocks: unplug the level sensor's yellow wire (watering locks out), wet the leak sensor (everything stops; the buzzer sounds outside quiet hours), unplug a moisture sensor (fault alert).
7. Connect the rest, one plant at a time.
