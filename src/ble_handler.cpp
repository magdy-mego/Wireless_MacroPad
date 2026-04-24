#include "ble_handler.h"

#include <BleKeyboard.h>
#include <BLEDevice.h>
#include <BLESecurity.h>
#include <string>

#ifndef MACROPAD_BLE_STRICT_AUTH
#define MACROPAD_BLE_STRICT_AUTH 0
#endif

namespace {
BleKeyboard gBleKeyboard("Macropad", "GSD", 100);
bool gBleBeginDone = false;
bool gConnected = false;
bool gBatteryValid = false;
bool gBatterySyncPending = false;
uint8_t gBatteryPercent = 100;
uint8_t gLastPublishedBatteryPercent = 100;

void applyBleSecurityPolicy() {
  BLESecurity security;
#if MACROPAD_BLE_STRICT_AUTH
  security.setAuthenticationMode(ESP_LE_AUTH_REQ_SC_MITM_BOND);
#else
  security.setAuthenticationMode(ESP_LE_AUTH_BOND);
  security.setCapability(ESP_IO_CAP_NONE);
#endif
  security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
  security.setKeySize(16);
}

struct NamedKey {
  const char* name;
  uint8_t code;
};

constexpr NamedKey kNamedKeys[] = {
    {"KEY_LEFT_CTRL", KEY_LEFT_CTRL},     {"KEY_LEFT_SHIFT", KEY_LEFT_SHIFT},
    {"KEY_LEFT_ALT", KEY_LEFT_ALT},       {"KEY_LEFT_GUI", KEY_LEFT_GUI},
    {"KEY_RIGHT_CTRL", KEY_RIGHT_CTRL},   {"KEY_RIGHT_SHIFT", KEY_RIGHT_SHIFT},
    {"KEY_RIGHT_ALT", KEY_RIGHT_ALT},     {"KEY_RIGHT_GUI", KEY_RIGHT_GUI},
    {"KEY_TAB", KEY_TAB},                 {"KEY_RETURN", KEY_RETURN},
    {"KEY_ESC", KEY_ESC},                 {"KEY_BACKSPACE", KEY_BACKSPACE},
    {"KEY_DELETE", KEY_DELETE},           {"KEY_UP_ARROW", KEY_UP_ARROW},
    {"KEY_DOWN_ARROW", KEY_DOWN_ARROW},   {"KEY_LEFT_ARROW", KEY_LEFT_ARROW},
    {"KEY_RIGHT_ARROW", KEY_RIGHT_ARROW}, {"KEY_PAGE_UP", KEY_PAGE_UP},
    {"KEY_PAGE_DOWN", KEY_PAGE_DOWN},     {"KEY_HOME", KEY_HOME},
    {"KEY_END", KEY_END},
};

bool resolveKeyboardKey(const String& token, uint8_t* out) {
  if (out == nullptr) {
    return false;
  }

  for (const auto& item : kNamedKeys) {
    if (token == item.name) {
      *out = item.code;
      return true;
    }
  }

  if (!token.startsWith("KEY_")) {
    if (token.length() == 1) {
      *out = static_cast<uint8_t>(token[0]);
      return true;
    }
    return false;
  }

  const String suffix = token.substring(4);
  if (suffix.length() == 1) {
    *out = static_cast<uint8_t>(suffix[0]);
    return true;
  }

  if (suffix == "SPACE") {
    *out = static_cast<uint8_t>(' ');
    return true;
  }

  if (suffix == "ENTER") {
    *out = KEY_RETURN;
    return true;
  }

  if (suffix == "MINUS") {
    *out = static_cast<uint8_t>('-');
    return true;
  }

  if (suffix == "EQUAL") {
    *out = static_cast<uint8_t>('=');
    return true;
  }

  return false;
}

bool sendMediaKey(const String& token) {
  if (token == "KEY_MEDIA_VOLUME_UP") {
    gBleKeyboard.write(KEY_MEDIA_VOLUME_UP);
    return true;
  }
  if (token == "KEY_MEDIA_VOLUME_DOWN") {
    gBleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
    return true;
  }
  if (token == "KEY_MEDIA_MUTE") {
    gBleKeyboard.write(KEY_MEDIA_MUTE);
    return true;
  }
  if (token == "KEY_MEDIA_PLAY_PAUSE") {
    gBleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
    return true;
  }
  if (token == "KEY_MEDIA_NEXT_TRACK") {
    gBleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
    return true;
  }
  if (token == "KEY_MEDIA_PREVIOUS_TRACK") {
    gBleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
    return true;
  }

  return false;
}

bool executeActionToken(const String& token, String* err) {
  if (!gBleKeyboard.isConnected()) {
    if (err != nullptr) {
      *err = "BLE not connected";
    }
    return false;
  }

  if (sendMediaKey(token)) {
    return true;
  }

  if (token == "MOUSE_SCROLL_UP" || token == "MOUSE_WHEEL_UP") {
    gBleKeyboard.write(KEY_PAGE_UP);
    return true;
  }

  if (token == "MOUSE_SCROLL_DOWN" || token == "MOUSE_WHEEL_DOWN") {
    gBleKeyboard.write(KEY_PAGE_DOWN);
    return true;
  }

  uint8_t key = 0;
  if (resolveKeyboardKey(token, &key)) {
    gBleKeyboard.write(key);
    return true;
  }

  if (err != nullptr) {
    *err = String("Unsupported token: ") + token;
  }
  return false;
}

// ==========================================
// (Sequence Parser Engine)
// ==========================================
void executeSequence(const String& script) {
    if (!gBleKeyboard.isConnected()) return;

    int startIndex = 0;
    int endIndex = script.indexOf(';');

    while (startIndex < script.length()) {
        String cmd = "";
        if (endIndex == -1) {
            cmd = script.substring(startIndex);
            startIndex = script.length();
        } else {
            cmd = script.substring(startIndex, endIndex);
            startIndex = endIndex + 1;
            endIndex = script.indexOf(';', startIndex);
        }

        cmd.trim(); 
        if (cmd.length() == 0) continue;

        if (cmd.startsWith("DELAY ")) {
            int delayTime = cmd.substring(6).toInt(); 
            if (delayTime > 0) delay(delayTime);
        } 
        else if (cmd.startsWith("TEXT ")) {
            String textToPrint = cmd.substring(5);
            gBleKeyboard.print(textToPrint);
            delay(50);
        } 
        else if (cmd == "ENTER") {
            gBleKeyboard.write(KEY_RETURN);
            delay(50);
        } 
        else if (cmd == "WIN+R") {
            gBleKeyboard.press(KEY_LEFT_GUI);
            gBleKeyboard.press('r');
            delay(50);
            gBleKeyboard.releaseAll();
            delay(100); 
        }
        else if (cmd == "ESC") {
            gBleKeyboard.write(KEY_ESC);
            delay(50);
        }
        else if (cmd == "TAB") {
            gBleKeyboard.write(KEY_TAB);
            delay(50);
        }
    }
}

bool executeButtonAction(const config::ButtonAction& action, String* err) {
  switch (action.type) {
    case config::ActionType::Combo: {
      if (!gBleKeyboard.isConnected()) {
        if (err != nullptr) {
          *err = "BLE not connected";
        }
        return false;
      }

      for (uint8_t i = 0; i < action.keyCount; ++i) {
        uint8_t keyCode = 0;
        if (!resolveKeyboardKey(action.keys[i], &keyCode)) {
          if (err != nullptr) {
            *err = String("Invalid combo key: ") + action.keys[i];
          }
          gBleKeyboard.releaseAll();
          return false;
        }
        gBleKeyboard.press(keyCode);
      }

      delay(5);
      gBleKeyboard.releaseAll();
      return true;
    }

    case config::ActionType::Text:
      if (!gBleKeyboard.isConnected()) {
        if (err != nullptr) {
          *err = "BLE not connected";
        }
        return false;
      }
      gBleKeyboard.print(action.data);
      return true;

    case config::ActionType::Sequence:
      if (!gBleKeyboard.isConnected()) {
        if (err != nullptr) {
          *err = "BLE not connected";
        }
        return false;
      }
      Serial.printf("[SEQ] Executing: %s\n", action.data.c_str());
      executeSequence(action.data);
      return true;

    case config::ActionType::Media:
      return executeActionToken(action.data, err);

    case config::ActionType::Mouse:
      return executeActionToken(action.data, err);

    case config::ActionType::LayerSwitch:
      if (err != nullptr) {
        *err = "Layer switch is handled by the app controller";
      }
      return false;

    case config::ActionType::None:
    default:
      return true;
  }
}

const config::Profile* activeProfile(const config::Config& cfg) {
  if (cfg.profileCount == 0 || cfg.activeProfile >= cfg.profileCount) {
    return nullptr;
  }
  return &cfg.profiles[cfg.activeProfile];
}

void publishBatteryPercent(bool force) {
  if (!gBatteryValid) {
    return;
  }

  if (!gBleKeyboard.isConnected()) {
    gBatterySyncPending = true;
    return;
  }

  if (!force && gBatteryPercent == gLastPublishedBatteryPercent) {
    return;
  }

  gBleKeyboard.setBatteryLevel(gBatteryPercent);
  gLastPublishedBatteryPercent = gBatteryPercent;
  gBatterySyncPending = false;
  Serial.printf("[BLE] BAS notify: %u%%\n", gBatteryPercent);
}

}  // namespace

