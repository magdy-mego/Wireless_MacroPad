#pragma once

#include <Arduino.h>

#include "config_types.h"

namespace battery_manager {

struct BatteryState {
  uint8_t percent = 0;
  float voltage = 0.0f;
  bool charging = false;
  bool lowBattery = false;
  bool valid = false;
  uint32_t updatedAtMs = 0;
};

void begin(const config::BatteryConfig& cfg);
void applyConfig(const config::BatteryConfig& cfg);
bool updateIfDue();
bool updateNow();
BatteryState getState();
bool shouldEnterDeepSleep(uint32_t idleMs);

}  // namespace battery_manager
