# Getting started

## 1. Tools (WSL / Linux)

| Tool | Install | Used for |
|---|---|---|
| PlatformIO Core | `curl -fsSL -o get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py && python3 get-platformio.py`, then link `~/.platformio/penv/bin/pio` into `~/.local/bin` | Building and testing the firmware. The first builds download the ESP32, ESP32-C3 and ESP32-S3 toolchains (a few GB). |
| Node.js 24 | `nvm install 24` | The dashboard |
| make, git, g++ | `sudo apt install build-essential git` | Host tests and the Makefile |

The repo pins exact versions: ESP32 platform `espressif32 @ 7.1.3` (Arduino core 2.0.17) in `firmware/platformio.ini`, and npm packages in `dashboard/package-lock.json`.

## 2. Build and test

```sh
make test        # 174 firmware tests on this PC + dashboard lint, types and 36 tests
make all         # build the dashboard, embed it, build the main station and all three node boards
make mock        # dashboard at http://localhost:5173 against a simulated station and nodes (password: plantcare)
```

Or step by step:

```sh
cd dashboard && npm ci && npm run build:embed     # writes firmware/src/generated/web_assets.h
cd ../firmware && pio test -e native
pio run -e hub                                    # main station (ESP32 DevKit)
pio run -e node_supermini_c3                      # or node_xiao_c3, node_xiao_s3
```

The main station also builds without the dashboard step; it then serves a placeholder page and the API still works.

## 3. Bench test one node first

Before building every plant unit, run the [bench spike](../firmware/tools/spike/README.md) on one node with its real pump. It measures the sleep current (battery life), whether your pump's current drops when it runs dry, the soil sensor's range on the board's ADC, and the radio range to the main station's spot.

## 4. First boot: the main station

