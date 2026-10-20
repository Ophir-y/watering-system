# Getting started

## 1. Tools (WSL / Linux)

| Tool | Install | Used for |
|---|---|---|
| PlatformIO Core | `curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py && python3 get-platformio.py`, then link `~/.platformio/penv/bin/pio` into `~/.local/bin` | Building and testing the firmware. The first ESP32 build downloads the toolchain (~1 GB). |
| Node.js 24 | `nvm install 24` | The dashboard |
| make, git, g++ | `sudo apt install build-essential git` | Host tests and the Makefile |

The repo pins exact versions: ESP32 platform `espressif32 @ 7.1.3` (Arduino core 2.0.17) in `firmware/platformio.ini`, and npm packages in `dashboard/package-lock.json`.

## 2. Build and test

```sh
make test        # 113 firmware tests on this PC + dashboard lint, types and 28 tests
make all         # build the dashboard, embed it, build the firmware
make mock        # dashboard at http://localhost:5173 against a simulated device (password: plantcare)
```

Or step by step:

```sh
cd dashboard && npm ci && npm run build:embed     # writes firmware/src/generated/web_assets.h
cd ../firmware && pio test -e native && pio run -e esp32dev
```

The firmware also builds without the dashboard step; it then serves a placeholder page and the API still works.

## 3. Flash the ESP32 (first time over USB)

WSL cannot see USB devices by default. Pick one:

- **usbipd-win (recommended):** in an admin PowerShell, `winget install usbipd`, then `usbipd list`, `usbipd bind --busid <id>`, `usbipd attach --wsl --busid <id>`. In WSL the board appears as `/dev/ttyUSB0`, so `make flash` works. You may need `sudo usermod -aG dialout $USER`.
- **From Windows:** install the PlatformIO extension in VS Code on Windows, open `firmware/`, and use *Upload*.
- **Browser:** flash `firmware/.pio/build/esp32dev/firmware.bin` (plus the bootloader and partitions from the same folder) with Espressif's web flasher in Chrome.

After the first flash, update over Wi-Fi: *Settings → Maintenance → Upload firmware*, or `make ota DEVICE=plantcare.local PASSWORD=…`. A bad image rolls back by itself.

`make monitor` shows the serial log (115200 baud).

## 4. First boot

1. Power on. With no Wi-Fi configured the device opens a hotspot **PlantCare-XXXX**, password **`plantcare`**. The status LED double-flashes.
2. Join it from a phone or laptop; the setup page usually opens by itself. If not, browse to **http://192.168.4.1**.
3. The wizard asks for a device password (8+ characters), takes the time zone and clock from your browser, and lets you join your home Wi-Fi.
4. Reconnect your phone to the home network and open **http://plantcare.local** (or the IP your router gave it).

To get the hotspot back later, hold the **BOOT** button for 5 seconds; it stays up for 15 minutes. It also opens automatically if the home Wi-Fi is unreachable for 10 minutes. You can change its password under *Settings → Wi-Fi*.

## 5. Add plants

For each plant: **Add a plant** → choose a species (fills in the moisture band, light, temperature, humidity, watering style and resting months) → set the **channel** it is wired to and the **pot volume** → save.

Then on the plant page:

1. **Calibrate the sensor:** hold it in the air → *Set dry point*; stand it in a glass of water up to its line → *Set wet point*; push it back into the soil.
2. **Measure the outlet flow:**
   - With the scale: water once with *Water now*, then *Use it* on the measured flow.
   - Without it: *Run 10 s* into a measuring cup, enter the amount, then *Save flow*.
3. Watch the first few waterings. After 2–3 sessions the system has learned how much water raises this plant's moisture by 1 %. Adjust the band if the soil feels too wet or too dry at the target.

Orchids in bark: moisture sensors read poorly in bark, so the species sets **manual watering only**.

## 6. Reservoir scale

*Room & reservoir → Calibrate the scale:* put the empty container on the scale → *Tare empty reservoir* → pour in a measured amount (1 L is good) → *Save calibration*. Then fill it up. Refills are detected from now on.

## 7. Phone notifications (optional)

Install the **ntfy** app (Android/iOS), *Settings → Phone notifications → Generate a random topic*, enable notifications, save, subscribe to the same topic in the app, then *Send a test notification*. Self-hosted ntfy servers work too (`http://` or `https://` with a Let's Encrypt certificate).

## 8. Bring-up checklist

- [ ] Each plant's raw reading changes when you squeeze the soil or move the sensor between air and water.
- [ ] *Run 10 s* moves water, and the scale's measurement matches the cup.
- [ ] Pull the SD card: a *SD card* warning appears and the dashboard keeps showing the last day. Put it back: the warning clears within a minute.
- [ ] Switch off the router: watering continues; after 10 minutes the PlantCare hotspot appears.
- [ ] Lift the float switch: *Reservoir empty*, and nothing waters.
- [ ] Wet the leak sensor: everything stops, the buzzer beeps (outside quiet hours), and a phone notification arrives. Dry it and *Resume watering*.
- [ ] Unplug a moisture sensor: within 3 minutes *Sensor problem*, and the plant's fallback policy applies.
- [ ] Press BOOT briefly: *Stop all*. Resume from the dashboard.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| Moisture stuck at 0 % or "looks disconnected" | Sensor unplugged, NE555 sensor at 3.3 V, or missing ADS1115 (check its address). |
| All moisture readings drift together | Sensor supply noise: add the 100 nF capacitors, keep sensor cables away from pump wires. |
| "No flow" right away | Pump not primed, tube kinked, valve needs more pressure (gravity: use zero-pressure valves), or flow rate set far too high. |
| Waters too often | Sensor sits where the water runs past it; the soil drains too fast; the target is too high. |
| `unexpected_reset` after brown-out | Supply sags when a pump starts: use a stronger 12 V supply and a capacitor (470 µF+) on the 5 V rail. |
| Can't reach `plantcare.local` | Some Android versions lack mDNS: use the IP shown in *Settings → Wi-Fi* or your router. |
