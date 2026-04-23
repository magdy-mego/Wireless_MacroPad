#pragma once

#include <Arduino.h>

namespace power_manager {

void begin();
float readBatteryVoltage();
bool shouldEnterDeepSleep(uint32_t idleMs, float batteryVoltage);
void enterDeepSleep();

}  // namespace power_manager
