# ESP32-WROOM-32S MacroPad Schematic Netlist (Text)

## Power Domain
- `USB_5V` -> `TP4056 IN+`
- `USB_GND` -> `TP4056 IN-`
- `LiPo+` -> `TP4056 BAT+`
- `LiPo-` -> `TP4056 BAT-`
- `TP4056 OUT+` -> `ESP32 5V/VIN`
- `TP4056 OUT-` -> `ESP32 GND`
- `ESP32 3V3` -> `OLED VCC`
- `ESP32 GND` -> `OLED GND`, matrix common ground, encoder GND

## Matrix (3x3)
- Rows (outputs):
- `R0` -> `GPIO13`
- `R1` -> `GPIO12`
- `R2` -> `GPIO14`
- Columns (inputs pull-up):
- `C0` -> `GPIO27`
- `C1` -> `GPIO26`
- `C2` -> `GPIO25`
- Switch wiring: each switch between one `R*` and one `C*` intersection.

## Rotary Encoder (KY-040)
- `CLK` -> `GPIO33`
- `DT` -> `GPIO32`
- `SW` -> `GPIO35`
- `+` -> `3V3`
- `GND` -> `GND`

## OLED SSD1306 (I2C)
- `SDA` -> `GPIO21`
- `SCL` -> `GPIO22`
- `VCC` -> `3V3`
- `GND` -> `GND`

## Status / Battery
- Status LED -> `GPIO2` (onboard LED, indicates BLE connection state)
- Battery sense (through divider) -> `GPIO34` (ADC1, measures battery voltage)

### Battery Voltage Divider

The divider configuration determines how raw ADC readings map to battery voltage:

```
Battery+ (0-4.2V)
    |
   [R_top] (100k typical)
    |
    +-- GPIO34 (ADC input)
    |
  [R_bot] (100k typical)
    |
   GND
```

**Calculation**: `divider_ratio = (R_top + R_bot) / R_bot = 200k / 100k = 2.0`

**Battery Voltage** = (ADC reading in mV) × divider_ratio / 1000

Suggested noise filter: `GPIO34 -> 100nF ceramic -> GND` for stable readings.

Keep battery measurement on ADC1 pins to avoid conflicts with WiFi (ADC2).

### Configuration Range

- `divider_ratio`: 1.1 to 12.0 (clamped in firmware to prevent invalid readings)
- `low_battery_threshold_v`: 2.5V to 4.2V (typical LiPo range)
- `full_voltage_v`: LiPo full charge voltage (usually 4.2V)
- `empty_voltage_v`: LiPo cutoff voltage (usually 3.0V - 3.2V)

## Reserved Pins

The following pins are reserved and should not be used:

- `GPIO0`, `GPIO5` - Strapping pins (affect boot mode).
- `GPIO18`, `GPIO19` - ESP32 internal use.
- `GPIO23` - Reserved for future use or internal functions.

**Safe ADC1 Pins** (for additional analog inputs):
- `GPIO36`, `GPIO37`, `GPIO38`, `GPIO39` (all ADC1 channel pins)
- Battery sense uses `GPIO34`

**Note**: Avoid ADC2 pins if WiFi is active, as ADC2 is shared with WiFi radio and will cause conflicts.
