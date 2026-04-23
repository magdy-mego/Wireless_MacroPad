#include "battery_manager.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "freertos/FreeRTOS.h"
#include "pins.h"

namespace {
constexpr uint8_t kMinSampleCount = 5;
constexpr uint8_t kMaxSampleCount = 9;
constexpr uint8_t kMinSmoothingWindow = 1;
constexpr uint8_t kMaxSmoothingWindow = 10;
constexpr float kLowBatteryHysteresisV = 0.05f;
constexpr float kEmaAlpha = 0.35f;
constexpr float kChargeTrendStepV = 0.006f;
constexpr uint8_t kChargeTrendAssertSamples = 2;
constexpr uint8_t kChargeTrendClearSamples = 2;
constexpr float kCurveReferenceEmptyV = 3.20f;
constexpr float kCurveReferenceFullV = 4.20f;

struct LutPoint {
  float voltage;
  uint8_t percent;
};

constexpr std::array<LutPoint, 21> kLipoCurve = {{
    {4.20f, 100}, {4.15f, 95}, {4.11f, 90}, {4.08f, 85}, {4.02f, 80}, {3.98f, 75},
    {3.95f, 70},  {3.91f, 65}, {3.87f, 60}, {3.85f, 55}, {3.84f, 50}, {3.82f, 45},
    {3.80f, 40},  {3.79f, 35}, {3.77f, 30}, {3.75f, 25}, {3.73f, 20}, {3.71f, 15},
    {3.69f, 10},  {3.61f, 5},  {3.30f, 0},
}};

template <typename T>
T clampValue(T value, T minValue, T maxValue) {
  if (value < minValue) {
    return minValue;
  }
  if (value > maxValue) {
    return maxValue;
  }
  return value;
}

void sanitizeConfig(config::BatteryConfig* cfg) {
  if (cfg == nullptr) {
    return;
  }

  cfg->dividerRatio = clampValue(cfg->dividerRatio, 1.1f, 12.0f);
  cfg->emptyVoltageV = clampValue(cfg->emptyVoltageV, 2.5f, 3.8f);
  cfg->fullVoltageV = clampValue(cfg->fullVoltageV, 3.7f, 4.35f);
  if (cfg->fullVoltageV <= cfg->emptyVoltageV) {
    cfg->fullVoltageV = cfg->emptyVoltageV + 0.1f;
  }

  cfg->lowBatteryThresholdV =
      clampValue(cfg->lowBatteryThresholdV, cfg->emptyVoltageV, cfg->fullVoltageV);
  cfg->updateIntervalMs = clampValue<uint32_t>(cfg->updateIntervalMs, 5000, 600000);
  cfg->sampleCount = clampValue<uint8_t>(cfg->sampleCount, kMinSampleCount, kMaxSampleCount);
  cfg->smoothingWindow =
      clampValue<uint8_t>(cfg->smoothingWindow, kMinSmoothingWindow, kMaxSmoothingWindow);
  cfg->calibrationScale = clampValue(cfg->calibrationScale, 0.8f, 1.2f);
  cfg->calibrationOffsetV = clampValue(cfg->calibrationOffsetV, -0.5f, 0.5f);
  cfg->normalIdleSleepMs = clampValue<uint32_t>(cfg->normalIdleSleepMs, 10000, 3600000);
  cfg->lowBatteryIdleSleepMs =
      clampValue<uint32_t>(cfg->lowBatteryIdleSleepMs, 5000, cfg->normalIdleSleepMs);
}

portMUX_TYPE gBatteryMux = portMUX_INITIALIZER_UNLOCKED;
config::BatteryConfig gConfig;
battery_manager::BatteryState gState;
uint32_t gLastUpdateMs = 0;

std::array<float, kMaxSmoothingWindow> gMovingWindow{};
uint8_t gMovingWindowCount = 0;
uint8_t gMovingWindowWriteIndex = 0;
float gEmaVoltage = 0.0f;
bool gHasEmaVoltage = false;
float gPreviousTrendVoltage = 0.0f;
bool gHasTrendReference = false;
uint8_t gTrendRiseCount = 0;
uint8_t gTrendFallCount = 0;

void resetFilterStateLocked() {
  gMovingWindow.fill(0.0f);
  gMovingWindowCount = 0;
  gMovingWindowWriteIndex = 0;
  gEmaVoltage = 0.0f;
  gHasEmaVoltage = false;
  gPreviousTrendVoltage = 0.0f;
  gHasTrendReference = false;
  gTrendRiseCount = 0;
  gTrendFallCount = 0;
}

float readRawBatteryVoltage(const config::BatteryConfig& cfg) {
  const uint8_t pin = static_cast<uint8_t>(pins::kBatteryAdc);
  const uint8_t sampleCount = clampValue<uint8_t>(cfg.sampleCount, kMinSampleCount, kMaxSampleCount);
  std::array<uint32_t, kMaxSampleCount> samplesMv{};

  for (uint8_t i = 0; i < sampleCount; ++i) {
    samplesMv[i] = analogReadMilliVolts(pin);
    delay(2);
  }

  std::sort(samplesMv.begin(), samplesMv.begin() + sampleCount);

  uint8_t start = 0;
  uint8_t end = sampleCount;
  if (sampleCount > 2) {
    start = 1;
    end = sampleCount - 1;
  }

  uint32_t sum = 0;
  for (uint8_t i = start; i < end; ++i) {
    sum += samplesMv[i];
  }

  const uint8_t keptCount = end - start;
  const float adcVoltage = (static_cast<float>(sum) / static_cast<float>(keptCount)) / 1000.0f;
  float batteryVoltage = adcVoltage * cfg.dividerRatio;
  batteryVoltage = (batteryVoltage * cfg.calibrationScale) + cfg.calibrationOffsetV;
  return batteryVoltage > 0.0f ? batteryVoltage : 0.0f;
}

float applyVoltageFilteringLocked(float rawVoltage, const config::BatteryConfig& cfg) {
  const uint8_t window = clampValue<uint8_t>(cfg.smoothingWindow, kMinSmoothingWindow, kMaxSmoothingWindow);

  if (gMovingWindowCount < window) {
    gMovingWindow[gMovingWindowCount] = rawVoltage;
    ++gMovingWindowCount;
    if (gMovingWindowCount == window) {
      gMovingWindowWriteIndex = 0;
    }
  } else {
    gMovingWindow[gMovingWindowWriteIndex] = rawVoltage;
    gMovingWindowWriteIndex = static_cast<uint8_t>((gMovingWindowWriteIndex + 1) % window);
  }

  float movingAverage = 0.0f;
  for (uint8_t i = 0; i < gMovingWindowCount; ++i) {
    movingAverage += gMovingWindow[i];
  }
  movingAverage /= static_cast<float>(gMovingWindowCount);

  if (!gHasEmaVoltage) {
    gEmaVoltage = movingAverage;
    gHasEmaVoltage = true;
  } else {
    gEmaVoltage = (kEmaAlpha * movingAverage) + ((1.0f - kEmaAlpha) * gEmaVoltage);
  }

  return gEmaVoltage;
}

bool inferChargingLocked(float filteredVoltage, bool previousCharging) {
  if (!gHasTrendReference) {
    gPreviousTrendVoltage = filteredVoltage;
    gHasTrendReference = true;
    return previousCharging;
  }

  const float delta = filteredVoltage - gPreviousTrendVoltage;
  gPreviousTrendVoltage = filteredVoltage;

  if (delta > kChargeTrendStepV) {
    gTrendRiseCount = static_cast<uint8_t>(gTrendRiseCount + 1);
    gTrendFallCount = 0;
  } else if (delta < -kChargeTrendStepV) {
    gTrendFallCount = static_cast<uint8_t>(gTrendFallCount + 1);
    gTrendRiseCount = 0;
  } else {
    if (gTrendRiseCount > 0) {
      --gTrendRiseCount;
    }
    if (gTrendFallCount > 0) {
      --gTrendFallCount;
    }
  }

  bool charging = previousCharging;
  if (gTrendRiseCount >= kChargeTrendAssertSamples) {
    charging = true;
  } else if (gTrendFallCount >= kChargeTrendClearSamples) {
    charging = false;
  }
  return charging;
}

float normalizeVoltageForCurve(const config::BatteryConfig& cfg, float measuredVoltage) {
  const float span = cfg.fullVoltageV - cfg.emptyVoltageV;
  if (span <= 0.01f) {
    return measuredVoltage;
  }

  float ratio = (measuredVoltage - cfg.emptyVoltageV) / span;
  ratio = clampValue(ratio, 0.0f, 1.0f);
  return kCurveReferenceEmptyV + (ratio * (kCurveReferenceFullV - kCurveReferenceEmptyV));
}

uint8_t mapVoltageToPercent(const config::BatteryConfig& cfg, float measuredVoltage) {
  const float normalizedVoltage = normalizeVoltageForCurve(cfg, measuredVoltage);

  if (normalizedVoltage >= kLipoCurve.front().voltage) {
    return 100;
  }
  if (normalizedVoltage <= kLipoCurve.back().voltage) {
    return 0;
  }

  for (size_t i = 0; i + 1 < kLipoCurve.size(); ++i) {
    const LutPoint& upper = kLipoCurve[i];
    const LutPoint& lower = kLipoCurve[i + 1];
    if (normalizedVoltage <= upper.voltage && normalizedVoltage >= lower.voltage) {
      const float range = upper.voltage - lower.voltage;
      if (range <= 0.0f) {
        return lower.percent;
      }
      const float ratio = (normalizedVoltage - lower.voltage) / range;
      const float interpolated =
          static_cast<float>(lower.percent) +
          (ratio * static_cast<float>(upper.percent - lower.percent));
      const int rounded = static_cast<int>(lroundf(interpolated));
      return static_cast<uint8_t>(clampValue(rounded, 0, 100));
    }
  }

  return 0;
}

config::BatteryConfig getConfigSnapshot() {
  portENTER_CRITICAL(&gBatteryMux);
  const config::BatteryConfig cfg = gConfig;
  portEXIT_CRITICAL(&gBatteryMux);
  return cfg;
}

}  // namespace

