#pragma once

#include <Arduino.h>

#include "config_types.h"
#include "matrix.h"

namespace ble_handler {

void begin(const char* deviceName);
void updateConnectionState();
bool isConnected();
void setBatteryPercent(uint8_t percent);
void dispatchInputEvent(const matrix::InputEvent& event, const config::Config& cfg);
bool triggerButtonAction(uint8_t buttonId, const config::Config& cfg, String* err = nullptr);

}  // namespace ble_handler
