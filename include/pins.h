#pragma once

#include <Arduino.h>

namespace pins {

// Matrix rows used by 2x3 firmware layout.
constexpr gpio_num_t kRow0 = GPIO_NUM_13;
constexpr gpio_num_t kRow1 = GPIO_NUM_12;

// Legacy third row pin kept reserved for hardware compatibility.
constexpr gpio_num_t kRow2 = GPIO_NUM_14;

// Matrix columns.
constexpr gpio_num_t kCol0 = GPIO_NUM_27;
constexpr gpio_num_t kCol1 = GPIO_NUM_26;
constexpr gpio_num_t kCol2 = GPIO_NUM_25;

// Rotary encoder signals.
constexpr gpio_num_t kEncoderClk = GPIO_NUM_33;
constexpr gpio_num_t kEncoderDt = GPIO_NUM_32;
constexpr gpio_num_t kEncoderSw = GPIO_NUM_35;

// OLED I2C bus pins.
constexpr gpio_num_t kOledSda = GPIO_NUM_21;
constexpr gpio_num_t kOledScl = GPIO_NUM_22;

// Status LED and battery ADC input.
constexpr gpio_num_t kStatusLed = GPIO_NUM_2;
constexpr gpio_num_t kBatteryAdc = GPIO_NUM_34;

}  // namespace pins
