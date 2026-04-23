# Build And First-Time Setup Guide

## 1. Assemble Hardware
1. Wire matrix rows to `GPIO13/12/14` and columns to `GPIO27/26/25`.
2. Wire KY-040 to `GPIO33 (CLK)`, `GPIO32 (DT)`, `GPIO35 (SW)`.
3. Wire OLED to `GPIO21 (SDA)`, `GPIO22 (SCL)`, `3V3`, `GND`.
4. Wire battery sense divider to `GPIO34` (ADC1). Default firmware assumes `100k` top + `100k` bottom (`divider_ratio = 2.0`).
5. Add a small capacitor (`100nF` typical) from the ADC sense node to GND for stable readings.
6. Connect TP4056 + LiPo + ESP32 power rails per `docs/schematic_netlist.md`.
7. Confirm common ground between all modules.

## 2. Prepare Firmware
1. Open project root in PlatformIO.
2. Confirm `platformio.ini` uses:
- `platform = espressif32@6.5.0`
- `board = esp32dev`
- `framework = arduino`
3. (Optional) Customize build flags in `platformio.ini` for your needs:
   - `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET=1` - Enable factory reset on encoder hold during boot (5s).
   - `MACROPAD_AP_START_ON_BOOT=1` - Start WiFi AP on boot instead of waiting for hold.
   - `APP_VERSION=\"X.Y.Z\"` - Change firmware version string.
4. Build firmware:
```bash
~/.platformio/penv/bin/pio run
```
5. Build LittleFS image:
```bash
~/.platformio/penv/bin/pio run -t buildfs
```

## 3. Flash Device
1. Connect ESP32 via USB.
2. Flash firmware:
```bash
~/.platformio/penv/bin/pio run -t upload
```
3. Flash web assets to LittleFS:
```bash
~/.platformio/penv/bin/pio run -t uploadfs
```
4. Open monitor:
```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

## 4. First Pairing + Configuration
1. Pair BLE device named `Macropad` from PC/Mac/Linux Bluetooth settings.
2. Connect phone/laptop WiFi to AP `Macropad_Config`.
3. Open browser at `http://192.168.4.1`.
4. Edit profiles/macros (and optional `battery` config fields) and click **Save to Device**.
5. Verify `/api/status` includes `battery_percent`, `battery_voltage`, `charging`, `low_battery`.
6. Press encoder to cycle profiles; hold encoder for 3s to re-enable AP if timed out.

### Input Modes

- **Normal Mode (BLE)**: Matrix and encoder input is sent to the paired BLE host. LED pulses when disconnected, steady when connected.
- **AP Mode (Web Config)**: When AP is active, the device enters web configuration mode. Matrix input is ignored (except encoder press for profile cycling). No BLE events are sent to avoid conflicts.

### Behavior Notes

- **AP Auto-Timeout**: The WiFi AP automatically disables after 120 seconds of no connected station to save power.
- **AP Re-enable**: Hold encoder switch for ~3 seconds to re-enable the AP.
- **Battery Calculation**: The ADC reading is multiplied by the configured `divider_ratio` to compute battery voltage. Default is `2.0` for a 50/50 resistor divider.

## 5. Battery Validation
- Monitor serial logs at `115200` for `[BAT]` lines.
- Confirm the battery percentage does not oscillate up/down while not charging.
- Confirm BLE host receives battery updates when percentage changes.
- Note: UI battery display behavior differs by OS and Bluetooth stack.

## 6. Recovery / Maintenance
- **Factory Reset** (if enabled with `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET=1`): Hold encoder switch during boot for 5 seconds to reset all settings to defaults.
- **AP Re-enable**: If AP auto-disables after 120s idle, hold encoder 3s to re-enable.
- **Profile Cycling**: Press encoder short press to cycle through configured profiles.
- **If config save fails with low space (HTTP 507)**: Reduce profile complexity and retry. Factory reset clears all profiles if needed.
- **Monitor firmware**: Open serial monitor at 115200 baud to view logs, battery readings, and debug information.
