# Architecture

PlantCare is a small fleet. Each plant has a **plant node**: an ESP32-C3/S3 on three AA batteries, with its own bottle, pump and soil sensor. The node decides when and how much to water its plant. One **main station** (an ESP32 DevKit on USB power) serves the dashboard, keeps the history and alerts, holds every plant's settings, and talks to the nodes by radio (ESP-NOW). There is no server and no cloud; the only optional internet use is NTP and ntfy.

```
                       home Wi-Fi router
                              │ Wi-Fi (dashboard, NTP, ntfy)
                 ┌────────────┴─────────────┐
                 │       MAIN STATION       │ ESP32 DevKit, USB powered
                 │ dashboard · REST · SSE   │ optional: SHT4x, BH1750, DS3231, SD
                 │ profiles · alerts · logs │
                 └──┬──────────┬─────────┬──┘
           ESP-NOW  │          │         │   the node speaks first; the station answers
              ┌─────┴──┐  ┌────┴───┐  ┌──┴─────┐
              │  node  │  │  node  │  │  node  │ ESP32-C3/S3 · 3×AA
              │ engine │  │ engine │  │ engine │ runs the watering logic for its plant
              └─┬────┬─┘  └────────┘  └────────┘
            pump│    │soil sensor
           ┌────┴┐  ┌┴─────┐
           │bottle│ │ pot  │
           └──────┘ └──────┘
```

## Who does what

| | Plant node | Main station |
|---|---|---|
| Decides when and how much to water | ✓ (plantcore `Engine`, one plant) | |
| Switches the pump, feels it run dry | ✓ | |
| Bottle fuel gauge | ✓ | shows it |
| Profiles, settings, alerts, history, notifications | keeps its own copy of its profile | ✓ (the source of truth) |
| Clock and room climate | gets them in every reply | ✓ (NTP / RTC, SHT4x / BH1750) |
| Keeps working when the other is off | ✓ waters with its last settings | ✓ shows the last known state |

## The radio conversation

Battery nodes sleep, so the station never calls them. Instead **the node speaks first and the station answers every frame at once**:

```
 node                                          main station
  │  Report (moisture, bottle, battery, …) ──►  │  stores it, catches up on alerts
  │  ◄── Reply: time, room climate, stop flag,  │
  │          + one mailbox item                 │  (a queued command, or a piece of
  │  Event (a watering, an alert) ──►           │   a new config)
  │  ◄── Reply                                  │
  │  Poll ("anything more?") ──►                │
  │  ◄── Reply (nothing more)                   │
  │  … sleeps until the next check-in           │
```

