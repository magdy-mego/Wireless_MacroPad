#include "matrix.h"

#include <array>

#include "freertos/queue.h"
#include "pins.h"

namespace {
constexpr uint8_t kRows = 3;
constexpr uint8_t kCols = 3;
constexpr uint8_t kQueueDepth = 32;
constexpr uint32_t kDebounceMs = 10;
constexpr uint32_t kEncoderLongPressMs = 3000;

constexpr std::array<gpio_num_t, kRows> kRowPins = {pins::kRow0, pins::kRow1, pins::kRow2};
constexpr std::array<gpio_num_t, kCols> kColPins = {pins::kCol0, pins::kCol1, pins::kCol2};

struct DebounceState {
  bool raw = false;
  bool stable = false;
  uint32_t changedAtMs = 0;
};

QueueHandle_t gEventQueue = nullptr;
std::array<DebounceState, kRows * kCols> gKeyState{};
DebounceState gEncoderButton;

uint8_t gLastEncoderState = 0;
int8_t gEncoderAccum = 0;
uint32_t gLastEncoderEventMs = 0;

uint32_t gEncoderPressStartMs = 0;
bool gEncoderLongPressFired = false;

uint32_t gLastActivityMs = 0;

void emitEvent(matrix::EventType type, uint8_t index) {
  if (gEventQueue == nullptr) {
    return;
  }

  matrix::InputEvent event{type, index, millis()};
  gLastActivityMs = event.timestampMs;
  (void)xQueueSend(gEventQueue, &event, 0);
}

int8_t transitionDelta(uint8_t previous, uint8_t current) {
  // Quadrature transitions for KY-040: clockwise and counterclockwise states.
  if ((previous == 0b00 && current == 0b01) || (previous == 0b01 && current == 0b11) ||
      (previous == 0b11 && current == 0b10) || (previous == 0b10 && current == 0b00)) {
    return 1;
  }

  if ((previous == 0b00 && current == 0b10) || (previous == 0b10 && current == 0b11) ||
      (previous == 0b11 && current == 0b01) || (previous == 0b01 && current == 0b00)) {
    return -1;
  }

  return 0;
}

}  // namespace

namespace matrix {

void begin() {
  for (gpio_num_t pin : kRowPins) {
    pinMode(static_cast<uint8_t>(pin), OUTPUT);
    digitalWrite(static_cast<uint8_t>(pin), HIGH);
  }

  for (gpio_num_t pin : kColPins) {
    pinMode(static_cast<uint8_t>(pin), INPUT_PULLUP);
  }

  pinMode(static_cast<uint8_t>(pins::kEncoderClk), INPUT_PULLUP);
  pinMode(static_cast<uint8_t>(pins::kEncoderDt), INPUT_PULLUP);
  pinMode(static_cast<uint8_t>(pins::kEncoderSw), INPUT_PULLUP);

  gEventQueue = xQueueCreate(kQueueDepth, sizeof(InputEvent));
  gLastActivityMs = millis();

  gLastEncoderState = static_cast<uint8_t>((digitalRead(static_cast<uint8_t>(pins::kEncoderClk)) << 1) |
                                           digitalRead(static_cast<uint8_t>(pins::kEncoderDt)));
}

void scan() {
  const uint32_t now = millis();

  // Matrix scan
  for (uint8_t row = 0; row < kRows; ++row) {
    for (uint8_t i = 0; i < kRows; ++i) {
      digitalWrite(static_cast<uint8_t>(kRowPins[i]), HIGH);
    }
    digitalWrite(static_cast<uint8_t>(kRowPins[row]), LOW);
    delayMicroseconds(3);

    for (uint8_t col = 0; col < kCols; ++col) {
      const uint8_t index = static_cast<uint8_t>(row * kCols + col);
      const bool pressed = digitalRead(static_cast<uint8_t>(kColPins[col])) == LOW;
      DebounceState& state = gKeyState[index];

      if (pressed != state.raw) {
        state.raw = pressed;
        state.changedAtMs = now;
      }

      if (state.stable != state.raw && (now - state.changedAtMs) >= kDebounceMs) {
        state.stable = state.raw;
        emitEvent(state.stable ? EventType::KeyPressed : EventType::KeyReleased, index);
      }
    }
  }

  for (gpio_num_t pin : kRowPins) {
    digitalWrite(static_cast<uint8_t>(pin), HIGH);
  }

  // Encoder rotation
  const uint8_t currentEncoderState = static_cast<uint8_t>(
      (digitalRead(static_cast<uint8_t>(pins::kEncoderClk)) << 1) |
      digitalRead(static_cast<uint8_t>(pins::kEncoderDt)));

  if (currentEncoderState != gLastEncoderState) {
    const int8_t delta = transitionDelta(gLastEncoderState, currentEncoderState);
    gLastEncoderState = currentEncoderState;

    if (delta != 0) {
      gEncoderAccum += delta;
      if (abs(gEncoderAccum) >= 4 && (now - gLastEncoderEventMs) >= kDebounceMs) {
        if (gEncoderAccum > 0) {
          emitEvent(EventType::EncoderClockwise, 0);
        } else {
          emitEvent(EventType::EncoderCounterClockwise, 0);
        }
        gEncoderAccum = 0;
        gLastEncoderEventMs = now;
      }
    }
  }

  // Encoder button (short + long press)
  const bool encoderPressed = digitalRead(static_cast<uint8_t>(pins::kEncoderSw)) == LOW;
  if (encoderPressed != gEncoderButton.raw) {
    gEncoderButton.raw = encoderPressed;
    gEncoderButton.changedAtMs = now;
  }

  if (gEncoderButton.stable != gEncoderButton.raw && (now - gEncoderButton.changedAtMs) >= kDebounceMs) {
    gEncoderButton.stable = gEncoderButton.raw;
    if (gEncoderButton.stable) {
      gEncoderPressStartMs = now;
      gEncoderLongPressFired = false;
      emitEvent(EventType::EncoderPressed, 0);
    } else {
      emitEvent(EventType::EncoderReleased, 0);
      gEncoderPressStartMs = 0;
      gEncoderLongPressFired = false;
    }
  }

  if (gEncoderButton.stable && !gEncoderLongPressFired && gEncoderPressStartMs > 0 &&
      (now - gEncoderPressStartMs) >= kEncoderLongPressMs) {
    emitEvent(EventType::EncoderLongPress, 0);
    gEncoderLongPressFired = true;
  }
}

bool pollEvent(InputEvent* event, TickType_t waitTicks) {
  if (event == nullptr || gEventQueue == nullptr) {
    return false;
  }
  return xQueueReceive(gEventQueue, event, waitTicks) == pdTRUE;
}

bool isIdle(uint32_t idleThresholdMs) { return idleDurationMs() >= idleThresholdMs; }

uint32_t idleDurationMs() { return millis() - gLastActivityMs; }

}  // namespace matrix
