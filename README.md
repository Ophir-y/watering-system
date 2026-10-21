# PlantCare

An autonomous plant-watering controller on a single ESP32. Each plant has its own soil-moisture sensor and a care profile, and all of them drink from one shared reservoir. The ESP32 decides when and how much to water, checks that every dose actually happened, detects faults, keeps working without Wi-Fi or internet, and serves its own web dashboard. There is no server and no cloud.

## What it does

- **Per-plant profiles** from a built-in library of 42 houseplants, succulents and herbs: moisture band, light, temperature, humidity, watering style, resting months and care notes. Every value can be edited.
- **Pulse-and-soak watering.** Small doses, a pause for the water to reach the sensor, then a re-measure. The system learns how much water each plant needs per 1 % of moisture and deliberately undershoots, which prevents the classic sensor-lag overwatering.
- **Responds to the room.** Hot, dry, bright air starts watering a little sooner; cold and resting months reduce doses.
- **Closed-loop dosing.** A scale under the reservoir measures what actually left, for pumps and gravity-fed valves alike. The same scale detects blocked outlets, valves stuck open, leaks, siphons and refills.
- **Fails safe.** Hard daily and per-dose limits, a hardware timer on every outlet, a master rail switch, low-water sensor, leak sensor, watchdogs, and power-cut accounting so a dose is never repeated after a restart.
- **Works offline.** A battery-backed clock, SD-card history, its own Wi-Fi hotspot when the home network is down, and notifications queued until the internet returns.
- **Dashboard** at `http://plantcare.local`:
  - Plant overview, moisture charts from 24 h to a year, and watering log.
  - Care recommendations, alerts, and room and reservoir charts.
  - Calibration helpers, settings, CSV export and firmware updates.
- **Phone notifications** through [ntfy](https://ntfy.sh).

## Status

The software is complete and tested on the PC: 113 firmware tests, including month-long simulations of plants, gravity valves, a dislodged sensor and a power cut mid-dose, plus 28 dashboard tests and a firmware build with the dashboard embedded. **It has not run on real hardware yet.** Follow the bring-up checklist in [docs/getting-started.md](docs/getting-started.md#8-bring-up-checklist) when the parts arrive, and watch the first waterings.

The reservoir is a **2 L Coca-Cola bottle** standing upside down on the scale, with the **master valve fitted to its cap** and one valve per plant below it ([assembly](docs/hardware/assembly.md#reservoir-the-coca-cola-bottle)). The firmware also supports peristaltic pumps, set per plant. See [the shopping list](docs/hardware/bom.md).

## Quick start

```sh
make test     # firmware unit tests + simulations on this PC, dashboard lint/types/tests
make mock     # try the dashboard against a simulated device: http://localhost:5173, password "plantcare"
make all      # build the dashboard, embed it, build the firmware
make flash    # flash over USB (see docs/getting-started.md for WSL)
make ota DEVICE=plantcare.local PASSWORD=…   # update over Wi-Fi
```

Needs PlatformIO Core and Node 24; see [docs/getting-started.md](docs/getting-started.md).

## Repository

```
firmware/
  lib/plantcore/   watering logic as plain C++17: engine, planner, safety, dosing, faults, alerts, JSON/CSV
  test/            Unity tests and multi-day simulations (pio test -e native)
  src/             ESP32: hal/ drivers, storage/ NVS + SD, net/ Wi-Fi + REST + ntfy, app/ tasks
  include/         pin map and compile-time constants
dashboard/         React + TypeScript + Vite; built and compiled into the firmware
shared/api-fixtures/  example API responses checked by both firmware and dashboard tests
docs/              architecture, hardware, control logic, API, alerts, getting started
```

## Documentation

- [Getting started](docs/getting-started.md): tools, building, flashing, first boot, calibration, checklist
- [Shopping list](docs/hardware/bom.md) · [Wiring](docs/hardware/wiring.md) · [Assembly](docs/hardware/assembly.md)
- [How watering decisions are made](docs/control-logic.md)
- [Architecture](docs/architecture.md)
- [REST API](docs/api.md) · [Alerts](docs/alerts.md)

## Safety

Water and mains-powered electronics share a room. Use a fused 12 V supply, keep the electronics above the water, put a drip tray and the leak sensor under the plants, and test every interlock before leaving the system unattended.