- A node checks in every **5 minutes** (*Settings → Nodes check in every*), and every minute while a watering session is under way. "Water now" from the dashboard is queued and done at the next check-in; the dashboard shows when that will be.
- Each frame is at most 250 bytes. After pairing, every frame ends with an **8-byte HMAC-SHA256** tag over the node's MAC, the header and the payload, keyed per node. Forged and replayed frames are ignored.
- **Sequence numbers** make retries safe: a lost reply is re-sent unchanged when the node repeats its frame, so nothing is done twice. The node reserves numbers in blocks in flash, so even a reboot never reuses one.
- **Config** (the plant's profile and the relevant settings) is versioned by a hash of its content and sent in ~180-byte pieces. An interrupted transfer resumes at the next check-in.
- **Commands** carry increasing ids, and the node reports the last one it carried out, so each runs exactly once. After a station restart, waiting commands are renumbered after the node's last one.
- **Pairing:** a new node says *Hello* (broadcast on every channel until the station answers), then waits. When you add it in the dashboard, its next *Hello* gets the key. The key is handed over only once: a node that later asks again (for example, after its flash was erased) shows as *asks to pair again*, and you confirm it in the dashboard.
- **Channel:** the station listens on the home router's channel. A node that misses three check-ins scans channels 1–13 to find it again.

The whole protocol is transport-free C++ (`firmware/lib/plantlink/`), so the node and station logic runs in host tests over a simulated, lossy radio.

## Repository

| Path | What it is |
|---|---|
| `firmware/lib/plantcore/` | The node's watering logic as plain C++17: engine, planner, safety rules, dosing, bottle fuel gauge, pump monitor, sensor faults, alerts, JSON and CSV formats. |
| `firmware/lib/plantlink/` | The radio: frame codec with HMAC (`Frames`), the node's side (`NodeSession`), the station's view of every node (`Fleet`), and the station core (`Station`): plants ↔ nodes, alerts, commands, `/api/status`. |
| `firmware/test/` | 174 Unity tests on the PC (`pio test -e native`): unit tests, multi-day plant simulations, and node ↔ station conversations with lost frames. |
| `firmware/src/node/` | Node firmware: hardware (`NodeHw`), flash (`NodeStore`), ESP-NOW (`NodeRadio`), and the loop with light sleep (`Node`). Pins per board in `include/boards.h`. |
| `firmware/src/hub/` | Main station firmware: Wi-Fi, web API, ntfy, SD/NVS storage, climate sensors, the ESP-NOW link (`net/NodeLink`) and the glue around the station core (`app/Hub`). |
| `firmware/tools/spike/` | Bench sketch to measure sleep current, pump current wet vs dry, the soil ADC and radio range. |
| `dashboard/` | React + TypeScript web app, compiled into the main station's firmware. |
| `shared/api-fixtures/` | Example JSON for every API response, checked by both the firmware and the dashboard tests. |

## Key decisions

**One node per plant, with its own bottle.** No shared reservoir means no scale, no valves, no 12 V supply and no plumbing between plants. Plants water at the same time without waiting for each other. A fault can only ever empty one small bottle into its own pot.

**The node decides.** The tested `plantcore` engine runs on every node, so watering continues with the last settings if the main station is off, rebooting or out of range. Without the station the node has no clock or room climate; it falls back to 24-hour uptime days and no climate adjustment.

**No level sensor: three ways to know the bottle is empty.**
1. A **fuel gauge** counts what the pump moves since the last refill (button press or *Refilled*). Until the pump current is learned, doses never dip into the reserve.
2. The **pump feels it**: a submersible pump draws much less current when it sucks air. The node learns the normal "wet" current per battery volt (so draining batteries are not mistaken for a dry pump) and stops the pump when the current drops. The same reading catches an unplugged or a jammed pump.
3. **The soil tells:** if two pulses do not make the soil wetter and the pump did not feel water moving, the bottle is called empty.

Each time the pump runs dry from a confirmed full bottle, the gauge corrects the pump's flow, so it gets more accurate with every refill.

**Defence in depth for "the pump must stop".**
1. The dose controller stops after *ml ÷ flow* seconds.
2. Each dose has a maximum run time (1.5 × expected + 3 s, never more than the hard limit, default 120 s).
3. An `esp_timer` on the node switches the pump off at the hard limit + 5 s, whatever the main loop does.
4. The pump monitor stops a dry, unplugged or jammed pump within one or two seconds.
5. The gate pull-down keeps the MOSFET off while the board boots, and the pump pin is driven low first thing.
6. The bottle holds a finite amount of water.

**Persist before pumping.** Before the pump starts, the node writes "dose in progress" to flash. After a power cut mid-dose the full dose is counted, so a reboot never waters twice.

**Light sleep, not deep sleep.** Light sleep keeps RAM, so the engine just continues where it was, with no state to save and restore every few minutes. It costs a little more current than deep sleep. The bench spike measures whether that matters for your boards.

**The dashboard lives inside the station firmware.** One image contains both, so an update can never leave them mismatched. The dashboard has a 200 KB gzipped budget enforced at build time (it is about 176 KB).

## Main station tasks

| Task | Core | Priority | Does |
|---|---|---|---|
| radio | 1 | 5 | Answers node frames as they arrive (a node waits about 60 ms) |
| web server | 0 | 3 | REST `/api`, SSE `/api/stream`, the dashboard |
| storage | 0 | 2 | SD card writes |
| net | 0 | 1 | Wi-Fi, NTP, ntfy, offline detection, live status every 5 s, button and LED, OTA health |

One mutex guards the station core; the radio task waits at most 40 ms for it, so a slow web request can delay a reply but never corrupt state. ESP-NOW runs on the station interface next to the Wi-Fi connection. Wi-Fi power saving is off, so the radio never dozes through a node's frame, and the setup hotspot runs as AP+STA so ESP-NOW keeps working.

## Storage

| Where | What | When written |
|---|---|---|
| Station NVS | Settings, plant profiles, paired nodes and their keys, the stop flag, Wi-Fi, password hash, token hashes | When changed |
| Station microSD | `readings/YYYY-MM-DD.csv` (moisture, bottle level, battery per plant), `env/YYYY-MM-DD.csv`, `events/YYYY-MM.csv`, `daily/` summaries, nightly `backup/` | Every log interval (10 min) and on every event |
| Station RAM | Last 24 h of readings and climate, last 100 events | Always, so the dashboard works without a card |
| Node NVS | Pairing (station, channel, key, sequence reserve), its config, engine state (today's ml, learned ml-per-%, lockout, bottle gauge, learned pump current, stop flag, dose in progress) | At watering boundaries, refills and config changes |

## Flash layout (main station, `firmware/partitions.csv`)

| Partition | Size | Use |
|---|---|---|
| nvs | 64 KB | configuration |
| otadata | 8 KB | which app slot boots |
| app0 / app1 | 1.875 MB each | firmware + dashboard (about 1.46 MB), A/B for OTA with rollback |
| coredump | 64 KB | crash dumps |

Nodes use the board's default partition table and are flashed over USB.
