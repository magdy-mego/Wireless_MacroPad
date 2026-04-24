# Build and First-Time Setup Guide

## 1. Assemble Hardware

1. Wire matrix rows to `GPIO13` and `GPIO12` (2x3 layout).
2. Wire matrix columns to `GPIO27`, `GPIO26`, and `GPIO25`.
3. Keep `GPIO14` unconnected or reserved for future expansion (not scanned in 2x3 firmware).
4. Wire KY-040 to `GPIO33 (CLK)`, `GPIO32 (DT)`, `GPIO35 (SW)`.
5. Wire OLED to `GPIO21 (SDA)`, `GPIO22 (SCL)`, `3V3`, `GND`.
6. Wire battery sense divider to `GPIO34` (ADC1). Default firmware assumes `100k` top + `100k` bottom (`divider_ratio = 2.0`).
7. Add a small capacitor (`100nF` typical) from ADC sense node to GND for stable readings.
8. Connect TP4056 + LiPo + ESP32 rails per `docs/schematic_netlist.md`.
9. Confirm common ground between all modules.

## 2. Prepare Firmware

1. Open project root in PlatformIO.
2. Confirm `platformio.ini` uses:
   - `platform = espressif32@6.5.0`
   - `board = esp32dev`
   - `framework = arduino`
3. Optional build flags in `platformio.ini`:
   - `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET=1` to enable factory reset on encoder hold during boot (5s).
   - `MACROPAD_AP_START_ON_BOOT=1` to start AP on boot.
   - `APP_VERSION="X.Y.Z"` to set firmware version.
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

4. Open serial monitor:

```bash
~/.platformio/penv/bin/pio device monitor -b 115200
```

## 4. First Pairing and Configuration

1. Pair BLE device named `Macropad` from host Bluetooth settings.
2. Connect phone/laptop WiFi to AP SSID `MD`.
3. Use AP password `MA#2580456`.
4. Open browser at `http://192.168.4.1`.
5. Edit profiles/macros and click **Save to Device**.
6. Verify `/api/status` fields include `battery_percent`, `battery_voltage`, `charging`, and `low_battery`.
7. Press encoder to cycle profiles; hold encoder for ~3s to re-enable AP when needed.

### Input Modes

- **Normal Mode (BLE)**: Matrix and encoder input is sent to paired BLE host. LED pulses when disconnected and stays solid when connected.
- **AP Mode (Web Config)**: While AP is active, matrix key dispatch to BLE is suspended to avoid conflicts.

### Behavior Notes

- **AP Auto-Timeout**: AP disables after 10 minutes without connected stations.
- **AP Re-enable**: Hold encoder switch for ~3 seconds.
- **AP/BLE Policy**: AP startup deinitializes BLE; reboot is required after AP stop to restore BLE safely.
- **Battery Calculation**: ADC reading is multiplied by configured `divider_ratio`.

## 5. Battery Validation

- Monitor serial logs at `115200` for `[BAT]` lines.
- Confirm battery percentage does not oscillate up/down while not charging.
- Confirm BLE host receives battery updates when percentage changes.
- Note: Battery display behavior differs by OS/Bluetooth stack.

## 6. Recovery and Maintenance

- **Factory Reset**: If enabled with `MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET=1`, hold encoder switch during boot for 5 seconds.
- **AP Re-enable**: If AP auto-disables after timeout, hold encoder 3s.
- **Profile Cycling**: Short encoder press cycles profiles.
- **Config Save HTTP 507**: Reduce config complexity or clear storage.
- **Monitoring**: Keep serial monitor open at 115200 for runtime diagnostics.

## 7. Action Compatibility Notes

- `sequence` is fully supported in UI and firmware.
- `layer_switch` is deprecated: parser keeps backward compatibility, but UI does not expose it and firmware does not execute it.
