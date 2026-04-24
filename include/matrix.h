#pragma once

#include <Arduino.h>

#include "freertos/FreeRTOS.h"

namespace matrix {

// Input event kinds produced by matrix and encoder scanner.
enum class EventType : uint8_t {
  KeyPressed,
  KeyReleased,
  EncoderClockwise,
  EncoderCounterClockwise,
  EncoderPressed,
  EncoderReleased,
  EncoderLongPress,
};

// Single event packet delivered over FreeRTOS queue.
struct InputEvent {
  EventType type;
  uint8_t index;
  uint32_t timestampMs;
};

// Initialize GPIO and queue infrastructure.
void begin();

// Perform one scan iteration and enqueue generated events.
void scan();

// Poll one input event from queue.
bool pollEvent(InputEvent* event, TickType_t waitTicks = 0);

// Check whether device has been idle longer than threshold.
bool isIdle(uint32_t idleThresholdMs);

// Return milliseconds since last input event.
uint32_t idleDurationMs();

}  // namespace matrix
