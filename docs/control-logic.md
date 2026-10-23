# How watering decisions are made

All of this runs on each plant's node, lives in `firmware/lib/plantcore/` and is covered by the native tests. Numbers are the defaults; most are settings or per-plant profile values.

## 1. Reading the soil

- The node reads its soil at **every check-in** (every 5 minutes, *Nodes check in every*), and **every minute during a watering session**. The sensor is powered from a GPIO for 0.3 s, and 9 readings of the board's ADC are taken; the median counts. Raw values are millivolts × 8 (the scale of the ADS1115 the first build used), so the sensor fault thresholds below are in those units.
- The raw value is mapped to **0–100 %** between the plant's two calibration points: **dry** (sensor in air) = 0 %, **wet** (sensor in water) = 100 %. Values are relative to the sensor, not a volumetric water content, so species values are starting points to tune.
- Readings are smoothed with an exponential moving average (α = 0.3). The trend over the last three readings (%/min) is tracked too.
- A sudden rise of more than 8 % that the system did not cause is logged as "watered outside the system", and the smoothing jumps to the new level.

## 2. The moisture band

Each plant has four thresholds: **very dry** (alert) < **start watering** < **water up to** (target) < **too wet**.

The start point moves with the conditions:

| Condition | Effect |
|---|---|
| Drying power: vapour-pressure deficit (VPD) from temperature and humidity | trigger + 2.5 × (VPD − 1.0 kPa), limited to −2…+5 % |
| Light above the plant's maximum | trigger + 1 % |
| Colder than the plant's minimum temperature | doses × 0.5, trigger never raised |
| Resting month (per plant, flipped for the southern hemisphere) | doses × resting factor; trigger − (1 − factor) × 10 % |

The trigger always stays at least 3 % below the target and never below "very dry". For **soak-and-dry** plants (succulents, cacti) the session target is "too wet" − 3 %: they are watered thoroughly, then left to dry out completely.

## 3. Starting a session

A session starts when the smoothed moisture is below the trigger **and**:
- the plant is enabled and set to water automatically, and automation is on;
- it is inside the **watering window** (default 07:00–21:00), unless the plant is below "very dry";
- the **rest between sessions** has passed (per plant, e.g. 8 h for ferns, 120 h for succulents);
- the plant's daily budget is not used up;
- it is not in a 30-minute back-off after a blocked, failed or unanswered session.

## 4. Pulse and soak

Overwatering mostly comes from sensor lag: water takes minutes to reach the sensor, so "water until the sensor says enough" overshoots. Instead:

