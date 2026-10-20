# Architecture

PlantCare is one ESP32 that does everything: it reads the sensors, decides when and how much to water, drives the pumps or valves, keeps the history on a microSD card, and serves its own web dashboard. There is no server and no cloud. The only optional internet use is NTP (time) and ntfy (phone notifications).

```
 ┌──────────────────────── ESP32 DevKit (WROOM-32) ─────────────────────────┐
 │ core 1  safety   50 ms  prio 6  interlocks · control watchdog · button · LED│
 │ core 1  control 100 ms  prio 5  sensors → plantcore::Engine → outlets      │
 │ core 0  storage         prio 2  SD card writes                              │
 │ core 0  net      20 ms  prio 1  Wi-Fi · NTP · ntfy · live status · OTA ok   │
 │ core 0  web server      prio 3  REST /api · SSE /api/stream · dashboard     │
 └───┬────────────┬──────────┬──────────┬──────────────┬──────────────────────┘
   I2C bus       SPI       HX711     GPIO in        GPIO out
   ADS1115 ×2    microSD   load cell  float switch   outlets 1–8 (MOSFETs)
   SHT4x                              leak sensor    actuator rail (master)
   BH1750                             BOOT button    LED · buzzer
   DS3231
```

## Repository

| Path | What it is |
|---|---|
| `firmware/lib/plantcore/` | All decision logic as plain C++17 with no Arduino headers: the engine, planner, safety rules, dosing, reservoir model, sensor fault detection, alerts, JSON and CSV formats. Compiles on the PC for tests. |
| `firmware/test/` | 113 Unity tests run on the PC (`pio test -e native`), including multi-day simulations. |
| `firmware/src/` | ESP32-specific code: hardware drivers (`hal/`), NVS and SD storage (`storage/`), Wi-Fi, web API, notifications (`net/`), and the controller that runs the tasks (`app/`). |
| `dashboard/` | React + TypeScript web app. Built, gzipped and compiled into the firmware as `firmware/src/generated/web_assets.h`. |
| `shared/api-fixtures/` | Example JSON for every API response. Both the firmware tests and the dashboard tests check against them, so the two cannot drift apart. |
| `docs/` | This documentation. |

## Key decisions

**All watering logic is host-testable.** `plantcore::Engine` owns no hardware: it receives a snapshot of inputs each tick (`Inputs`), switches outlets through an `IOutlets` interface, and reports events and state through `IEngineSink`. The same code runs in the multi-day simulations in `firmware/test/test_scenarios` and on the device. The simulations already caught real bugs: a plant was watered twice in a row by the fallback schedule, and a soak was judged before the water had reached the sensor.

**Closed-loop dosing.** A load cell under the reservoir measures the water that actually leaves (1 g ≈ 1 ml), so a dose stops at the right amount whether it comes from a pump or a gravity valve with a falling water level. The same scale detects outlets that deliver nothing, valves stuck open, siphons, leaks and refills. Without the scale the system falls back to timed doses from a calibrated flow rate.

**One lock around the engine.** The control task ticks the engine every 100 ms; web handlers and the network task call into it through `Controller::withEngine`, which takes the same mutex. Nothing touches the engine concurrently, and only engine code switches the outlets. The safety task is the one exception: it may *close* outlets directly on a leak, an empty reservoir or a stalled control task, which can only ever make things safer.

**Defence in depth for "water must stop".**
1. The engine's dose controller closes the outlet at the target volume.
2. Each dose has a maximum on-time (1.5 × expected + 3 s, never more than the hard limit, default 120 s).
3. An `esp_timer` armed on every outlet opening forces everything off at the hard limit + 5 s, independent of the control loop.
4. The safety task closes all outlets if the control task stops reporting for 2 s, and restarts the controller after 15 s.
5. Water flows only when a channel *and* the actuator rail are both switched on: a high-side switch on the pump supply, or a master valve in series for gravity feed.
6. The task watchdog panics and restarts the chip if the control or safety task hangs for 8 s. All actuator pins are driven low first thing at boot, and gate pull-downs hold them off before that.

**Offline first.** Watering needs no network. The DS3231 keeps the time through power and internet outages, and NTP corrects it when online. If the home Wi-Fi is unreachable for 10 minutes the device also opens its own hotspot, so a phone can still reach the dashboard. Notifications queue with exponential backoff until the internet returns.

**The dashboard lives inside the firmware.** One firmware image contains both, so an over-the-air update can never leave them mismatched, and nothing else needs hosting. The dashboard has a 200 KB gzipped budget enforced at build time (it is about 174 KB). The firmware with the dashboard uses about 1.45 MB of each 1.875 MB app slot.

## Storage

| Where | What | When written |
|---|---|---|
| NVS (flash) | Settings, plant profiles, Wi-Fi, scale calibration, password hash, API token hashes | When changed |
| NVS (flash) | Safety state: ml watered today per plant, last watering times, learned ml-per-%, lockouts, emergency stop, "dispense in progress" | Before every dose and at session ends |
| microSD | `readings/YYYY-MM-DD.csv`, `env/YYYY-MM-DD.csv`, `events/YYYY-MM.csv`, daily summaries in `daily/`, nightly `backup/config-*.json` | Every log interval (default 10 min) and on every event |
| RAM | Last 24 h of readings, environment and the last 100 events | Always, so the dashboard keeps working without an SD card |

Queries longer than 14 days read the daily summary files instead of the raw readings, so a one-year chart stays fast. When the card runs low on space, the oldest raw days are deleted first; daily summaries are kept.

## Flash layout (`firmware/partitions.csv`)

| Partition | Size | Use |
|---|---|---|
| nvs | 64 KB | configuration and safety state |
| otadata | 8 KB | which app slot boots |
| app0 / app1 | 1.875 MB each | firmware + dashboard, A/B for OTA with automatic rollback |
| coredump | 64 KB | crash dumps for debugging |

A new image starts in "pending verify" mode. It marks itself valid only after running for a minute with the control task healthy; if it crashes before that, the bootloader returns to the previous image.

## Live updates

The dashboard opens a Server-Sent Events stream (`/api/stream`). The device pushes a full status every 5 seconds and each event (watering, alerts, refills) as it happens. If the stream goes quiet the dashboard falls back to polling `/api/status`.