namespace battery_manager {

void begin(const config::BatteryConfig& cfg) {
  const uint8_t batteryPin = static_cast<uint8_t>(pins::kBatteryAdc);
  analogReadResolution(12);
  analogSetPinAttenuation(batteryPin, ADC_11db);
  pinMode(batteryPin, INPUT);

  applyConfig(cfg);
}

void applyConfig(const config::BatteryConfig& cfg) {
  config::BatteryConfig nextCfg = cfg;
  sanitizeConfig(&nextCfg);

  portENTER_CRITICAL(&gBatteryMux);
  const bool smoothingChanged = nextCfg.smoothingWindow != gConfig.smoothingWindow;
  const bool sampleChanged = nextCfg.sampleCount != gConfig.sampleCount;
  gConfig = nextCfg;
  if (smoothingChanged || sampleChanged) {
    resetFilterStateLocked();
  }
  portEXIT_CRITICAL(&gBatteryMux);
}

bool updateIfDue() {
  const config::BatteryConfig cfg = getConfigSnapshot();

  uint32_t lastUpdateMs = 0;
  bool hasValidState = false;
  portENTER_CRITICAL(&gBatteryMux);
  lastUpdateMs = gLastUpdateMs;
  hasValidState = gState.valid;
  portEXIT_CRITICAL(&gBatteryMux);

  const uint32_t now = millis();
  if (!hasValidState || lastUpdateMs == 0 || (now - lastUpdateMs) >= cfg.updateIntervalMs) {
    return updateNow();
  }
  return false;
}

bool updateNow() {
  const config::BatteryConfig cfg = getConfigSnapshot();
  const float rawVoltage = readRawBatteryVoltage(cfg);

  BatteryState nextState;
  uint8_t mappedPercent = 0;

  portENTER_CRITICAL(&gBatteryMux);
  const float filteredVoltage = applyVoltageFilteringLocked(rawVoltage, cfg);
  mappedPercent = mapVoltageToPercent(cfg, filteredVoltage);

  nextState = gState;
  nextState.valid = true;
  nextState.voltage = filteredVoltage;
  nextState.updatedAtMs = millis();
  nextState.charging = inferChargingLocked(filteredVoltage, nextState.charging);

  if (!gState.valid) {
    nextState.percent = mappedPercent;
  } else if (nextState.charging) {
    if (mappedPercent > nextState.percent) {
      const uint8_t rising = static_cast<uint8_t>(nextState.percent + 1);
      nextState.percent = rising < mappedPercent ? rising : mappedPercent;
    } else {
      nextState.percent = mappedPercent;
    }
  } else {
    nextState.percent = mappedPercent < nextState.percent ? mappedPercent : nextState.percent;
  }

  if (!nextState.lowBattery && filteredVoltage <= cfg.lowBatteryThresholdV) {
    nextState.lowBattery = true;
  } else if (nextState.lowBattery &&
             filteredVoltage >= (cfg.lowBatteryThresholdV + kLowBatteryHysteresisV)) {
    nextState.lowBattery = false;
  }

  gState = nextState;
  gLastUpdateMs = nextState.updatedAtMs;
  portEXIT_CRITICAL(&gBatteryMux);

  Serial.printf(
      "[BAT] Vraw=%.3fV Vflt=%.3fV pct=%u mapped=%u charging=%s low=%s interval=%lums\n", rawVoltage,
      nextState.voltage, nextState.percent, mappedPercent, nextState.charging ? "true" : "false",
      nextState.lowBattery ? "true" : "false", static_cast<unsigned long>(cfg.updateIntervalMs));

  return true;
}

BatteryState getState() {
  portENTER_CRITICAL(&gBatteryMux);
  const BatteryState state = gState;
  portEXIT_CRITICAL(&gBatteryMux);
  return state;
}

bool shouldEnterDeepSleep(uint32_t idleMs) {
  config::BatteryConfig cfg;
  BatteryState state;

  portENTER_CRITICAL(&gBatteryMux);
  cfg = gConfig;
  state = gState;
  portEXIT_CRITICAL(&gBatteryMux);

  if (idleMs >= cfg.normalIdleSleepMs) {
    return true;
  }

  if (state.valid && state.lowBattery && !state.charging && idleMs >= cfg.lowBatteryIdleSleepMs) {
    return true;
  }

  return false;
}

}  // namespace battery_manager
