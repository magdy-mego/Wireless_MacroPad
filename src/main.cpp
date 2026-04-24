#include <Arduino.h>
#include <memory>
#include <new>

#include "freertos/task.h"

#include "battery_manager.h"
#include "ble_handler.h"
#include "config_types.h"
#include "matrix.h"
#include "oled_display.h"
#include "pins.h"
#include "power_manager.h"
#include "storage.h"
#include "task_config.h"
#include "web_server.h"

#ifndef MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET
#define MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET 0
#endif

#ifndef MACROPAD_AP_START_ON_BOOT
#define MACROPAD_AP_START_ON_BOOT 0
#endif

extern TaskHandle_t loopTaskHandle;

namespace {
TaskHandle_t gBleTaskHandle = nullptr;
TaskHandle_t gWebTaskHandle = nullptr;
TaskHandle_t gOledTaskHandle = nullptr;
TaskHandle_t gBatteryTaskHandle = nullptr;

uint32_t gLastLedToggleMs = 0;
uint32_t gLastStackDiagMs = 0;
bool gLedState = false;
bool gEncoderPressPending = false;
bool gEncoderLongPressHandled = false;

static int gEncoderLedBrightness = 0; 
constexpr uint8_t kLedPwmChannel = 0;

constexpr uint32_t kStackDiagPeriodMs = 15000;
constexpr UBaseType_t kStackWarnWords = 256;
constexpr UBaseType_t kLoopStackWarnWords = 512;

config::RuntimeStatus buildRuntimeStatus() {
  const battery_manager::BatteryState battery = battery_manager::getState();
  config::RuntimeStatus status;
  status.batteryVoltage = battery.voltage;
  status.batteryPercent = battery.percent;
  status.charging = battery.charging;
  status.lowBattery = battery.lowBattery;
  status.bleConnected = ble_handler::isConnected();
  status.wifiClientConnected = web_server::isClientConnected();
  status.apEnabled = web_server::isApEnabled();
  status.inDeepSleep = false;

  const config::Config& cfg = storage::getConfig();
  if (cfg.profileCount > 0 && cfg.activeProfile < cfg.profileCount) {
    status.activeProfileName = cfg.profiles[cfg.activeProfile].name;
  } else {
    status.activeProfileName = "N/A";
  }
  return status;
}

String provideConfigJson() { return storage::serializeConfig(storage::getConfig()); }

bool onRemoteAction(uint8_t buttonId, String* err) {
  return ble_handler::triggerButtonAction(buttonId, storage::getConfig(), err);
}

web_server::ConfigSaveStatus onConfigSave(const String& json, String* err) {
  std::unique_ptr<config::Config> nextCfg(new (std::nothrow) config::Config());
  if (!nextCfg) {
    if (err != nullptr) {
      *err = "Out of memory while preparing config update";
    }
    return web_server::ConfigSaveStatus::InternalError;
  }

  if (!storage::deserializeConfig(json, nextCfg.get(), err)) {
    return web_server::ConfigSaveStatus::InvalidPayload;
  }

  storage::SaveError saveError = storage::SaveError::None;
  if (!storage::saveConfig(*nextCfg, err, &saveError)) {
    switch (saveError) {
      case storage::SaveError::InvalidConfig:
        return web_server::ConfigSaveStatus::InvalidPayload;
      case storage::SaveError::NotEnoughSpace:
        return web_server::ConfigSaveStatus::NotEnoughSpace;
      case storage::SaveError::Io:
      case storage::SaveError::None:
      default:
        return web_server::ConfigSaveStatus::InternalError;
    }
  }

  battery_manager::applyConfig(nextCfg->battery);
  if (battery_manager::updateNow()) {
    const battery_manager::BatteryState battery = battery_manager::getState();
    if (battery.valid) {
      ble_handler::setBatteryPercent(battery.percent);
    }
  }
  return web_server::ConfigSaveStatus::Ok;
}

bool shouldFactoryResetOnBoot() {
#if MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET
  pinMode(static_cast<uint8_t>(pins::kEncoderSw), INPUT_PULLUP);
  if (digitalRead(static_cast<uint8_t>(pins::kEncoderSw)) != LOW) {
    return false;
  }

  const uint32_t startMs = millis();
  while ((millis() - startMs) < 5000) {
    if (digitalRead(static_cast<uint8_t>(pins::kEncoderSw)) != LOW) {
      return false;
    }
    delay(10);
  }

  return true;
#else
  return false;
#endif
}

void logTaskWatermark(const char* taskName, TaskHandle_t handle, UBaseType_t warnWords) {
  if (handle == nullptr) {
    return;
  }

  const UBaseType_t highWaterWords = uxTaskGetStackHighWaterMark(handle);
  const uint32_t highWaterBytes = static_cast<uint32_t>(highWaterWords * sizeof(StackType_t));
  if (highWaterWords <= warnWords) {
    Serial.printf("[STACK][WARN] %s low free stack: %u words (%lu bytes)\n", taskName,
                  static_cast<unsigned>(highWaterWords), static_cast<unsigned long>(highWaterBytes));
    return;
  }

  Serial.printf("[STACK] %s free stack high-water: %u words (%lu bytes)\n", taskName,
                static_cast<unsigned>(highWaterWords), static_cast<unsigned long>(highWaterBytes));
}

void logStackDiagnosticsIfDue() {
  const uint32_t now = millis();
  if ((now - gLastStackDiagMs) < kStackDiagPeriodMs) {
    return;
  }
  gLastStackDiagMs = now;

  logTaskWatermark("loopTask", loopTaskHandle, kLoopStackWarnWords);
  logTaskWatermark("BLE_HID_Task", gBleTaskHandle, kStackWarnWords);
  logTaskWatermark("WebServer_Task", gWebTaskHandle, kStackWarnWords);
  logTaskWatermark("OLED_Task", gOledTaskHandle, kStackWarnWords);
  logTaskWatermark("Battery_Task", gBatteryTaskHandle, kStackWarnWords);
}

void cycleActiveProfile() {
  config::Config& cfg = storage::mutableConfig();
  if (cfg.profileCount == 0) {
    return;
  }

  const uint8_t nextProfile = static_cast<uint8_t>((cfg.activeProfile + 1) % cfg.profileCount);
  String err;
  if (!storage::setActiveProfile(nextProfile, &err)) {
    Serial.printf("[PROFILE] Failed to switch profile: %s\n", err.c_str());
    return;
  }

  Serial.printf("[PROFILE] Active profile -> %s\n", cfg.profiles[cfg.activeProfile].name.c_str());
}

void updateStatusLed() {
  const uint32_t now = millis();
  
  if (ble_handler::isConnected()) {
    // ledcWrite(kLedPwmChannel, 255); 
    gLedState = true;
    return;
  }

 
  if ((now - gLastLedToggleMs) >= 500) {
    gLedState = !gLedState;
    gLastLedToggleMs = now;
    ledcWrite(kLedPwmChannel, gLedState ? 128 : 0); 
  }
}

void handleInputEvent(const matrix::InputEvent& event) {
  switch (event.type) {
    case matrix::EventType::EncoderPressed:
      gEncoderPressPending = true;
      gEncoderLongPressHandled = false;
      break;

    case matrix::EventType::EncoderLongPress:
      web_server::enableAp();
      gEncoderLongPressHandled = true;
      Serial.println("[WEB] AP re-enabled via encoder hold");
      break;

    case matrix::EventType::EncoderReleased:
      if (gEncoderPressPending && !gEncoderLongPressHandled) {
        cycleActiveProfile();
      }
      gEncoderPressPending = false;
      gEncoderLongPressHandled = false;
      break;

   
    case matrix::EventType::EncoderClockwise:
      gEncoderLedBrightness += 25; 
      if (gEncoderLedBrightness > 255) gEncoderLedBrightness = 255;
      ledcWrite(kLedPwmChannel, gEncoderLedBrightness);
      break;

    case matrix::EventType::EncoderCounterClockwise:
      gEncoderLedBrightness -= 25; 
      if (gEncoderLedBrightness < 0) gEncoderLedBrightness = 0;
      ledcWrite(kLedPwmChannel, gEncoderLedBrightness);
      break;

    default:
      break;
  }

  ble_handler::dispatchInputEvent(event, storage::getConfig());
}

void bleTask(void* /*param*/) {
  for (;;) {
    matrix::scan();

    matrix::InputEvent event;
    while (matrix::pollEvent(&event, 0)) {
      if (!web_server::isApEnabled()) {
         handleInputEvent(event);
      } else {
         if (event.type == matrix::EventType::EncoderPressed) {
            gEncoderPressPending = true;
            gEncoderLongPressHandled = false;
         } else if (event.type == matrix::EventType::EncoderReleased && gEncoderPressPending && !gEncoderLongPressHandled) {
             cycleActiveProfile();
             gEncoderPressPending = false;
             gEncoderLongPressHandled = false;
         }
      }
    }

    if (!web_server::isApEnabled()) {
        ble_handler::updateConnectionState();
        updateStatusLed();
    }
    
    vTaskDelay(pdMS_TO_TICKS(taskcfg::kBleScanPeriodMs));
  }
}

void webTask(void* /*param*/) {
  for (;;) {
    web_server::loop();
    vTaskDelay(pdMS_TO_TICKS(50));
  }
}

void oledTask(void* /*param*/) {
  for (;;) {
    oled_display::renderStatus(buildRuntimeStatus());
    vTaskDelay(pdMS_TO_TICKS(taskcfg::kOledUpdatePeriodMs));
  }
}

void batteryTask(void* /*param*/) {
  for (;;) {
    logStackDiagnosticsIfDue();

    if (battery_manager::updateIfDue()) {
      const battery_manager::BatteryState state = battery_manager::getState();
      if (state.valid && !web_server::isApEnabled()) {
        ble_handler::setBatteryPercent(state.percent);
      }
    }

    const uint32_t idleMs = matrix::idleDurationMs();
    if (!web_server::isApEnabled() && battery_manager::shouldEnterDeepSleep(idleMs)) {
      delay(50);
      power_manager::enterDeepSleep();
    }

    vTaskDelay(pdMS_TO_TICKS(taskcfg::kBatteryCheckPeriodMs));
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);
  delay(100);

  Serial.println(
      "[BOOT] NOTE: Encoder SW on GPIO35 needs an external pull-up resistor for reliable input.");
#if !MACROPAD_ENABLE_BOOT_HOLD_FACTORY_RESET
  Serial.println("[BOOT] Boot-hold factory reset is disabled by policy.");
#endif

  // digitalWrite
  ledcSetup(kLedPwmChannel, 5000, 8); 
  ledcAttachPin(static_cast<uint8_t>(pins::kStatusLed), kLedPwmChannel);
  ledcWrite(kLedPwmChannel, 0); 

  const bool factoryResetRequested = shouldFactoryResetOnBoot();
  if (factoryResetRequested) {
    Serial.println("[BOOT] Factory reset requested (encoder held for 5s)");
  }

  if (!storage::begin(factoryResetRequested)) {
    Serial.println("[BOOT] Failed to initialize storage");
  }

  matrix::begin();
  battery_manager::begin(storage::getConfig().battery);
  (void)battery_manager::updateNow();
  ble_handler::begin("Macropad");
  const battery_manager::BatteryState battery = battery_manager::getState();
  if (battery.valid) {
    ble_handler::setBatteryPercent(battery.percent);
  }
  oled_display::begin();

  web_server::setStatusProvider(buildRuntimeStatus);
  web_server::setConfigProvider(provideConfigJson);
  web_server::setRemoteActionCallback(onRemoteAction);
  web_server::setConfigSaveCallback(onConfigSave);
  web_server::begin(MACROPAD_AP_START_ON_BOOT);

  xTaskCreatePinnedToCore(bleTask, "BLE_HID_Task", taskcfg::kTaskStackWords, nullptr,
                          taskcfg::kBleTaskPriority, &gBleTaskHandle, 1);
  xTaskCreatePinnedToCore(webTask, "WebServer_Task", taskcfg::kTaskStackWords, nullptr,
                          taskcfg::kWebTaskPriority, &gWebTaskHandle, 0);
  xTaskCreatePinnedToCore(oledTask, "OLED_Task", taskcfg::kTaskStackWords, nullptr,
                          taskcfg::kOledTaskPriority, &gOledTaskHandle, 1);
  xTaskCreatePinnedToCore(batteryTask, "Battery_Task", taskcfg::kTaskStackWords, nullptr,
                          taskcfg::kBatteryTaskPriority, &gBatteryTaskHandle, 1);
}

void loop() { vTaskDelay(pdMS_TO_TICKS(1000)); }