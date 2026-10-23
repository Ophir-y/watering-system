# Alerts

Each alert is raised once per cause (and per plant), updates while the cause lasts, and clears by itself when the cause is gone. Plant alerts are raised by the plant's node and forwarded to the main station at its next check-in; the station raises the system ones (clock, SD card, offline nodes). If a forwarded message is lost on the way, the station still learns about the alert from the node's next report (with a generic text). **Acknowledging** stops phone reminders, not the alert.

Phone notifications (ntfy) are sent for alerts at or above *Notify me about* (default: warnings). Unacknowledged **critical** alerts are re-sent every 6 h and **warnings** every 24 h; notes are never pushed. The main station's LED blinks 4×/s for a critical alert and once a second for a warning.

| Code | Severity | Raised when | What to do |
|---|---|---|---|
| `bottle_empty` | critical | The pump ran dry, the gauge counted the bottle empty (before the pump current is learned), or two pulses did not make the soil wetter. Nothing is pumped for this plant. | Refill the bottle, then press the node's button (or *Refilled* on the plant page). |
| `battery_low` | warning → critical | The node's batteries are below 3.6 V at rest (warning). Below 3.3 V the pump is refused (critical). | Replace the AA cells. |
| `node_offline` | warning | The plant's node missed three check-ins. Its plant is still watered with the last settings if the node is alive; it just cannot report. | Check the batteries, then the range: move the main station or the node. |
| `pump_disconnected` | warning | The pump drew almost no current. The pump is locked. | Check the pump wires, the MOSFET and the 1 Ω resistor. *Clear faults* on the plant page. |
| `pump_jammed` | warning | The pump drew more than twice its normal current. The pump is locked. | Clean the pump's intake and impeller. *Clear faults*. |
| `emergency_stop` | note | You pressed *Stop all*. Every node stops at its next check-in. | *Resume watering* when ready. |
| `bottle_low` | warning | Below the plant's refill reminder (25 %), or less than 2 days left while under half full. | Refill soon. |
| `sensor_fault` | warning / note | A moisture sensor is disconnected, shorted, out of range or not responding (warning), or stuck / erratic (note). | Check the cable and connector. The plant's *If the sensor fails* policy applies. |
| `sensor_no_response` | warning | Two pulses raised moisture by less than 1 % although the pump moved water. Automatic watering for the plant pauses. | Make sure the sensor is in the soil and the tube ends in the pot, then *Clear faults*. |
| `calibration_needed` | note | The sensor's dry and wet points are too close (new plant). | Calibrate on the plant page. |
| `plant_too_dry` | warning | Moisture below the plant's "very dry" level. | Usually resolves on the next watering; if not, look for an earlier alert explaining why. |
| `plant_too_wet` | warning | Above "too wet" for 2 days. | Check drainage and the saucer; lower the band. |
| `target_not_reached` | warning | A session hit its limits while the soil was still below the start point. | Sensor position, drainage, or limits too low. |
| `daily_limit_reached` | note | A plant used its daily budget. Resets at midnight. | Raise *Most per day* if this is normal for it. |
| `temp_out_of_range` / `humidity_out_of_range` | note | Outside the plant's preferred range for more than 2 hours (main station's climate sensor). | See the plant's recommendations. |
| `interrupted_dispense` | warning | The node restarted during a dose. The full dose was counted. | If it repeats, the batteries are weak or the pump draws too much. |
| `unexpected_reset` | warning | The node or the main station restarted after a crash, watchdog or brown-out. | Node brown-outs mean weak batteries; station brown-outs mean a weak USB charger. |
| `time_not_set` | warning | The main station has no valid clock (no internet, no RTC time). | *Settings → Maintenance → Set clock from this browser*; fit an RTC with a battery. |
| `storage_fault` | note | No SD card in the main station. Only the last day of history is kept, in memory. | Fit or re-seat a FAT32 card if you want long-term history. |
| `env_sensor_fault` | note | No temperature/humidity sensor on the main station. Climate adjustments are off. | Optional: fit an SHT4x. |
