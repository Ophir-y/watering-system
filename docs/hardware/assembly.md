# Assembly

## Moisture sensors

- **Waterproof the top.** The electronics at the top of a capacitive sensor are not sealed. Coat the components and the board edges with conformal coating or clear nail varnish, and slide heat-shrink over the top half. Moisture there makes readings drift.
- **Only the probe goes in the soil**, up to the line printed on it, never deeper.
- **Same depth, same spot.** Insert it about a third of the way from the pot edge, into the root zone, not touching the pot wall. Once calibrated, do not move it: a different depth or position gives different numbers.
- **Not under the drip.** Put the watering point on the opposite side of the pot so the sensor reads the soil, not the stream of water passing it. If watering is unusually frequent, this is the first thing to check.
- **Firm contact.** Press the soil gently around the probe; air gaps read as dry.

## Reservoir and load cell

- Use an opaque container with a lid: less algae, less evaporation, nothing falls in.
- Mount the load cell between two rigid plates (plywood or 3 mm aluminium), bolted at opposite ends of the bar so it can flex. The arrow on the cell shows the direction of force. Put the reservoir on the top plate, centred.
- Nothing else may rest on the reservoir or touch it: tubes should hang loosely so they do not carry weight.
- Calibrate the scale in the dashboard (*Room & reservoir → Calibrate the scale*): tare the empty container, pour in a measured litre, save.

## Float switch

Mount it so it trips with enough water left to cover the pump intakes or the master valve outlet: about 2–3 cm above the bottom. It is the independent backup to the scale: either one reporting "empty" stops all watering.

## Leak sensor

Lay it flat where water would collect first if something leaks: under the reservoir or in the lowest drip tray. When it gets wet, every outlet closes and watering stays stopped until you resume from the dashboard.

## Tubing

### Pumps

- Put the pump inlet tubes at the bottom of the reservoir, weighted, but above any sediment.
- Run each outlet tube to its pot and fix the end with a drip stake.
- A peristaltic pump blocks flow when stopped, so there is no siphon. If you ever use **submersible** pumps instead, the outlet must end above the reservoir's water level or have an anti-siphon hole, otherwise the reservoir drains through the pot after the pump stops.

### Gravity

- Reservoir outlet → small filter → **master valve** → manifold (tees) → one valve per plant → tube to the pot.
- Keep the drop (reservoir bottom to drip outlet) the same for all plants if you can, and enter it in each plant's outlet settings.
- Fill the lines and open each valve once by hand (*plant page → Run 10 s*) to push air out.

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
6. Test the interlocks: lift the float switch (watering locks out), wet the leak sensor (everything stops; the buzzer sounds outside quiet hours), unplug a moisture sensor (fault alert).
7. Connect the rest, one plant at a time.