Flash it over USB (see [flashing](#flashing-from-wsl)): `make flash`.

1. Power it from a USB charger. With no Wi-Fi configured it opens a hotspot **PlantCare-XXXX**, password **`plantcare`**. The status LED double-flashes.
2. Join it from a phone or laptop; the setup page usually opens by itself. If not, browse to **http://192.168.4.1**.
3. The wizard asks for a password (8+ characters), takes the time zone and clock from your browser, and lets you join your home Wi-Fi.
4. Reconnect your phone to the home network and open **http://plantcare.local** (or the IP your router gave it).

To get the hotspot back later, hold the **BOOT** button for 5 seconds; it stays up for 15 minutes. It also opens automatically if the home Wi-Fi is unreachable for 10 minutes. The nodes follow the station to whatever channel it ends up on.

## 5. Add a plant (one node each)

1. Build the plant unit: [wiring](hardware/wiring.md#plant-node), [assembly](hardware/assembly.md#a-plant-unit).
2. Flash its node over USB: `make flash-node BOARD=supermini_c3` (or `xiao_c3`, `xiao_s3`). Unplug USB, switch the batteries on.
3. Within a minute the dashboard shows **New plant node found**. Choose **Add as a plant**.
4. Choose a species: it fills in the moisture band, light, temperature, humidity, watering style and resting months. Set the **pot volume** and how much the **bottle holds**, then save.
5. At its next check-in (the "Hello" interval is 20 s for the first 15 minutes) the node pairs and receives its settings. The LED flashes once.

Then on the plant page:

1. **Calibrate the sensor.** Hold it in the air, wait for a check-in, then *Set dry point*. Stand it in a glass of water up to its line, wait for a check-in, then *Set wet point*. Push it back into the soil. The node reads the soil at each check-in; the page says how old the reading was.
2. **Measure the pump flow.** Fill the bottle, hold the tube in a measuring cup, choose *Run 10 s* (it runs at the next check-in), enter what came out, then *Save flow*. The gauge also corrects the flow by itself each time the bottle runs dry.
3. Watch the first few waterings. After 2–3 sessions the node has learned how much water raises this plant's moisture by 1 %, and after 3 pump runs it knows the pump's normal current and can feel it run dry.

**Refilling:** fill the bottle, then press the node's button once. That resets the gauge and makes the node check in at once, and the plant's card lights up on the dashboard (handy for finding which pot is which).

**Pairing again:** hold the node's button for 5 seconds. It forgets the station and says hello again. A node whose flash was erased shows as *asks to pair again* on its plant; confirm it there.

Orchids in bark: moisture sensors read poorly in bark, so the species sets **manual watering only**.

## 6. Phone notifications (optional)

Install the **ntfy** app (Android/iOS). In *Settings → Phone notifications*, generate a random topic, enable notifications and save. Subscribe to the same topic in the app, then *Send a test notification*. Self-hosted ntfy servers work too (`http://` or `https://` with a Let's Encrypt certificate).

## 7. Bring-up checklist

- [ ] Each plant's raw reading changes between air and water, and moves when you squeeze the soil.
- [ ] *Run 10 s* moves water, and the amount matches the cup.
- [ ] *Water now* shows as queued, then happens at the next check-in.
- [ ] Lift the pump out of the water during a run (after a few waterings): it stops, and the bottle shows *empty: the pump ran dry*. Refill and press the button: the alert clears.
- [ ] Unplug the pump: *Pump drew no current*; *Clear faults* after plugging it back.
- [ ] Unplug a moisture sensor: within three check-ins *Sensor problem*, and the plant's fallback policy applies.
- [ ] Pull a node's batteries: *Node offline* after about 16 minutes. Put them back: it clears at the next check-in.
- [ ] Switch off the main station for an hour: the plants keep being watered; when it is back, the nodes report again within a few minutes.
- [ ] *Stop all*: every node stops at its next check-in. *Resume watering*.
- [ ] Pull the station's SD card: a note appears and the dashboard keeps showing the last day.

## Flashing from WSL

WSL cannot see USB devices by default. Pick one:

- **usbipd-win (recommended):** in an admin PowerShell, `winget install usbipd`, then `usbipd list`, `usbipd bind --busid <id>`, `usbipd attach --wsl --busid <id>`. In WSL the board appears as `/dev/ttyUSB0` (DevKit) or `/dev/ttyACM0` (C3/S3 boards with native USB). You may need `sudo usermod -aG dialout $USER`.
- **From Windows:** install the PlatformIO extension in VS Code on Windows, open `firmware/`, pick the environment and use *Upload*.
- **Browser:** flash `firmware/.pio/build/<env>/firmware.bin` (plus the bootloader and partitions from the same folder) with Espressif's web flasher in Chrome.

C3/S3 boards that do not show up: hold BOOT while plugging in USB, then flash. After the first flash, update the **main station** over Wi-Fi with *Settings → Maintenance → Upload firmware* or `make ota DEVICE=plantcare.local PASSWORD=…`; a bad image rolls back by itself. Nodes are updated over USB.

`make monitor` shows the serial log (115200 baud). Nodes light-sleep, which drops USB serial; build a node with `-DNODE_STAY_AWAKE` (add it to its `build_flags`) to keep the log while debugging.

## Troubleshooting

| Symptom | Likely cause |
|---|---|
| A node never shows up | Batteries in backwards or flat; it is still on USB power with the holder off; it is out of range (run the range test); the station is in hotspot-only mode on another channel (it will find it after a few tries). |
| *Node offline* now and then | Weak signal: move the station, or turn the node's antenna side towards it. The *Signal* figure on the plant page shows the share of answered messages. |
| Moisture stuck at 0 % or "looks disconnected" | Sensor unplugged, NE555 sensor at 3.3 V, or the sensor's power wire not on the SOIL PWR pin. |
| Dry reading flat near 2450–2500 mV | The C3's ADC is saturating: add the divider in [wiring](hardware/wiring.md#if-the-dry-soil-reading-looks-clipped). |
| *Pump drew no current* right away | Pump wire loose, MOSFET wired drain/source the wrong way, or the 1 Ω resistor not in the pump's ground path. No resistor fitted? Turn off *Feel the pump run dry*. |
| Bottle called empty too early | The pump's flow is set too high: measure it again. Without current sensing, the gauge stops at the reserve. |
| Water keeps dripping after the pump stops | Siphon: pierce the 1 mm anti-siphon hole in the tube above the water line. |
| `unexpected_reset` from a node | Weak batteries sag when the pump starts: replace them. |
| Batteries drain in weeks | SuperMini power LED still fitted, or the soil sensor wired to the battery instead of SOIL PWR. |
| Can't reach `plantcare.local` | Some Android versions lack mDNS: use the IP shown in *Settings → Wi-Fi* or your router. |