namespace ble_handler {

void begin(const char* deviceName) {
  if (gBleBeginDone) {
    return;
  }

  if (deviceName != nullptr && deviceName[0] != '\0') {
    gBleKeyboard.setName(std::string(deviceName));
  }

  gBleKeyboard.begin();
  applyBleSecurityPolicy();

  gConnected = false;
  gBatterySyncPending = true;
  gBleBeginDone = true;
}

void updateConnectionState() {
  const bool nowConnected = gBleKeyboard.isConnected();
  if (nowConnected != gConnected) {
    gConnected = nowConnected;
    if (gConnected) {
      Serial.println("[BLE] Connected");
      gBatterySyncPending = true;
    } else {
      Serial.println("[BLE] Disconnected (waiting for reconnect)");
      gBatterySyncPending = true;
    }
  }

  if (gBatterySyncPending && gBleKeyboard.isConnected()) {
    publishBatteryPercent(true);
  }
}

bool isConnected() { return gConnected; }

void setBatteryPercent(uint8_t percent) {
  if (percent > 100) {
    percent = 100;
  }

  const bool changed = !gBatteryValid || gBatteryPercent != percent;
  gBatteryPercent = percent;
  gBatteryValid = true;

  if (!changed) {
    return;
  }

  publishBatteryPercent(false);
}

void dispatchInputEvent(const matrix::InputEvent& event, const config::Config& cfg) {
  const config::Profile* profile = activeProfile(cfg);
  if (profile == nullptr) {
    return;
  }

  String err;

  switch (event.type) {
    case matrix::EventType::KeyPressed:
      if (event.index < config::kButtonsPerProfile) {
        (void)executeButtonAction(profile->buttons[event.index], &err);
      }
      break;

    case matrix::EventType::EncoderClockwise:
      (void)executeActionToken(profile->encoder.cw, &err);
      break;

    case matrix::EventType::EncoderCounterClockwise:
      (void)executeActionToken(profile->encoder.ccw, &err);
      break;

    case matrix::EventType::EncoderPressed:
      // Short-press is reserved for profile cycle by application controller.
      break;

    case matrix::EventType::EncoderReleased:
    case matrix::EventType::EncoderLongPress:
    case matrix::EventType::KeyReleased:
    default:
      break;
  }
}

bool triggerButtonAction(uint8_t buttonId, const config::Config& cfg, String* err) {
  const config::Profile* profile = activeProfile(cfg);
  if (profile == nullptr) {
    if (err != nullptr) {
      *err = "No active profile";
    }
    return false;
  }
  if (buttonId >= config::kButtonsPerProfile) {
    if (err != nullptr) {
      *err = "Button id out of range";
    }
    return false;
  }
  return executeButtonAction(profile->buttons[buttonId], err);
}

}  // namespace ble_handler