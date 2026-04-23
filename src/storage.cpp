#include "storage.h"

#include <ArduinoJson.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <memory>
#include <new>

namespace {
constexpr const char* kConfigPath = "/config.json";
constexpr const char* kTmpPath = "/config.tmp";
constexpr const char* kBakPath = "/config.bak";
constexpr const char* kPrefsNs = "macropad";
constexpr const char* kPrefsActiveProfile = "active_profile";
constexpr size_t kWriteOverheadBytes = 512;
constexpr uint8_t kMinBatterySampleCount = 5;
constexpr uint8_t kMaxBatterySampleCount = 9;
constexpr uint8_t kMinBatterySmoothingWindow = 1;
constexpr uint8_t kMaxBatterySmoothingWindow = 10;

config::Config gConfig;
Preferences gPreferences;

std::unique_ptr<config::Config> allocateConfigScratch(String* err, const char* context) {
  std::unique_ptr<config::Config> cfg(new (std::nothrow) config::Config());
  if (!cfg && err != nullptr) {
    *err = String("Out of memory while ") + context;
  }
  return cfg;
}

void loadDefaultConfig(config::Config* cfg) {
  cfg->version = config::kConfigVersion;
  cfg->profileCount = 1;
  cfg->activeProfile = 0;
  cfg->battery = config::BatteryConfig{};

  config::Profile& p = cfg->profiles[0];
  p.id = 0;
  p.name = "Default";

  for (size_t i = 0; i < config::kButtonsPerProfile; ++i) {
    p.buttons[i].id = static_cast<uint8_t>(i);
    p.buttons[i].type = config::ActionType::None;
    p.buttons[i].keyCount = 0;
    p.buttons[i].data = "";
  }

  p.buttons[0].type = config::ActionType::Combo;
  p.buttons[0].keyCount = 2;
  p.buttons[0].keys[0] = "KEY_LEFT_CTRL";
  p.buttons[0].keys[1] = "KEY_S";

  p.buttons[1].type = config::ActionType::Text;
  p.buttons[1].data = "git commit -m ";

  p.buttons[2].type = config::ActionType::Media;
  p.buttons[2].data = "KEY_MEDIA_VOLUME_UP";

  p.encoder.cw = "KEY_MEDIA_VOLUME_UP";
  p.encoder.ccw = "KEY_MEDIA_VOLUME_DOWN";
  p.encoder.press = "KEY_MEDIA_MUTE";
}

void loadActiveProfileFromNvs(config::Config* cfg) {
  const uint8_t saved = gPreferences.getUChar(kPrefsActiveProfile, cfg->activeProfile);
  if (saved < cfg->profileCount) {
    cfg->activeProfile = saved;
  }
}

bool hasEnoughSpace(size_t payloadBytes) {
  const size_t freeBytes = LittleFS.totalBytes() - LittleFS.usedBytes();
  return payloadBytes + kWriteOverheadBytes <= freeBytes;
}

bool commitAtomicConfigPayload(const String& payload, String* err) {
  File temp = LittleFS.open(kTmpPath, "w");
  if (!temp) {
    if (err != nullptr) {
      *err = "Failed to open temp file";
    }
    return false;
  }

  if (temp.print(payload) != payload.length()) {
    temp.close();
    if (err != nullptr) {
      *err = "Failed to write temp file";
    }
    return false;
  }
  temp.close();

  if (LittleFS.exists(kBakPath)) {
    LittleFS.remove(kBakPath);
  }

  if (LittleFS.exists(kConfigPath) && !LittleFS.rename(kConfigPath, kBakPath)) {
    if (err != nullptr) {
      *err = "Failed to stage old config backup";
    }
    LittleFS.remove(kTmpPath);
    return false;
  }

  if (!LittleFS.rename(kTmpPath, kConfigPath)) {
    if (LittleFS.exists(kBakPath)) {
      (void)LittleFS.rename(kBakPath, kConfigPath);
    }
    if (err != nullptr) {
      *err = "Failed to commit config file";
    }
    return false;
  }

  if (LittleFS.exists(kBakPath)) {
    LittleFS.remove(kBakPath);
  }

  return true;
}

bool loadConfigFromPath(const char* path, config::Config* outConfig, String* err) {
  if (!LittleFS.exists(path)) {
    if (err != nullptr) {
      *err = String(path) + " not found";
    }
    return false;
  }

  File f = LittleFS.open(path, "r");
  if (!f) {
    if (err != nullptr) {
      *err = String("Failed to open ") + path;
    }
    return false;
  }

  String payload = f.readString();
  f.close();

  std::unique_ptr<config::Config> parsed = allocateConfigScratch(err, "parsing config payload");
  if (!parsed) {
    return false;
  }

  if (!storage::deserializeConfig(payload, parsed.get(), err)) {
    return false;
  }

  *outConfig = *parsed;
  return true;
}

}  // namespace

