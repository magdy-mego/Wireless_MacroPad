# ESP32-WROOM-32S Wireless MacroPad

Firmware + web configuration interface for a standalone 3x3 wireless macropad based on ESP32-WROOM-32S.

## Highlights

- Standalone operation (no host-side app required after BLE pairing).
- BLE HID output using `ESP32-BLE-Keyboard`.
- Standard BLE Battery Service support (`0x180F` / `0x2A19`, READ + NOTIFY).
- Filtered LiPo battery measurement with stable percentage mapping.
- WiFi AP config portal (`Macropad_Config`) at `http://192.168.4.1`.
- 3x3 matrix + KY-040 encoder input engine.
- OLED status UI (SSD1306 128x64, I2C).
- Config persisted in LittleFS (`/config.json`) with atomic write behavior.
- Profile switching and persistence.
- Factory reset and power-saving deep sleep behavior.

## Hardware

- MCU: ESP32-WROOM-32S (target board profile: `esp32dev`).
- Inputs: 9 keys in 3x3 matrix + KY-040 encoder (CLK/DT/SW).
- Display: 0.96" SSD1306 I2C OLED.
- Power: USB 5V and/or LiPo + TP4056.
- Battery sensing: divider to ADC1 pin (`GPIO34`), default ratio `2.0` (100k/100k).

### Pin Map

- Matrix rows: `GPIO13`, `GPIO12`, `GPIO14`
- Matrix cols: `GPIO27`, `GPIO26`, `GPIO25`
- Encoder: `GPIO33` (CLK), `GPIO32` (DT), `GPIO35` (SW)
- OLED I2C: `GPIO21` (SDA), `GPIO22` (SCL)
- Status LED: `GPIO2`
- Battery ADC: `GPIO34` (ADC1)

Detailed netlist: `docs/schematic_netlist.md`.

## Firmware Architecture

- `BLE_HID_Task` (5 ms scan period, priority 5): matrix/encoder scan, debounce events, BLE HID dispatch.
- `WebServer_Task` (priority 3): captive-portal + REST API (50 ms loop delay).
- `OLED_Task` (100 ms update period, priority 2): status rendering.
- `Battery_Task` (1 s check period, priority 1): periodic battery measurements (30 s sample period) + deep-sleep checks.

Core modules:

- `src/main.cpp` - task orchestration and high-level flow.
- `src/matrix.cpp` - 3x3 matrix + encoder scanning/debounce/event queue.
- `src/ble_handler.cpp` - HID dispatch and BLE battery-level publishing.
- `src/battery_manager.cpp` - ADC sampling, filtering, LiPo mapping, charging inference, low-battery logic.
- `src/storage.cpp` - config parsing/validation/atomic persistence.
- `src/web_server.cpp` - AP, captive portal, REST handlers.
- `src/oled_display.cpp` - OLED status screen.
- `src/power_manager.cpp` - deep sleep entry helper.

## Battery System

- Service UUID: `0x180F` (Battery Service)
- Characteristic UUID: `0x2A19` (Battery Level, `uint8_t` 0..100, READ + NOTIFY)
- Notifications are sent only when percentage changes.
- On reconnect, battery level is re-synced to host.

Measurement pipeline:

- ADC1 `GPIO34` with `ADC_11db`.
- `5..9` samples per reading (default `7`), sorted and trimmed (min/max removed).
- Voltage from `analogReadMilliVolts()` with divider ratio applied: `battery_voltage = adc_voltage * divider_ratio`.
- Calibration scale and offset applied for fine-tuning.
- Moving average + EMA smoothing.
- LiPo lookup-table percentage interpolation (non-linear).
- Decrease-only behavior while not charging to avoid false upward jumps.
- Trend-based charging inference fallback when no dedicated charge-status pin is wired.

Low-battery behavior:

- Low-battery flag set at threshold (default `3.30V`) with 50mV hysteresis to avoid oscillation.
- Deep sleep on normal idle timeout (default `60s`).
- Faster deep sleep on low battery when idle (default `15s`) and not charging.

Note:

- Host UI battery display is OS/stack dependent. The firmware uses the standard GATT method for best compatibility, but display location/format varies by system.

## Web UI

The SPA is implemented with vanilla HTML/CSS/JS in `data/`:

- `index.html`, `style.css`, `app.js`
- compressed assets: `index.html.gz`, `style.css.gz`, `app.js.gz`

Compressed UI payload size:

