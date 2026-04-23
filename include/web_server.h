#pragma once

#include <Arduino.h>
#include "config_types.h"

namespace web_server {

enum class ConfigSaveStatus : uint8_t {
  Ok,
  InvalidPayload,
  NotEnoughSpace,
  InternalError,
};

void begin(bool enableApOnBoot = false);
void loop();

// تعريفات الدوال فقط (بدون الأقواس {})
void setStatusProvider(config::RuntimeStatus (*provider)());
void setConfigProvider(String (*provider)());
void setRemoteActionCallback(bool (*cb)(uint8_t, String*));
void setConfigSaveCallback(ConfigSaveStatus (*cb)(const String&, String*));

bool isApEnabled();
bool isClientConnected();
void enableAp();
void disableAp();

}  // namespace web_server