#pragma once

#include <Arduino.h>

namespace taskcfg {

// Task execution periods.
constexpr uint32_t kBleScanPeriodMs = 5;
constexpr uint32_t kOledUpdatePeriodMs = 100;
constexpr uint32_t kBatterySamplePeriodMs = 30000;
constexpr uint32_t kBatteryCheckPeriodMs = 1000;

// FreeRTOS priorities (higher value means higher priority).
constexpr UBaseType_t kBleTaskPriority = 5;
constexpr UBaseType_t kWebTaskPriority = 3;
constexpr UBaseType_t kOledTaskPriority = 2;
constexpr UBaseType_t kBatteryTaskPriority = 1;

// Uniform stack allocation for worker tasks.
constexpr uint32_t kTaskStackWords = 4096;

}  // namespace taskcfg