1. **Pulse.** The first pulse is 5 % of the pot volume (20 ml up to the plant's largest pulse). Once the system has learned how many ml raise this plant's moisture by 1 %, pulses are *deficit × ml-per-% × 0.7*: deliberately 30 % short.
2. **Soak** for the plant's soak time (default 10 min). If moisture is still rising faster than 0.15 %/min, it keeps soaking for up to two more soak periods, so the result is judged on settled soil.
3. **Measure and learn.** A rise of at least 1.5 % updates ml-per-% (moving average, 70 % old / 30 % new).
4. **Decide.** Stop when within 0.5 % of the target. Otherwise pulse again, up to **6 pulses** or the daily budget.

If a session hits its limits while the soil is still below the start-watering point, you get *Target not reached*. If **two pulses produce less than 1 % rise**, the session stops as *No moisture response*, and the pump's current decides what that means:

- **The pump felt water moving** (its current was normal): the water went somewhere, but the sensor did not see it. The sensor is probably out of the soil, or the tube is not in the pot. Automatic watering pauses until the reading rises by 5 % on its own or you clear the fault.
- **Otherwise** (no current sensing, or not learned yet): the likeliest cause is an empty bottle. It is marked empty, so nothing is pumped until it is refilled.

The 30-day simulation in `test_scenarios` keeps four quite different plants inside their bands, with no overshoot beyond 3 % above "too wet" and never below "very dry".

## 5. Safety rules (every dose, automatic or manual)

A dose is refused if:
- an emergency stop is active (from the dashboard, delivered with every reply), the **bottle is empty**, the **batteries are flat** (below 3.3 V at rest), or the pump is locked after a fault;
- the plant is disabled;
- *(automatic only)* automation is paused, the plant is manual-only, its sensor has failed, or the rest between sessions has not passed (pulses within a session are exempt).

The amount is then capped at the largest pulse (plant limit for automatic, global hard limit of 250 ml for manual) and at what is left of the plant's daily limit (plant limit, or the 1,500 ml hard limit for manual). Doses below 5 ml are not worth starting the pump for. Until the pump's current has been learned, a dose is also capped at what the fuel gauge says is **above the reserve** (default 50 ml), so the pump never sucks air.

Each dose may run the pump for at most **1.5 × the expected time + 3 s**, and never more than the hard limit (120 s). A hardware timer on the node enforces the hard limit + 5 s even if the software hangs.

## 6. Dosing

There is no scale: the pump runs for *dose ÷ flow*. The flow is the profile's measured flow (*Measure flow* on the plant page) times the correction the fuel gauge has learned (§7).

While it runs, the node reads the pump current every 100 ms, divided by the battery voltage so that draining cells are not mistaken for a problem. The first 0.4 s (start-up surge) is ignored. After three healthy runs it knows the pump's normal **wet** current, and then:

| Seen | For | Means | Effect |
|---|---|---|---|
| under 65 % of the wet current | 1.5 s | the pump is sucking air | stop; the bottle is **empty**; only the water before the drop is counted |
| under 20 mA (needs no learning) | 1 s | nothing connected | stop; pump **locked** until you clear it |
| over 2× the wet current | 1 s | impeller blocked | stop; pump **locked** |

A run only teaches the wet current if it ended normally and the gauge thought the bottle held water. *Measure flow* runs (pump into a cup) count against the bottle but not as watering the plant. Turn off *Feel the pump run dry* in the plant's settings if the 1 Ω resistor is not fitted.

## 7. The bottle (fuel gauge)

There is no level sensor. The node keeps a gauge instead:

- **Counting:** every dose subtracts what was pumped. A new node assumes its bottle is full.
- **Refill:** a short press of the node's button, or *Refilled* in the dashboard, resets the gauge to full.
- **Low** below the plant's refill reminder (default 25 %), or when it lasts less than 2 days at the current rate and is under half full.
- **Empty**, and no more pumping until refilled, for one of three reasons:
  1. the **pump ran dry** (§6), the most reliable;
  2. the **gauge counted it empty** (only while the pump's current is not learned yet; afterwards the gauge only warns and the pump decides);
  3. **watering stopped making the soil wetter** (§4).
- **Self-calibration:** when the pump runs dry after a confirmed full refill, the gauge compares what it counted with what the bottle holds. The difference corrects the pump flow by half the error each cycle (within 0.5–2×), so the gauge and the doses get more accurate over a few refills without a measuring cup.

The plant page shows the level, days left and why the bottle was called empty, plus a chart of the level over time.

## 8. Sensor faults

| Fault | Detected when | Effect |
|---|---|---|
| Not calibrated | dry and wet points less than 1,500 counts (188 mV) apart | no automatic watering (info) |
| Bus error | 3 failed ADC reads in a row | sensor failed |
| Disconnected / shorted | 3 readings below 0.25 V or above 3.25 V | sensor failed |
| Out of range | 3 readings beyond the calibration points ± 25 % | sensor failed |
| No moisture response | watering produced no rise (see §4) | sensor failed until it rises or you clear it |
| Stuck | reading moved by less than 3 counts in 12 h | suspect: still used, warned |
| Erratic | 3 jumps of more than 25 % within an hour (not after watering) | suspect: still used, warned; clears after 6 quiet hours |

When a sensor **fails**, the plant's policy applies: **pause** (default) or **fixed schedule** (X ml every N days, inside the watering window). The fixed schedule runs only for a confirmed hardware fault (bus error, disconnected, out of range), never right after boot before the first reading, and never after "no moisture response", because then the water might not be reaching the pot.

## 9. Power cuts, reboots and outages

- Before the pump starts, the node saves "dose in progress (ml)" to flash. After an unexpected restart (batteries pulled, brown-out) that dose is **counted in full**, taken off the bottle gauge, and the rest period starts from the restart, so a crash can never cause a double watering. You get an *Interrupted watering* warning.
- Daily totals reset at local midnight (the node gets the time and its UTC offset in every reply). Without a valid clock they reset every 24 h of uptime instead, so a missing clock can never lock watering forever.
- With no clock at all the watering window is ignored (better to water at night than not at all).
- **The main station being off changes little.** Each node keeps watering with its last settings. It has no new room climate (after 30 minutes the climate adjustments switch off) and, after a reboot, no clock. Dashboard commands wait in the station until the node next checks in. A node that misses three check-ins scans the radio channels to find the station again.
- **A node going quiet** (flat batteries, out of range) raises *Node offline* on the station after three missed check-ins plus a minute.
- **Batteries:** *Batteries low* below 3.6 V at rest (measured between pump runs, since the cells sag while it runs), and the pump is refused below 3.3 V so a weak cell cannot brown out the board mid-dose.

## 10. Environment alerts

Temperature or humidity (from the main station's sensors) outside a plant's range for more than 2 hours raises a note (info, not pushed to the phone) for that plant. Moisture below "very dry" raises *Plant too dry*. Above "too wet" for 2 days raises *Plant too wet* (check drainage).

## 11. Recommendations (dashboard)

`dashboard/src/lib/recommendations.ts` compares the profile with the last 7 days: daily light peak (median), average humidity, temperature extremes, waterings per day, weekly water use against the pot size, current wetness, resting season and the bottle's forecast. It runs in the browser to keep the firmware small.
