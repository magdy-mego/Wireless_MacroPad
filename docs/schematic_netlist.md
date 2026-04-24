# ESP32-WROOM-32S MacroPad Schematic Netlist (Text)

## Power Domain

- `USB_5V` -> `TP4056 IN+`
- `USB_GND` -> `TP4056 IN-`
- `LiPo+` -> `TP4056 BAT+`
- `LiPo-` -> `TP4056 BAT-`
- `TP4056 OUT+` -> `ESP32 5V/VIN`
- `TP4056 OUT-` -> `ESP32 GND`
- `ESP32 3V3` -> `OLED VCC`
- `ESP32 GND` -> `OLED GND`, matrix ground, encoder GND

## Matrix (2x3)

- Rows (outputs in active firmware):
  - `R0` -> `GPIO13`
  - `R1` -> `GPIO12`
- Reserved row pin (legacy/optional, not scanned in 2x3 firmware):
  - `R2` -> `GPIO14`
- Columns (inputs with pull-up):
  - `C0` -> `GPIO27`
  - `C1` -> `GPIO26`
  - `C2` -> `GPIO25`
- Switch wiring: each switch between one active row (`R0`/`R1`) and one column (`C*`).

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

## Status and Battery

- Status LED -> `GPIO2`
- Battery sense (divider) -> `GPIO34` (ADC1)

### Battery Voltage Divider

```text
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

- `divider_ratio = (R_top + R_bot) / R_bot = 2.0` for 100k/100k.
- `Battery Voltage = ADC_mV * divider_ratio / 1000`.
- Suggested filter: `GPIO34 -> 100nF ceramic -> GND`.
- Keep battery measurement on ADC1 pins to avoid ADC2/WiFi conflicts.

### Configuration Range (Firmware Validation)

- `divider_ratio`: 1.1 to 12.0
- `low_battery_threshold_v`: between configured empty and full voltages
- `full_voltage_v`: 3.7V to 4.35V
- `empty_voltage_v`: 2.5V to 3.8V

## Reserved Pins

- `GPIO0`, `GPIO5` - strapping pins.
- `GPIO18`, `GPIO19` - reserved by project policy.
- `GPIO23` - reserved for future use.

### Safe ADC1 Pins (Additional Analog Inputs)

- `GPIO36`, `GPIO37`, `GPIO38`, `GPIO39`
- Battery sense already uses `GPIO34`

Note: Avoid ADC2 pins while WiFi is active.
