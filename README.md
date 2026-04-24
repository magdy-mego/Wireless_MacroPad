# ESP32-WROOM-32S Wireless MacroPad

Firmware and web configuration interface for a standalone **2x3 (6-key)** wireless macropad based on ESP32-WROOM-32S.

## Highlights

- Standalone operation (no host-side helper app required after BLE pairing).
- BLE HID output via `ESP32-BLE-Keyboard`.
- Standard BLE Battery Service support (`0x180F` / `0x2A19`, READ + NOTIFY).
- Filtered LiPo battery measurement with stable percentage mapping.
- WiFi AP config portal at `http://192.168.4.1`.
- 2x3 matrix + KY-040 encoder input engine.
- OLED status UI (SSD1306 128x64, I2C).
- Config persisted in LittleFS (`/config.json`) with atomic write behavior.
- Profile switching and persistence.
- Factory reset and power-saving deep sleep behavior.

## Hardware

- MCU: ESP32-WROOM-32S (target board profile: `esp32dev`).
- Inputs: 6 keys in 2x3 matrix + KY-040 encoder (CLK/DT/SW).
- Display: 0.96" SSD1306 I2C OLED.
- Power: USB 5V and/or LiPo + TP4056.
- Battery sensing: divider to ADC1 pin (`GPIO34`), default ratio `2.0` (100k/100k).

### Pin Map

- Matrix rows in use: `GPIO13`, `GPIO12`
- Matrix row reserved (legacy/optional): `GPIO14`
- Matrix cols: `GPIO27`, `GPIO26`, `GPIO25`
- Encoder: `GPIO33` (CLK), `GPIO32` (DT), `GPIO35` (SW)
- OLED I2C: `GPIO21` (SDA), `GPIO22` (SCL)
- Status LED: `GPIO2`
- Battery ADC: `GPIO34` (ADC1)

Detailed netlist: `docs/schematic_netlist.md`.

## Firmware Architecture

- `BLE_HID_Task` (5 ms scan period, priority 5): matrix/encoder scan, debounce events, BLE HID dispatch.
- `WebServer_Task` (priority 3): captive portal + REST API (50 ms loop delay).
- `OLED_Task` (100 ms update period, priority 2): status rendering.
- `Battery_Task` (1 s check period, priority 1): periodic battery checks + deep-sleep checks.

Core modules:

- `src/main.cpp` - task orchestration and high-level flow.
- `src/matrix.cpp` - 2x3 matrix + encoder scanning/debounce/event queue.
- `src/ble_handler.cpp` - HID dispatch and BLE battery-level publishing.
- `src/battery_manager.cpp` - ADC sampling, filtering, LiPo mapping, charging inference, low-battery logic.
- `src/storage.cpp` - config parsing/validation/atomic persistence.
- `src/web_server.cpp` - AP, captive portal, REST handlers.
- `src/oled_display.cpp` - OLED status screen.
- `src/power_manager.cpp` - deep sleep helper.

## Battery System

- Service UUID: `0x180F` (Battery Service)
- Characteristic UUID: `0x2A19` (Battery Level, `uint8_t` 0..100, READ + NOTIFY)
- Notifications are sent only when percentage changes.
- On reconnect, battery level is re-synced to host.

Measurement pipeline:

- ADC1 `GPIO34` with `ADC_11db`.
- `5..9` samples per reading (default `7`), sorted and trimmed (min/max removed).
- Voltage from `analogReadMilliVolts()` with divider ratio applied: `battery_voltage = adc_voltage * divider_ratio`.
- Calibration scale and offset applied for tuning.
- Moving average + EMA smoothing.
- LiPo lookup-table percentage interpolation (non-linear).
- Decrease-only behavior while not charging to avoid false upward jumps.
- Trend-based charging inference fallback when no dedicated charge-status pin is wired.

Low-battery behavior:

- Low-battery flag set at threshold (default `3.30V`) with 50mV hysteresis.
- Deep sleep on normal idle timeout (default `10 min`).
- Faster deep sleep on low battery when idle (default `60s`) and not charging.

## Web UI

The SPA is implemented with vanilla HTML/CSS/JS in `data/`:

- `index.html`, `style.css`, `app.js`
- compressed assets: `index.html.gz`, `style.css.gz`, `app.js.gz`

## Config Schema

Stored in LittleFS at `/config.json` (versioned, currently `version = 1`).

References:

- `data/config.default.json`
- `data/config.schema.json`

Action types accepted in config parsing:

- `combo`
- `sequence`
- `text`
- `media`
- `mouse`
- `layer_switch` (legacy/deprecated; parsed for compatibility but not exposed in UI and not executed)
- `none`

Canonical profile shape:

- `6` buttons per profile (2x3 layout)

Optional `battery` fields:

