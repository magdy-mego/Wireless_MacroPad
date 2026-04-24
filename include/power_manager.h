#pragma once

#include <Arduino.h>

namespace power_manager {

// Configure ADC input used by simple battery helper.
void begin();

// Read current battery voltage estimate from ADC + divider.
float readBatteryVoltage();

// Legacy deep-sleep decision helper.
bool shouldEnterDeepSleep(uint32_t idleMs, float batteryVoltage);

// Enter ESP32 deep sleep and configure wake source.
void enterDeepSleep();

}  // namespace power_manager
