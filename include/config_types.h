#pragma once

#include <Arduino.h>
#include <array>

namespace config {

constexpr uint8_t kConfigVersion = 1;
constexpr size_t kButtonsPerProfile = 9;
constexpr size_t kMaxProfiles = 8;
constexpr size_t kMaxKeysPerCombo = 6;

enum class ActionType : uint8_t {
  Combo,
  Text,
  Media,
  Mouse,
  LayerSwitch,
  None,
};

struct ButtonAction {
  uint8_t id = 0;
  ActionType type = ActionType::None;
  std::array<String, kMaxKeysPerCombo> keys{};
  uint8_t keyCount = 0;
  String data;
};

struct EncoderAction {
  String cw;
  String ccw;
  String press;
};

struct Profile {
  String name;
  uint8_t id = 0;
  std::array<ButtonAction, kButtonsPerProfile> buttons{};
  EncoderAction encoder;
};

struct BatteryConfig {
  float dividerRatio = 2.0f;
  float lowBatteryThresholdV = 3.30f;
  float fullVoltageV = 4.20f;
  float emptyVoltageV = 3.20f;
  uint32_t updateIntervalMs = 30000;
  uint8_t sampleCount = 7;
  uint8_t smoothingWindow = 5;
  float calibrationScale = 1.0f;
  float calibrationOffsetV = 0.0f;
  uint32_t normalIdleSleepMs = 60000;
  uint32_t lowBatteryIdleSleepMs = 15000;
};

struct Config {
  uint8_t version = kConfigVersion;
  std::array<Profile, kMaxProfiles> profiles{};
  uint8_t profileCount = 0;
  uint8_t activeProfile = 0;
  BatteryConfig battery;
};

struct RuntimeStatus {
  float batteryVoltage = 0.0f;
  uint8_t batteryPercent = 0;
  bool charging = false;
  bool lowBattery = false;
  bool bleConnected = false;
  bool wifiClientConnected = false;
  bool apEnabled = true;
  bool inDeepSleep = false;
  String activeProfileName;
};

const char* actionTypeToString(ActionType type);
ActionType actionTypeFromString(const String& value);

}  // namespace config
