#pragma once

#include <Arduino.h>
#include "config_types.h"

namespace oled_display {

void begin();
void renderStatus(const config::RuntimeStatus& status);

}  // namespace oled_display
