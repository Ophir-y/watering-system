# REST API

Served by the device at `http://plantcare.local/api` (or its IP). Bodies are JSON. Example responses for every shape are in [`shared/api-fixtures/`](../shared/api-fixtures); the firmware and dashboard tests both check against them. Send request bodies with `Content-Type: application/json`.

## Authentication

There is one admin password, set in the first-run wizard. Exchange it for a token and send the token on every request:

```
POST /api/login            {"password": "…"}            → {"token": "32 hex chars"}
Authorization: Bearer <token>
```

Tokens survive reboots (their hashes are stored), up to 8 at a time; the oldest is dropped first. Changing the password signs out every other session. After 5 wrong passwords, logins are refused for 30 s (`429`, with `retryAfterS`). The event stream takes the token as `?token=` because browsers cannot set headers on EventSource; so does CSV export, so a plain link works.

Errors look like `{"error": "code", "message": "human readable"}` with a matching HTTP status: 400 invalid input, 401 not signed in, 404 not found, 409 refused by a safety rule or busy, 413 body over 16 KB.

## Public endpoints

| Method | Path | Body → Response |
|---|---|---|
| GET | `/api/info` | → `{name, version, setupRequired, timeValid, apActive, dashboardEmbedded}` |
| POST | `/api/setup` | `{password, settings?, now?}` → `{token}`. Only while no password is set. `settings` is a partial settings object; `now` (unix seconds) sets the clock if it is not set yet. |
| POST | `/api/login` | `{password}` → `{token}` |

## Status and live updates

| Method | Path | |
|---|---|---|
| GET | `/api/status` | Everything at a glance: device, network, storage, automation, leak, reservoir, environment, the running dose, the last completed dose, today's total, per-plant state, alert counts. See [`status.json`](../shared/api-fixtures/status.json). |
| GET | `/api/stream?token=` | Server-Sent Events. `status` every 5 s (same body as `/api/status`) and `event` for each logged event (same shape as `/api/events` items). |

Per-plant `state` is `idle`, `waiting` (queued for the outlet), `watering` or `soaking`. `sensor` is `ok`, `suspect` or `failed`, and `issue` says why (`uncalibrated`, `bus_error`, `disconnected`, `out_of_range`, `no_response`, `stuck`, `erratic`).

## Plants

| Method | Path | Body → Response |
|---|---|---|
| GET | `/api/plants` | → `[PlantProfile]` |
| POST | `/api/plants` | `PlantProfile` (id optional: generated from the name) → `201 PlantProfile` |
| GET | `/api/plants/{id}` | → `PlantProfile` |
| PUT | `/api/plants/{id}` | partial `PlantProfile` → `PlantProfile`. The id cannot change. |
| DELETE | `/api/plants/{id}` | → `{ok}` |
| POST | `/api/plants/{id}/water` | `{ml}` → `{ok}`. Manual watering: ignores the schedule, not the safety limits. 409 with the reason otherwise (`busy`, `leak`, `emergency_stop`, `reservoir_empty`, `lockout`, `plant_daily_limit`, …). |
| POST | `/api/plants/{id}/timed-run` | `{seconds}` (1–60) → `{ok}`. Opens the outlet for a fixed time, ignoring the scale, so you can measure its flow in a cup. |
| POST | `/api/plants/{id}/calibrate` | `{point: "dry" or "wet"}` → `{raw, calibration}`. Stores the current reading as that point. |
| POST | `/api/plants/{id}/clear-faults` | → `{ok}`. Unlocks the outlet after *No flow* / *Stuck open* and clears *No moisture response*. |

`PlantProfile`: see [`plant.json`](../shared/api-fixtures/plant.json). Validation (all enforced on the device):

- `id` is 1–24 characters of a–z, 0–9, `-` or `_`, and `name` is required.
- `channel` is 0–7, and each channel holds one plant.
- `moisture` must satisfy 0 ≤ critical < min < target < max ≤ 100.
- Each range in `lightLux`, `tempC` and `humidityPct` must have min < max.
- `style` is `evenly_moist`, `dry_between` or `soak_and_dry`.
- `onSensorFault` is `pause` or `fallback_schedule`.
- `dormantMonths` lists months 1–12.
- `outlet.type` is `pump` or `valve`.

## History and events

| Method | Path | Response |
|---|---|---|
| GET | `/api/history?plant=&from=&to=&points=` | `{plantId, from, to, points: [[ts, moisture%], …]}`. Averaged into at most `points` (10–300) buckets. Ranges over 14 days use daily averages. Default range: last 24 h. |
| GET | `/api/env/history?from=&to=&points=` | `{from, to, points: [[ts, tempC, humidity%, lux, reservoirMl], …]}`, with `null` where a sensor had no value |
| GET | `/api/events?type=&plant=&from=&to=&limit=` | `{events: [{ts, type, plantId, code, ml, detail}]}`, newest first. `type`: `watering`, `alert`, `alert_cleared`, `refill`, `system`. Default: last 30 days, 100 events. |
| GET | `/api/export.csv?kind=&from=&to=&token=` | Raw CSV download, `kind` = `readings`, `env` or `events`. Needs the SD card. |

Watering events have `code` = `auto`, `manual`, `fallback`, `timed`, `detected` (watered by hand) or `interrupted` (counted after a restart).

## Alerts

| Method | Path | |
|---|---|---|
| GET | `/api/alerts` | `{alerts: [Alert]}`: active first, then the 50 most recent resolved. See [alerts.md](alerts.md). |
| POST | `/api/alerts/{id}/ack` | Stops reminders and the buzzer for it. The alert stays until the cause is gone. |
| POST | `/api/alerts/ack-all` | |

## Settings and system

| Method | Path | Body |
|---|---|---|
| GET | `/api/settings` | → `Settings` ([`settings.json`](../shared/api-fixtures/settings.json)). The ntfy token is never returned, only `tokenSet`. |
| PUT | `/api/settings` | partial `Settings`; `notify.token` is write-only. |
| POST | `/api/system/stop` | `{reason?}`. Emergency stop: closes everything, latches until resume. |
| POST | `/api/system/resume` | Refused (`leak_still_detected`) while the leak sensor is wet. |
| POST | `/api/system/automation` | `{enabled}` |
| POST | `/api/system/time` | `{epoch}`. Sets the clock (and RTC) from a browser. |
| POST | `/api/system/reboot` | |
| POST | `/api/system/ota` | multipart file field `firmware` (a `firmware.bin`). Outlets close during the upload. The new image rolls back automatically if it does not run healthily for a minute. |
| GET | `/api/config/backup` | → `{version, exportedAt, settings, plants}` |
| POST | `/api/config/restore` | `{settings, plants}`. Everything is validated before anything is saved. |
| POST | `/api/reservoir/tare` | Stores the current scale reading as "empty container". |
| POST | `/api/reservoir/calibrate` | `{ml}`: the amount of water now in the container (100–50,000). |
| POST | `/api/reservoir/refilled` | `{ml}`. Logs a manual refill. |
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
curl -s -H "Authorization: Bearer $TOKEN" http://plantcare.local/api/status | jq '.plants[] | {name, moisturePct, state}'
curl -s -X POST -H "$J" -H "Authorization: Bearer $TOKEN" -d '{"ml":80}' http://plantcare.local/api/plants/monstera/water
curl -s -H "Authorization: Bearer $TOKEN" -F firmware=@firmware/.pio/build/esp32dev/firmware.bin http://plantcare.local/api/system/ota
```
