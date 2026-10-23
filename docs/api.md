# REST API

Served by the main station at `http://plantcare.local/api` (or its IP). The plant nodes have no HTTP API: they talk to the station by radio, and the station relays commands to them. Bodies are JSON. Example responses for every shape are in [`shared/api-fixtures/`](../shared/api-fixtures); the firmware and dashboard tests both check against them. Send request bodies with `Content-Type: application/json`.

## Authentication

There is one admin password, set in the first-run wizard. Exchange it for a token and send the token on every request:

```
POST /api/login            {"password": "…"}            → {"token": "32 hex chars"}
Authorization: Bearer <token>
```

Tokens survive reboots (their hashes are stored), up to 8 at a time; the oldest is dropped first. Changing the password signs out every other session. After 5 wrong passwords, logins are refused for 30 s (`429`, with `retryAfterS`). The event stream takes the token as `?token=` because browsers cannot set headers on EventSource; so does CSV export, so a plain link works.

Errors look like `{"error": "code", "message": "human readable"}` with a matching HTTP status: 400 invalid input, 401 not signed in, 404 not found, 409 refused or busy, 413 body over 16 KB, 503 the station core stayed busy (try again).

## Public endpoints

| Method | Path | Body → Response |
|---|---|---|
| GET | `/api/info` | → `{name, version, setupRequired, timeValid, apActive, dashboardEmbedded}` |
| POST | `/api/setup` | `{password, settings?, now?}` → `{token}`. Only while no password is set. `settings` is a partial settings object; `now` (unix seconds) sets the clock if it is not set yet. |
| POST | `/api/login` | `{password}` → `{token}` |

## Status and live updates

| Method | Path | |
|---|---|---|
| GET | `/api/status` | Everything at a glance: device, network, storage, automation, room climate, per-plant state (including its node, bottle, battery, signal and queued commands), nodes waiting to be added, radio summary, today's total, alert counts. See [`status.json`](../shared/api-fixtures/status.json). |
| GET | `/api/stream?token=` | Server-Sent Events. `status` every 5 s (same body as `/api/status`) and `event` for each logged event (same shape as `/api/events` items). |

Per plant:

- `state` is `idle`, `waiting`, `watering`, `soaking`, or `unknown` (the node has not reported since the station started).
- `sensor` is `ok`, `suspect`, `failed` or `unknown`, and `issue` says why (`uncalibrated`, `bus_error`, `disconnected`, `out_of_range`, `no_response`, `stuck`, `erratic`).
- `bottle` is `null` until the node has reported. Otherwise it has `{ml, pct, capacityMl, daysLeft, low, empty, emptyReason, flowFactor}`, with `emptyReason` = `none`, `gauge`, `pump_dry` or `no_response`.
- `pump` is `{learned, lastRun}`, with `lastRun` = `ok`, `dry`, `disconnected` or `jammed`.
- `battery` is `{mv, pct, low}` and `linkPct` is the share of the node's recent frames that got a reply.
- `nextCheckInS` is when the node is expected next; `queued` lists the commands waiting for it.
- `configPending` is true while new settings are on their way to the node.
- `repairRequested` means the node lost its key and asks to pair again.
- `buttonPressed` is true for a minute after someone pressed the node's button.

`newNodes` lists nodes that said hello and are waiting to be added: `{id, board, firmware, lastSeenAt}`.

## Plants

| Method | Path | Body → Response |
|---|---|---|
| GET | `/api/plants` | → `[PlantProfile]` |
| POST | `/api/plants` | `PlantProfile` (id optional: generated from the name) → `201 PlantProfile`. With `node` set to a waiting node's id, that node is added and pairs at its next check-in. |
| GET | `/api/plants/{id}` | → `PlantProfile` |
| PUT | `/api/plants/{id}` | partial `PlantProfile` → `PlantProfile`. The id cannot change. Setting another `node` unpairs the old one; `"node": null` unlinks it. |
| DELETE | `/api/plants/{id}` | → `{ok}`. Its node is told to unpair at its next check-in. |
| POST | `/api/plants/{id}/water` | `{ml}` (5–250) → `202 {queued, id, etaS}`. Manual watering: ignores the schedule, not the safety limits. |
| POST | `/api/plants/{id}/timed-run` | `{seconds}` (1–60) → `202 {queued, id, etaS}`. Runs the pump for a fixed time so you can measure its flow in a cup. It does not count as watering the plant. |
| POST | `/api/plants/{id}/refilled` | `{ml?}` → `202 …`. Resets the bottle gauge (full, or `ml` added). Same as the node's button. |
| POST | `/api/plants/{id}/clear-faults` | → `202 …`. Unlocks the pump after *Pump drew no current* / *jammed* and clears *No moisture response*. |
| POST | `/api/plants/{id}/identify` | → `202 …`. Blinks the node's LED for 10 s (boards with an LED). |
| POST | `/api/plants/{id}/calibrate` | `{point: "dry" or "wet"}` → `{raw, ageS, calibration}`. Stores the node's latest reading as that point; `ageS` says how old the reading was. |
| POST | `/api/nodes/{nodeId}/repair` | → `{ok}`. Hands a new key to a plant's node that asked to pair again. |

