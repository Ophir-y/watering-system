# Alerts

Each alert is raised once per cause (and per plant), updates while the cause lasts, and clears by itself when the cause is gone. **Acknowledging** stops phone reminders and the buzzer, not the alert.

Phone notifications (ntfy) are sent for alerts at or above *Notify me about* (default: warnings). Unacknowledged **critical** alerts are re-sent every 6 h and **warnings** every 24 h; notes are never pushed. The buzzer beeps every 10 s for unacknowledged critical alerts, except in quiet hours. The status LED blinks 4×/s for a critical alert and once a second for a warning.

| Code | Severity | Raised when | What to do |
|---|---|---|---|
| `leak_detected` | critical | The leak sensor is wet. Every outlet closes and an emergency stop latches. | Find the leak, dry the sensor, then *Resume watering*. |
| `reservoir_unexplained_loss` | critical | ≥ 60 ml left the reservoir while nothing was being watered. Emergency stop. | Look for a leaking tube or a valve stuck open. Resume when fixed. |
| `outlet_stuck_open` | critical | Water kept flowing for 20 s after an outlet closed. Emergency stop and outlet lockout. | Check the valve or MOSFET, or a siphoning tube. *Clear faults* on the plant page, then resume. |
| `reservoir_empty` | critical | Float switch open, reservoir lifted off the scale, or below the empty level. All watering pauses. | Refill. It clears automatically. |
| `emergency_stop` | note | You pressed *Stop all* or the button, or a critical fault above stopped watering. | *Resume watering* when ready. |
| `reservoir_low` | warning | Below the low level (20 %) or less than 2 days left at the current rate. | Refill soon. |
| `float_scale_disagree` | warning | The float switch says empty but the scale shows over 30 %. Treated as empty. | One of the two is faulty or misplaced. |
| `scale_fault` | warning | No valid scale reading for 30 s, or the scale is not calibrated. Doses fall back to timed mode. | Check the HX711 wiring; calibrate under *Room & reservoir*. |
| `outlet_no_flow` | warning | A dose delivered almost nothing. The outlet is locked. | Check tubing, pump, valve and that the reservoir outlet is primed. *Clear faults* to retry. |
| `sensor_fault` | warning / note | A moisture sensor is disconnected, shorted, out of range or not responding on the bus (warning), or stuck / erratic (note). | Check the cable and connector. The plant's *If the sensor fails* policy applies. |
| `sensor_no_response` | warning | Two pulses raised moisture by less than 1 %. Automatic watering for the plant pauses. | Make sure the sensor is in the soil and the water reaches the pot, then *Clear faults*. |
| `calibration_needed` | note | The sensor's dry and wet points are too close (new plant). | Calibrate on the plant page. |
| `plant_too_dry` | warning | Moisture below the plant's "very dry" level. | Usually resolves on the next watering; if not, look for an earlier alert explaining why. |
| `plant_too_wet` | warning | Above "too wet" for 2 days. | Check drainage and the saucer; lower the band. |
| `target_not_reached` | warning | A session hit its limits while the soil was still below the start point. | Sensor position, drainage, or limits too low. |
| `daily_limit_reached` | note | A plant used its daily budget. Resets at midnight. | Raise *Most per day* if this is normal for it. |
| `temp_out_of_range` / `humidity_out_of_range` | note | Outside the plant's preferred range for more than 2 hours. | See the plant's recommendations. |
| `interrupted_dispense` | warning | The device restarted during a dose. The full dose was counted. | Check the power supply if it repeats. |
| `unexpected_reset` | warning | Restarted after a crash, watchdog or brown-out. | Brown-outs mean the supply sags when pumps start. |
| `time_not_set` | warning | No valid clock (no RTC time, no internet). | *Settings → Maintenance → Set clock from this browser*; check the RTC battery. |
| `storage_fault` | warning | SD card missing or failing. Only the last day of history is kept, in memory. | Re-seat or replace the card (FAT32). It is re-mounted automatically. |
| `env_sensor_fault` | note | No temperature/humidity sensor found. Environmental adjustments are off. | Check the SHT4x wiring. |