- `divider_ratio`
- `low_battery_threshold_v`
- `full_voltage_v`
- `empty_voltage_v`
- `update_interval_ms`
- `sample_count`
- `smoothing_window`
- `calibration_scale`
- `calibration_offset_v`
- `normal_idle_sleep_ms`
- `low_battery_idle_sleep_ms`

## REST API

- `GET /api/config`
- `POST /api/config`
- `GET /api/status`
- `POST /api/action`
- `POST /api/restart`

`POST /api/config` behavior:

- payload limit: `< 24KB` (returns `413` if exceeded)
- validation errors: `400`
- storage exhaustion: `507`
- internal failure: `500`

`GET /api/status` example:

```json
{
  "battery_percent": 87,
  "battery_voltage": 4.05,
  "charging": false,
  "low_battery": false,
  "battery": 4.05,
  "ble_connected": true,
  "active_profile": "Default",
  "ap_enabled": true,
  "wifi_client_connected": false
}
```

`POST /api/action` request body example:

```json
{"button_id": 0}
```

## Runtime Behaviors

- BLE disconnect: continues scanning and advertises for reconnect.
- AP credentials are currently hardcoded in firmware:
  - SSID: `MD`
  - Password: `MA#2580456`
- AP auto-timeout: disables AP after 10 minutes with no connected station.
- AP re-enable: hold encoder for about 3s.
- Profile cycle: encoder short press.
- Factory reset: hold encoder switch during boot for 5s (when enabled by build flag).
- Deep sleep: normal idle timeout, or faster low-battery idle timeout when not charging.
- AP/BLE coexistence policy: when AP mode starts, BLE is deinitialized; reboot is required after AP stop to restore BLE safely.

## Build and Flash

### Prerequisites

- PlatformIO Core
- ESP32 connected over USB

### Build

```bash
~/.platformio/penv/bin/pio run
```

### Build LittleFS image

```bash
~/.platformio/penv/bin/pio run -t buildfs
```

### Flash firmware

```bash
~/.platformio/penv/bin/pio run -t upload
```

### Flash LittleFS assets

```bash
~/.platformio/penv/bin/pio run -t uploadfs
```

### Serial monitor

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

## Project Config

`platformio.ini` is pinned for reproducibility:

- `platform = espressif32@6.5.0`
- `board = esp32dev`
- `framework = arduino`
- `board_build.filesystem = littlefs`
- `board_build.partitions = huge_app.csv`
- `monitor_speed = 115200`
- `upload_speed = 921600`

### Compile Flags

- `APP_VERSION="0.1.0"` - firmware version string.
- `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET` (default `0`) - enable factory reset on encoder hold during boot for 5s.
- `MACROPAD_AP_START_ON_BOOT` (default `0`) - enable AP automatically on boot.
- `MACROPAD_BLE_STRICT_AUTH` (default `0`) - enforce stricter BLE authentication.
- `CORE_DEBUG_LEVEL=3` - verbose debug logging.

### Dependencies

- `t-vk/ESP32 BLE Keyboard@^0.3.2` - BLE HID keyboard library.
- `https://github.com/esphome/ESPAsyncWebServer.git#v3.0.0` - async web server.
- `bblanchon/ArduinoJson@^7.0.4` - JSON parsing and serialization.
- `olikraus/U8g2@^2.36.18` - OLED display driver.
- `mathertel/RotaryEncoder@^1.6.0` - rotary encoder utility.

## Additional Docs

- Schematic/netlist: `docs/schematic_netlist.md`
- BOM: `docs/bom.md`
- Build guide: `docs/build_guide.md`

## Troubleshooting

### Battery Voltage Issues

- Oscillating battery percentage: verify divider resistors (default 100k/100k), add 100nF capacitor from ADC node to GND, calibrate using `calibration_scale` and `calibration_offset_v`.
- Battery not detected: verify `GPIO34` divider path and ground reference.
- Battery always 0%: verify divider ratio and update `divider_ratio` to match hardware.

### BLE Connection Issues

- Device not visible in scan: verify firmware boots cleanly and BLE library is installed.
- Frequent disconnects: check power stability and RF environment.
- Input not working: confirm BLE connected before action execution.

### WiFi / Web Configuration Issues

- Cannot access `http://192.168.4.1`: ensure AP is enabled (hold encoder for ~3s if auto-disabled).
- Config save fails with 507: not enough LittleFS space.
- AP shows but BLE no longer works after AP stop: this firmware policy requires reboot after AP mode.

### Hardware Troubleshooting

- Matrix keys not responding: verify row/column wiring for 2x3 matrix and no shorts.
- Encoder not responding: verify `GPIO33`, `GPIO32`, `GPIO35` plus external pull-up for `GPIO35`.
- OLED blank: verify I2C wiring (`GPIO21`, `GPIO22`) and power.

## Notes

- Mouse-scroll actions are currently mapped via keyboard fallback events (Page Up/Page Down) due BLE library constraints.
- `layer_switch` remains in parser for backward compatibility but is not executable in firmware.
