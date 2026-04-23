#include "power_manager.h"

#include <esp_sleep.h>

#include "pins.h"

namespace {
constexpr float kAdcRefVoltage = 3.3f;
constexpr int kAdcMax = 4095;
constexpr float kDividerRatio = 2.0f;
constexpr float kLowBatteryThreshold = 3.3f;
constexpr uint32_t kIdleDeepSleepMs = 60000;
}  // namespace

namespace power_manager {

void begin() {
  analogReadResolution(12);
  pinMode(static_cast<uint8_t>(pins::kBatteryAdc), INPUT);
}

float readBatteryVoltage() {
  const int raw = analogRead(static_cast<uint8_t>(pins::kBatteryAdc));
  return (static_cast<float>(raw) / static_cast<float>(kAdcMax)) * kAdcRefVoltage * kDividerRatio;
}

bool shouldEnterDeepSleep(uint32_t idleMs, float batteryVoltage) {
  return (idleMs >= kIdleDeepSleepMs) || (batteryVoltage > 0.0f && batteryVoltage < kLowBatteryThreshold);
}

void enterDeepSleep() {
  esp_sleep_enable_ext0_wakeup(pins::kEncoderSw, 0);
  esp_deep_sleep_start();
}

}  // namespace power_manager