namespace storage {

bool begin(bool factoryResetRequested) {
  if (!LittleFS.begin(true)) {
    return false;
  }

  if (!gPreferences.begin(kPrefsNs, false)) {
    return false;
  }

  if (factoryResetRequested) {
    factoryReset();
    return true;
  }

  String err;
  if (!loadConfig(&gConfig, &err)) {
    resetToDefault();
  }

  loadActiveProfileFromNvs(&gConfig);
  return true;
}

bool loadConfig(config::Config* outConfig, String* err) {
  if (!loadConfigFromPath(kConfigPath, outConfig, err)) {
    std::unique_ptr<config::Config> fallback =
        allocateConfigScratch(err, "loading backup config");
    if (!fallback) {
      return false;
    }

    String backupErr;
    if (loadConfigFromPath(kBakPath, fallback.get(), &backupErr)) {
      *outConfig = *fallback;
      const String payload = serializeConfig(*fallback);
      String writeErr;
      (void)commitAtomicConfigPayload(payload, &writeErr);
      return true;
    }

    loadDefaultConfig(outConfig);
    return true;
  }

  return true;
}

bool saveConfig(const config::Config& cfg, String* err, SaveError* saveError) {
  if (saveError != nullptr) {
    *saveError = SaveError::None;
  }

  String validationErr;
  if (!validateConfig(cfg, &validationErr)) {
    if (saveError != nullptr) {
      *saveError = SaveError::InvalidConfig;
    }
    if (err != nullptr) {
      *err = validationErr;
    }
    return false;
  }

  const String payload = serializeConfig(cfg);
  if (!hasEnoughSpace(payload.length())) {
    if (saveError != nullptr) {
      *saveError = SaveError::NotEnoughSpace;
    }
    if (err != nullptr) {
      *err = "Not enough LittleFS space for config write";
    }
    return false;
  }

  if (!commitAtomicConfigPayload(payload, err)) {
    if (saveError != nullptr) {
      *saveError = SaveError::Io;
    }
    return false;
  }

  gConfig = cfg;
  if (gConfig.activeProfile < gConfig.profileCount) {
    gPreferences.putUChar(kPrefsActiveProfile, gConfig.activeProfile);
  }

  return true;
}

void resetToDefault() {
  loadDefaultConfig(&gConfig);
  gPreferences.putUChar(kPrefsActiveProfile, gConfig.activeProfile);

  String err;
  SaveError saveError = SaveError::None;
  (void)saveConfig(gConfig, &err, &saveError);
}

void factoryReset() {
  (void)LittleFS.remove(kConfigPath);
  (void)LittleFS.remove(kTmpPath);
  (void)LittleFS.remove(kBakPath);
  resetToDefault();
}

const config::Config& getConfig() { return gConfig; }

config::Config& mutableConfig() { return gConfig; }

bool setActiveProfile(uint8_t profileId, String* err) {
  if (profileId >= gConfig.profileCount) {
    if (err != nullptr) {
      *err = "Invalid profile id";
    }
    return false;
  }

  gConfig.activeProfile = profileId;
  gPreferences.putUChar(kPrefsActiveProfile, profileId);
  return true;
}

String serializeConfig(const config::Config& cfg) {
  JsonDocument doc;
  doc["version"] = cfg.version;
  doc["active_profile"] = cfg.activeProfile;

  JsonObject battery = doc["battery"].to<JsonObject>();
  battery["divider_ratio"] = cfg.battery.dividerRatio;
  battery["low_battery_threshold_v"] = cfg.battery.lowBatteryThresholdV;
  battery["full_voltage_v"] = cfg.battery.fullVoltageV;
  battery["empty_voltage_v"] = cfg.battery.emptyVoltageV;
  battery["update_interval_ms"] = cfg.battery.updateIntervalMs;
  battery["sample_count"] = cfg.battery.sampleCount;
  battery["smoothing_window"] = cfg.battery.smoothingWindow;
  battery["calibration_scale"] = cfg.battery.calibrationScale;
  battery["calibration_offset_v"] = cfg.battery.calibrationOffsetV;
  battery["normal_idle_sleep_ms"] = cfg.battery.normalIdleSleepMs;
  battery["low_battery_idle_sleep_ms"] = cfg.battery.lowBatteryIdleSleepMs;

  JsonArray profiles = doc["profiles"].to<JsonArray>();
  for (uint8_t i = 0; i < cfg.profileCount; ++i) {
    const config::Profile& profile = cfg.profiles[i];
    JsonObject p = profiles.add<JsonObject>();
    p["name"] = profile.name;
    p["id"] = profile.id;

    JsonArray buttons = p["buttons"].to<JsonArray>();
    for (size_t bi = 0; bi < config::kButtonsPerProfile; ++bi) {
      const config::ButtonAction& action = profile.buttons[bi];
      JsonObject b = buttons.add<JsonObject>();
      b["id"] = action.id;
      b["type"] = config::actionTypeToString(action.type);
      if (action.type == config::ActionType::Combo && action.keyCount > 0) {
        JsonArray keys = b["keys"].to<JsonArray>();
        for (uint8_t ki = 0; ki < action.keyCount && ki < config::kMaxKeysPerCombo; ++ki) {
          keys.add(action.keys[ki]);
        }
      }
      if (!action.data.isEmpty()) {
        b["data"] = action.data;
      }
    }

    JsonObject encoder = p["encoder"].to<JsonObject>();
    encoder["cw"] = profile.encoder.cw;
    encoder["ccw"] = profile.encoder.ccw;
    encoder["press"] = profile.encoder.press;
  }

  String out;
  serializeJson(doc, out);
  return out;
}

bool deserializeConfig(const String& json, config::Config* cfg, String* err) {
  JsonDocument doc;
  DeserializationError de = deserializeJson(doc, json);
  if (de) {
    if (err != nullptr) {
      *err = String("JSON parse error: ") + de.c_str();
    }
    return false;
  }

  if (!doc["profiles"].is<JsonArray>()) {
    if (err != nullptr) {
      *err = "Missing profiles array";
    }
    return false;
  }

  std::unique_ptr<config::Config> parsed =
      allocateConfigScratch(err, "deserializing config document");
  if (!parsed) {
    return false;
  }

  parsed->version = doc["version"] | config::kConfigVersion;
  parsed->activeProfile = doc["active_profile"] | 0;
  parsed->battery = config::BatteryConfig{};

  if (doc["battery"].is<JsonObject>()) {
    JsonObject battery = doc["battery"].as<JsonObject>();
    parsed->battery.dividerRatio = battery["divider_ratio"] | parsed->battery.dividerRatio;
    parsed->battery.lowBatteryThresholdV =
        battery["low_battery_threshold_v"] | parsed->battery.lowBatteryThresholdV;
    parsed->battery.fullVoltageV = battery["full_voltage_v"] | parsed->battery.fullVoltageV;
    parsed->battery.emptyVoltageV = battery["empty_voltage_v"] | parsed->battery.emptyVoltageV;
    parsed->battery.updateIntervalMs = battery["update_interval_ms"] | parsed->battery.updateIntervalMs;
    parsed->battery.sampleCount = battery["sample_count"] | parsed->battery.sampleCount;
    parsed->battery.smoothingWindow = battery["smoothing_window"] | parsed->battery.smoothingWindow;
    parsed->battery.calibrationScale = battery["calibration_scale"] | parsed->battery.calibrationScale;
    parsed->battery.calibrationOffsetV =
        battery["calibration_offset_v"] | parsed->battery.calibrationOffsetV;
    parsed->battery.normalIdleSleepMs =
        battery["normal_idle_sleep_ms"] | parsed->battery.normalIdleSleepMs;
    parsed->battery.lowBatteryIdleSleepMs =
        battery["low_battery_idle_sleep_ms"] | parsed->battery.lowBatteryIdleSleepMs;
  }

  JsonArray profiles = doc["profiles"].as<JsonArray>();
  parsed->profileCount = min(static_cast<size_t>(profiles.size()), config::kMaxProfiles);
  for (uint8_t i = 0; i < parsed->profileCount; ++i) {
    JsonObject p = profiles[i].as<JsonObject>();
    parsed->profiles[i].name = p["name"] | String("Profile");
    parsed->profiles[i].id = p["id"] | i;

    for (size_t bi = 0; bi < config::kButtonsPerProfile; ++bi) {
      parsed->profiles[i].buttons[bi].id = static_cast<uint8_t>(bi);
      parsed->profiles[i].buttons[bi].type = config::ActionType::None;
      parsed->profiles[i].buttons[bi].keyCount = 0;
      parsed->profiles[i].buttons[bi].data = "";
    }

    if (p["buttons"].is<JsonArray>()) {
      JsonArray buttons = p["buttons"].as<JsonArray>();
      const size_t count = min(static_cast<size_t>(buttons.size()), config::kButtonsPerProfile);
      for (size_t bi = 0; bi < count; ++bi) {
        JsonObject b = buttons[bi].as<JsonObject>();
        parsed->profiles[i].buttons[bi].id = b["id"] | static_cast<uint8_t>(bi);
        parsed->profiles[i].buttons[bi].type =
            config::actionTypeFromString(String(static_cast<const char*>(b["type"] | "none")));
        parsed->profiles[i].buttons[bi].data = b["data"] | String("");

        if (b["keys"].is<JsonArray>()) {
          JsonArray keys = b["keys"].as<JsonArray>();
          const size_t keyCount = min(static_cast<size_t>(keys.size()), config::kMaxKeysPerCombo);
          parsed->profiles[i].buttons[bi].keyCount = static_cast<uint8_t>(keyCount);
          for (size_t ki = 0; ki < keyCount; ++ki) {
            parsed->profiles[i].buttons[bi].keys[ki] = keys[ki].as<String>();
          }
        }
      }
    }

    if (p["encoder"].is<JsonObject>()) {
      JsonObject enc = p["encoder"].as<JsonObject>();
      parsed->profiles[i].encoder.cw = enc["cw"] | String("");
      parsed->profiles[i].encoder.ccw = enc["ccw"] | String("");
      parsed->profiles[i].encoder.press = enc["press"] | String("");
    }
  }

  String validationErr;
  if (!validateConfig(*parsed, &validationErr)) {
    if (err != nullptr) {
      *err = validationErr;
    }
    return false;
  }

  *cfg = *parsed;
  return true;
}

bool validateConfig(const config::Config& cfg, String* err) {
  if (cfg.version != config::kConfigVersion) {
    if (err != nullptr) {
      *err = "Unsupported config version";
    }
    return false;
  }

  if (cfg.profileCount == 0 || cfg.profileCount > config::kMaxProfiles) {
    if (err != nullptr) {
      *err = "Invalid profile count";
    }
    return false;
  }

  if (cfg.activeProfile >= cfg.profileCount) {
    if (err != nullptr) {
      *err = "active_profile out of range";
    }
    return false;
  }

  if (!(cfg.battery.dividerRatio > 1.0f && cfg.battery.dividerRatio <= 12.0f)) {
    if (err != nullptr) {
      *err = "battery.divider_ratio must be > 1.0 and <= 12.0";
    }
    return false;
  }

  if (!(cfg.battery.emptyVoltageV >= 2.5f && cfg.battery.emptyVoltageV <= 3.8f)) {
    if (err != nullptr) {
      *err = "battery.empty_voltage_v out of range";
    }
    return false;
  }

  if (!(cfg.battery.fullVoltageV >= 3.7f && cfg.battery.fullVoltageV <= 4.35f)) {
    if (err != nullptr) {
      *err = "battery.full_voltage_v out of range";
    }
    return false;
  }

  if (cfg.battery.fullVoltageV <= cfg.battery.emptyVoltageV) {
    if (err != nullptr) {
      *err = "battery.full_voltage_v must be greater than battery.empty_voltage_v";
    }
    return false;
  }

  if (!(cfg.battery.lowBatteryThresholdV >= cfg.battery.emptyVoltageV &&
        cfg.battery.lowBatteryThresholdV <= cfg.battery.fullVoltageV)) {
    if (err != nullptr) {
      *err = "battery.low_battery_threshold_v must be within empty/full voltage bounds";
    }
    return false;
  }

  if (cfg.battery.updateIntervalMs < 5000 || cfg.battery.updateIntervalMs > 600000) {
    if (err != nullptr) {
      *err = "battery.update_interval_ms must be between 5000 and 600000";
    }
    return false;
  }

  if (cfg.battery.sampleCount < kMinBatterySampleCount ||
      cfg.battery.sampleCount > kMaxBatterySampleCount) {
    if (err != nullptr) {
      *err = "battery.sample_count must be between 5 and 9";
    }
    return false;
  }

  if (cfg.battery.smoothingWindow < kMinBatterySmoothingWindow ||
      cfg.battery.smoothingWindow > kMaxBatterySmoothingWindow) {
    if (err != nullptr) {
      *err = "battery.smoothing_window must be between 1 and 10";
    }
    return false;
  }

  if (!(cfg.battery.calibrationScale >= 0.8f && cfg.battery.calibrationScale <= 1.2f)) {
    if (err != nullptr) {
      *err = "battery.calibration_scale must be between 0.8 and 1.2";
    }
    return false;
  }

  if (!(cfg.battery.calibrationOffsetV >= -0.5f && cfg.battery.calibrationOffsetV <= 0.5f)) {
    if (err != nullptr) {
      *err = "battery.calibration_offset_v must be between -0.5 and 0.5";
    }
    return false;
  }

  if (cfg.battery.normalIdleSleepMs < 10000 || cfg.battery.normalIdleSleepMs > 3600000) {
    if (err != nullptr) {
      *err = "battery.normal_idle_sleep_ms must be between 10000 and 3600000";
    }
    return false;
  }

  if (cfg.battery.lowBatteryIdleSleepMs < 5000 ||
      cfg.battery.lowBatteryIdleSleepMs > cfg.battery.normalIdleSleepMs) {
    if (err != nullptr) {
      *err = "battery.low_battery_idle_sleep_ms must be between 5000 and normal_idle_sleep_ms";
    }
    return false;
  }

  for (uint8_t p = 0; p < cfg.profileCount; ++p) {
    const config::Profile& profile = cfg.profiles[p];
    if (profile.name.isEmpty()) {
      if (err != nullptr) {
        *err = "Profile name cannot be empty";
      }
      return false;
    }

    for (size_t bi = 0; bi < config::kButtonsPerProfile; ++bi) {
      const config::ButtonAction& action = profile.buttons[bi];
      if (action.type == config::ActionType::Combo && action.keyCount == 0) {
        if (err != nullptr) {
          *err = "Combo action requires at least one key";
        }
        return false;
      }
      if (action.keyCount > config::kMaxKeysPerCombo) {
        if (err != nullptr) {
          *err = "Combo key count exceeds limit";
        }
        return false;
      }
    }
  }

  return true;
}

size_t estimatedSerializedSize(const config::Config& cfg) { return serializeConfig(cfg).length(); }

}  // namespace storage
