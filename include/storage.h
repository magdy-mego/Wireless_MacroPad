#pragma once

#include <Arduino.h>

#include "config_types.h"

namespace storage {

enum class SaveError : uint8_t {
  None,
  InvalidConfig,
  NotEnoughSpace,
  Io,
};

bool begin(bool factoryResetRequested = false);
bool loadConfig(config::Config* outConfig, String* err = nullptr);
bool saveConfig(const config::Config& cfg, String* err = nullptr, SaveError* saveError = nullptr);
void resetToDefault();
void factoryReset();

const config::Config& getConfig();
config::Config& mutableConfig();
bool setActiveProfile(uint8_t profileId, String* err = nullptr);

String serializeConfig(const config::Config& cfg);
bool deserializeConfig(const String& json, config::Config* cfg, String* err = nullptr);
bool validateConfig(const config::Config& cfg, String* err = nullptr);
size_t estimatedSerializedSize(const config::Config& cfg);

}  // namespace storage
