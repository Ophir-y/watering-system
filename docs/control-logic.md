# How watering decisions are made

All of this lives in `firmware/lib/plantcore/` and is covered by the native tests. Numbers are the defaults; most are settings or per-plant profile values.

## 1. Reading the soil

- Every **60 s** (*Check sensors every*), each configured channel is read: 9 conversions on the ADS1115 at 860 samples/s, median taken.
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

If a session hits its limits while the soil is still below the start-watering point, you get *Target not reached*. If **two pulses produce less than 1 % rise**, the session stops as *No moisture response* (the sensor is probably out of the soil, or the water is not reaching the pot), and automatic watering for that plant pauses until the reading rises by 5 % on its own or you clear the fault.

The 30-day simulation in `test_scenarios` keeps four quite different plants inside their bands, with no overshoot beyond 3 % above "too wet" and never below "very dry".

## 5. Safety rules (every dose, automatic or manual)

A dose is refused if:
- the leak sensor is wet, an emergency stop is active, the reservoir is empty, another dose is running, or the plant's outlet is locked after a fault;
- the plant is disabled;
- *(automatic only)* automation is paused, the plant is manual-only, its sensor has failed, or the rest between sessions has not passed (pulses within a session are exempt).

The amount is then capped at the largest pulse (plant limit for automatic, global hard limit of 250 ml for manual), at what is left of the plant's daily limit (plant limit, or the 1,500 ml hard limit for manual), and at what is left of the 5,000 ml daily total for all plants. Doses below 5 ml are not worth opening an outlet for.

Each dose may keep its outlet open for at most **1.5 × the expected time + 3 s**, and never more than the hard limit (120 s). A hardware timer enforces the hard limit + 5 s even if the software hangs.

## 6. Dosing

**With the scale (closed loop).** The outlet opens and the reservoir weight is watched:
- it closes when the measured loss reaches the dose (2 ml early, for the water still in the tube);
- **no flow:** if after 60 % of the expected time (at least 4 s) less than max(3 ml, 15 %) has left, it closes, the outlet is **locked**, and *No flow* is raised;
- **slow flow:** if the time limit is hit with at least 25 % delivered, the partial dose is kept;
- after closing, it waits 4 s for drips, then **watches for 20 s**: a further loss of more than max(10 ml, 25 % of the dose) means a valve stuck open or a siphon, so it triggers an **emergency stop**, a critical alert and an outlet lockout.

**Without the scale (open loop).** The outlet stays open for *dose ÷ calibrated flow*. Gravity valves correct the flow for the current water height: flow ∝ √(drop + water depth).

## 7. Reservoir

- **Level** comes from the scale: net weight (container tared out) = ml.
- **Empty** when the float switch opens, the container is lifted off the scale, or less than 150 ml is left. Any one of them stops everything and aborts a running dose.
- **Low** below 20 % or when the forecast drops under 2 days (forecast = level ÷ average daily use).
- **Refill** detected when the stable weight rises by ≥ 200 ml. Lifting the container off and putting it back is recognised, not mistaken for a leak.
- **Unexplained loss:** ≥ 60 ml disappearing while nothing is being watered, either suddenly (confirmed over 30 s) or accumulated within 2 h. It means a leak or a stuck valve, so it triggers an emergency stop and a critical alert. Slow evaporation never trips it.
- If the float switch says empty but the scale shows over 30 %, one of them is faulty; the reservoir is treated as empty and you get a warning.

## 8. Sensor faults

| Fault | Detected when | Effect |
|---|---|---|
| Not calibrated | dry and wet points less than 1,500 counts apart | no automatic watering (info) |
| Bus error | 3 failed ADC reads in a row | sensor failed |
| Disconnected / shorted | 3 readings below 0.25 V or above 3.25 V | sensor failed |
| Out of range | 3 readings beyond the calibration points ± 25 % | sensor failed |
| No moisture response | watering produced no rise (see §4) | sensor failed until it rises or you clear it |
| Stuck | reading moved by less than 3 counts in 12 h | suspect: still used, warned |
| Erratic | 3 jumps of more than 25 % within an hour (not after watering) | suspect: still used, warned; clears after 6 quiet hours |

When a sensor **fails**, the plant's policy applies: **pause** (default) or **fixed schedule** (X ml every N days, inside the watering window). The fixed schedule runs only for a confirmed hardware fault (bus error, disconnected, out of range), never right after boot before the first reading, and never after "no moisture response", because then the water might not be reaching the pot.

## 9. Power cuts, reboots and outages

- Before an outlet opens, the device saves "dose in progress (channel, ml)" to flash. After an unexpected restart that dose is **counted in full** and the rest period starts from the restart, so a crash can never cause a double watering. You get an *Interrupted watering* warning.
- Daily totals reset at local midnight. Without a valid clock they reset every 24 h of uptime instead, so a missing clock can never lock watering forever.
- With no clock at all the watering window is ignored (better to water at night than not at all) and you get a *Clock not set* warning. Set it from the dashboard, or the DS3231 keeps it.
- Wi-Fi or internet outages change nothing about watering.

## 10. Environment alerts

Temperature or humidity outside a plant's range for more than 2 hours raises a note (info, not pushed to the phone) for that plant. Moisture below "very dry" raises *Plant too dry*. Above "too wet" for 2 days raises *Plant too wet* (check drainage).

## 11. Recommendations (dashboard)

`dashboard/src/lib/recommendations.ts` compares the profile with the last 7 days: daily light peak (median), average humidity, temperature extremes, waterings per day, weekly water use against the pot size, current wetness, resting season and reservoir forecast. It runs in the browser to keep the firmware small.
