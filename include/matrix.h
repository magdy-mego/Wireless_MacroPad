#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"

namespace matrix {

enum class EventType : uint8_t {
  KeyPressed,
  KeyReleased,
  EncoderClockwise,
  EncoderCounterClockwise,
  EncoderPressed,
  EncoderReleased,
  EncoderLongPress,
};

struct InputEvent {
  EventType type;
  uint8_t index;
  uint32_t timestampMs;
};

void begin();
void scan();
bool pollEvent(InputEvent* event, TickType_t waitTicks = 0);
bool isIdle(uint32_t idleThresholdMs);
uint32_t idleDurationMs();

}  // namespace matrix
