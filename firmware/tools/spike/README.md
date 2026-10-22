# Plant-node bench spike

A throwaway sketch you run once before trusting the node design. It answers four questions the code can't answer by itself. Wire one node as described in [docs/hardware/wiring.md](../../../docs/hardware/wiring.md): board, soil sensor, pump with MOSFET, diode and 1 Ω shunt, 3×AA with the divider.

```
cd firmware/tools/spike
pio run -e supermini_c3 -t upload -t monitor     # or xiao_c3 / xiao_s3
```

Type a letter in the serial monitor.

| Key | Test | What to write down | Decides |
|---|---|---|---|
| `s` | Soil sensor | mV in air, dry soil, wet soil and a glass of water | Whether dry soil stays below about 2450 mV. Above that, the C3's ADC flattens out, and a 100 kΩ/330 kΩ divider is needed on the sensor output. |
| `p` | Pump, under water | average mA and mA per volt | The "wet" pump current |
| `p` | Pump, lifted out of the water | average mA and mA per volt | **Dry ÷ wet below about 0.8:** leave *Feel the pump run dry* on. **Above 0.8:** turn it off in the plant's settings; the fuel gauge and the "soil didn't get wetter" check still apply. |
| `b` | Battery | mV with fresh cells | Divider check: compare with a multimeter across the holder |
| `l` | Light sleep | meter in series with the battery during the 10 s sleeps; every wake must say `send ok` | **Under about 0.5 mA:** the node firmware's light sleep is fine. **Higher:** look for a power LED first (the SuperMini's red one must go). If it is still high, the node needs deep sleep. |
| `d` | Deep sleep | the same current, for comparison | – |

## Range test

Flash the receiver on the main station's ESP32 DevKit:

```
pio run -e receiver -t upload -t monitor
```

Put the node where the plant will stand and run `l`. The receiver prints how many of the frames it heard. Anything above 90 % is fine, because the real protocol retries. At the edge of range, move the main station, or turn the node's antenna side towards it.

## Battery estimate

Use your measurements:

```
mAh per day ≈ sleep_mA × 24
            + (wakes per day × awake_ms / 3 600 000) × 80
            + (pump seconds per day / 3600) × pump_mA
```

With a 5-minute check-in there are 288 wakes per day. A plant using 150 ml a day at 1.5 ml/s runs the pump for 100 s. Divide about 2000 mAh (3×AA alkaline) by the daily total. For example, 0.3 mA of sleep current, 288 wakes of 40 ms each and 100 s at 180 mA comes to 7.2 + 0.3 + 5.0 ≈ 12.5 mAh a day, or about 5 months. Sleep current is usually the biggest term.
