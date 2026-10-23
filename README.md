# PlantCare

Automatic plant watering with one small battery-powered node per plant. Each plant has its own bottle of water with a little pump inside, a soil-moisture sensor, and an ESP32-C3/S3 node that decides when and how much to water. A main station (an ESP32 on USB power) shows everything in a web dashboard, keeps the history and alerts, and passes your commands to the nodes by radio. There is no server, no cloud, and no wiring between plants.

```
 home Wi-Fi ── main station (dashboard) ──radio── node ─ pump in the bottle ─► pot
                                         ├─radio── node ─ …
                                         └─radio── node ─ …
```

## What it does

- **Per-plant profiles** from a built-in library of 42 houseplants, succulents and herbs: moisture band, light, temperature, humidity, watering style, resting months and care notes. Every value can be edited.
- **Pulse-and-soak watering.** Small doses, a pause for the water to reach the sensor, then a re-measure. Each node learns how much water its plant needs per 1 % of moisture and deliberately undershoots, which prevents the classic sensor-lag overwatering.
- **Responds to the room.** Hot, dry, bright air starts watering a little sooner; cold and resting months reduce doses.
- **No level sensor, still knows when the bottle is empty.**
  - A fuel gauge counts what the pump moves, and one press of the node's button after refilling resets it.
  - The pump's own current tells when it starts sucking air, and also catches an unplugged or a jammed pump.
  - If watering stops making the soil wetter, that counts too.
  - Every empty bottle teaches the gauge the pump's real flow.
- **Each node works on its own.** If the main station is off, every plant keeps being watered with its last settings. A crash or a power cut never waters twice, and the worst case is one bottle of water in its own pot.
- **Months on 3×AA.** Nodes sleep between check-ins (every 5 minutes), power the soil sensor only while reading, and report their battery level.
- **Plug and adopt.** Put batteries in a new node and the dashboard shows *New plant node found*: pick the species and you are done. No MAC addresses, channels or Wi-Fi passwords on the nodes.
- **Dashboard** at `http://plantcare.local`:
  - Plants with their bottle level, batteries and signal.
  - Moisture and bottle charts from 24 h to a year, and the watering log.
  - Care recommendations, alerts and room climate.
  - Calibration and flow-measuring helpers, settings, CSV export and firmware updates.
- **Phone notifications** through [ntfy](https://ntfy.sh).

## Status

The software is complete and tested on the PC: 174 firmware tests, including month-long simulations of plants, bottles and pumps, and node ↔ station conversations over a radio that loses frames. There are also 36 dashboard tests and builds for the main station and all three node boards. **It has not run on real hardware yet.** Start with the [bench spike](firmware/tools/spike/README.md) on one node, then follow the checklist in [docs/getting-started.md](docs/getting-started.md#7-bring-up-checklist).

## Quick start

```sh
make test                          # firmware tests + simulations on this PC, dashboard lint/types/tests
make mock                          # the dashboard against a simulated station and nodes: http://localhost:5173, password "plantcare"
make all                           # build the dashboard, embed it, build the main station and the node boards
make flash                         # flash the main station over USB (see docs/getting-started.md for WSL)
make flash-node BOARD=supermini_c3 # flash a plant node (supermini_c3, xiao_c3, xiao_s3)
make ota DEVICE=plantcare.local PASSWORD=…   # update the main station over Wi-Fi
```

Needs PlatformIO Core and Node 24; see [docs/getting-started.md](docs/getting-started.md).

## Repository

```
firmware/
  lib/plantcore/   the node's watering logic as plain C++17: engine, planner, safety, dosing, bottle gauge, pump monitor, faults, alerts, JSON/CSV
  lib/plantlink/   the radio protocol (frames + HMAC), the node's session, the station's fleet and station core
  test/            Unity tests, multi-day simulations and lossy-radio conversations (pio test -e native)
  src/node/        plant node firmware (ESP32-C3 SuperMini, XIAO ESP32-C3, XIAO ESP32-S3)
  src/hub/         main station firmware (ESP32 DevKit): Wi-Fi, REST, dashboard, ntfy, SD, ESP-NOW
  tools/spike/     bench sketch: sleep current, pump current wet vs dry, soil ADC, radio range
  include/         pin maps (boards.h for nodes, pins.h for the station) and constants
dashboard/         React + TypeScript + Vite; built and compiled into the main station's firmware
shared/api-fixtures/  example API responses checked by both firmware and dashboard tests
docs/              architecture, hardware, control logic, API, alerts, getting started
```

## Documentation

- [Getting started](docs/getting-started.md): tools, building, flashing, first boot, adding plants, checklist
- [Shopping list](docs/hardware/bom.md) · [Wiring](docs/hardware/wiring.md) · [Assembly](docs/hardware/assembly.md)
- [How watering decisions are made](docs/control-logic.md)
- [Architecture](docs/architecture.md): nodes, the main station and the radio conversation
- [REST API](docs/api.md) · [Alerts](docs/alerts.md)

## Safety

Keep each node box above its bottle's water line, and size each bottle so the pot and saucer can hold all of it. Then even a pump stuck on can only move one bottle of water into its own pot. Test the dry-run and unplugged-pump behaviour on the bench before leaving plants unattended.