Plant commands are **queued**: the node carries them out when it next checks in, and `etaS` estimates when (seconds, or `null` if unknown). They answer 404 (`not_found`), 400 (`invalid_amount`), or 409 with `no_node` (the plant has no node), `busy` (eight commands already waiting) or `emergency_stop`. A refusal by the node itself (for example an empty bottle) appears as a `system` event with code `blocked`.

`PlantProfile`: see [`plant.json`](../shared/api-fixtures/plant.json). Validation (all enforced on the main station, and again on the node):

- `id` is 1–24 characters of a–z, 0–9, `-` or `_`, and `name` is required.
- `node` is 12 lowercase hex digits or `null`, and each node waters one plant.
- `pump.flowMlPerS` is 0.05–100; `bottle.capacityMl` is 100–20,000; `bottle.lowPct` is 0–90; `bottle.reserveMl` is below half the capacity.
- `moisture` must satisfy 0 ≤ critical < min < target < max ≤ 100.
- Each range in `lightLux`, `tempC` and `humidityPct` must have min < max.
- `style` is `evenly_moist`, `dry_between` or `soak_and_dry`.
- `onSensorFault` is `pause` or `fallback_schedule`.
- `dormantMonths` lists months 1–12.
- At most 16 plants.

## History and events

| Method | Path | Response |
|---|---|---|
| GET | `/api/history?plant=&from=&to=&points=` | `{plantId, from, to, points: [[ts, moisture%, bottleMl], …]}`. Averaged into at most `points` (10–300) buckets. Ranges over 14 days use daily averages. Default range: last 24 h. |
| GET | `/api/env/history?from=&to=&points=` | `{from, to, points: [[ts, tempC, humidity%, lux], …]}`, with `null` where a sensor had no value |
| GET | `/api/events?type=&plant=&from=&to=&limit=` | `{events: [{ts, type, plantId, code, ml, detail}]}`, newest first. `type`: `watering`, `alert`, `alert_cleared`, `refill`, `system`. Default: last 30 days, 100 events. |
| GET | `/api/export.csv?kind=&from=&to=&token=` | Raw CSV download, `kind` = `readings`, `env` or `events`. Needs the SD card. |

Watering events have `code` = `auto`, `manual`, `fallback`, `timed` (flow test), `detected` (watered by hand) or `interrupted` (counted after a restart). Refills have `code` = `refill`. System events include `node_found`, `node_repair`, `blocked`, `emergency_stop`, `resume` and `automation_on`/`automation_off`.

## Alerts

| Method | Path | |
|---|---|---|
| GET | `/api/alerts` | `{alerts: [Alert]}`: active first, then the 50 most recent resolved. See [alerts.md](alerts.md). |
| POST | `/api/alerts/{id}/ack` | Stops reminders for it. The alert stays until the cause is gone. |
| POST | `/api/alerts/ack-all` | |

## Settings and system

| Method | Path | Body |
|---|---|---|
| GET | `/api/settings` | → `Settings` ([`settings.json`](../shared/api-fixtures/settings.json)). The ntfy token is never returned, only `tokenSet`. |
| PUT | `/api/settings` | partial `Settings`; `notify.token` is write-only. |
| POST | `/api/system/stop` | `{reason?}`. Emergency stop: every node stops pumping at its next check-in and stays stopped until resume. |
| POST | `/api/system/resume` | |
| POST | `/api/system/automation` | `{enabled}` |
| POST | `/api/system/time` | `{epoch}`. Sets the clock (and RTC) from a browser. |
| POST | `/api/system/reboot` | |
| POST | `/api/system/ota` | multipart file field `firmware` (the main station's `firmware.bin`). The nodes keep watering meanwhile. The new image rolls back automatically if it does not run healthily for a minute. Nodes are flashed over USB. |
| GET | `/api/config/backup` | → `{version, exportedAt, settings, plants}` |
| POST | `/api/config/restore` | `{settings, plants}`. Everything is validated before anything is saved. |
| GET | `/api/wifi` | → `{configured, ssid, mode, ip, rssi, apActive, apSsid, internet}` |
| POST | `/api/wifi/scan` · GET `/api/wifi/scan` | Start a scan · `{scanning, networks: [{ssid, rssi, secure}]}` |
| POST | `/api/wifi` | `{ssid, password, apPassword?}` |
| POST | `/api/notify/test` · GET `/api/notify/test` | Send a test notification · `{result, at}` (`"ok"` or the error) |
| POST | `/api/account/password` | `{current, next}` → `{token}` |
| POST | `/api/logout` | |

## Examples

```sh
J='Content-Type: application/json'
TOKEN=$(curl -s -X POST -H "$J" -d '{"password":"…"}' http://plantcare.local/api/login | jq -r .token)
curl -s -H "Authorization: Bearer $TOKEN" http://plantcare.local/api/status | jq '.plants[] | {name, moisturePct, bottle: .bottle.ml, battery: .battery.pct}'
curl -s -X POST -H "$J" -H "Authorization: Bearer $TOKEN" -d '{"ml":80}' http://plantcare.local/api/plants/monstera/water
# → {"queued":true,"id":12,"etaS":140}
curl -s -H "Authorization: Bearer $TOKEN" -F firmware=@firmware/.pio/build/hub/firmware.bin http://plantcare.local/api/system/ota
```
