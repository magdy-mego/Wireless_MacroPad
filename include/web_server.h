#pragma once

#include <Arduino.h>

#include "config_types.h"

namespace web_server {

// Outcome mapping used by POST /api/config callback.
enum class ConfigSaveStatus : uint8_t {
  Ok,
  InvalidPayload,
  NotEnoughSpace,
  InternalError,
};

// Initialize route handlers and optionally request AP on boot.
void begin(bool enableApOnBoot = false);

// Run AP/captive portal state machine.
void loop();

// Inject callbacks provided by main application layer.
void setStatusProvider(config::RuntimeStatus (*provider)());
void setConfigProvider(String (*provider)());
void setRemoteActionCallback(bool (*cb)(uint8_t, String*));
void setConfigSaveCallback(ConfigSaveStatus (*cb)(const String&, String*));

// AP status/controls used by other tasks.
bool isApEnabled();
bool isClientConnected();
void enableAp();
void disableAp();

}  // namespace web_server