- `index.html.gz`: 1426 bytes
- `style.css.gz`: 1621 bytes
- `app.js.gz`: 3607 bytes
- total: 6654 bytes

## Config Schema

Stored in LittleFS at `/config.json` (versioned, currently `version = 1`).
Default and schema references:

- `data/config.default.json`
- `data/config.schema.json`

Action types supported in config:

- `combo`
- `text`
- `media`
- `mouse`
- `layer_switch`
- `none`

Optional `battery` config object fields:

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

`POST /api/config` behavior:

- payload limit: `< 500KB` (returns `413` if exceeded)
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
- AP auto-timeout: disables AP after 120s with no connected station.
- AP re-enable: hold encoder for about 3s.
- Profile cycle: encoder short press.
- Factory reset: hold encoder switch during boot for 5s.
- Deep sleep: normal idle timeout, or faster low-battery idle timeout when not charging.

## Build & Flash

### Prerequisites

- PlatformIO Core (this workspace uses `~/.platformio/penv/bin/pio`).
- ESP32 connected over USB.

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

- `APP_VERSION="0.1.0"` - Firmware version (can be customized in `platformio.ini`).
- `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET` (default `0`/disabled) - Enable factory reset on encoder hold during boot for 5s.
- `MACROPAD_AP_START_ON_BOOT` (default `0`/disabled) - Enable AP automatically on boot.
- `MACROPAD_BLE_STRICT_AUTH` (default `0`/disabled) - Enforce stricter BLE authentication.
- `CORE_DEBUG_LEVEL=3` - Enable verbose debug logging.

### Dependencies

- `t-vk/ESP32 BLE Keyboard@^0.3.2` - BLE HID keyboard library.
- `https://github.com/esphome/ESPAsyncWebServer.git#v3.0.0` - Async web server.
- `bblanchon/ArduinoJson@^7.0.4` - JSON parsing and serialization.
- `olikraus/U8g2@^2.36.18` - OLED display driver.
- `mathertel/RotaryEncoder@^1.6.0` - Rotary encoder library.

## Additional Docs

- Schematic/netlist: `docs/schematic_netlist.md`
- BOM: `docs/bom.md`
- Build guide: `docs/build_guide.md`

## Troubleshooting

### Battery Voltage Issues
- **Oscillating battery percentage**: Ensure battery divider resistors are correct (default 100k/100k). Add 100nF capacitor across ADC input to GND for noise filtering. Calibrate using `calibration_scale` and `calibration_offset_v` fields.
- **Battery not detected**: Verify `GPIO34` connection to divider. Check that ADC1 pins are used (avoid ADC2 during WiFi activity).
- **Battery always showing 0%**: Check divider ratio calculation. If using different resistor values, adjust `divider_ratio` in config accordingly.

### BLE Connection Issues
- **Device not visible in Bluetooth scan**: Verify `ESP32-BLE-Keyboard` library is installed. Check firmware build without errors. Try power cycling the device.
- **Disconnects frequently**: May indicate weak signal or power supply issues. Ensure adequate 3.3V supply. Check antenna placement if external antenna is used.
- **Keyboard input not working**: Verify matrix scanning works via serial monitor. Confirm BLE connection before sending keys.

### WiFi / Web Configuration Issues
- **Cannot access `http://192.168.4.1`**: Ensure AP is enabled (hold encoder 3s if auto-disabled). Check device is broadcasting `Macropad_Config` SSID. Verify AP is not in deep sleep.
- **Config save fails with 507 error**: Insufficient LittleFS space. Factory reset to clear profiles, or reduce config size.
- **OLED not updating during web config**: OLED updates are disabled while AP is active to reduce power draw. Resume normal operation after exiting AP mode.

### Hardware Troubleshooting
- **Matrix keys not responding**: Test with serial monitor to see if matrix events are logged. Check switch wiring to correct row/column pins. Verify no short circuits between pins.
- **Encoder not responding**: Check all three encoder pins (`GPIO33`, `GPIO32`, `GPIO35`) are connected. Test encoder with `matrix::pollEvent()` debug logs.
- **OLED not displaying**: Verify I2C address (default `0x3C`). Check SDA/SCL connections to `GPIO21`/`GPIO22`. Confirm 3.3V power to OLED.

## Notes

- Mouse-scroll actions are currently mapped via keyboard fallback events (Page Up/Page Down) due BLE library constraints in this implementation.
