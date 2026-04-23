#pragma once

#include <Arduino.h>

namespace pins {
constexpr gpio_num_t kRow0 = GPIO_NUM_13;
constexpr gpio_num_t kRow1 = GPIO_NUM_12;
constexpr gpio_num_t kRow2 = GPIO_NUM_14;

constexpr gpio_num_t kCol0 = GPIO_NUM_27;
constexpr gpio_num_t kCol1 = GPIO_NUM_26;
constexpr gpio_num_t kCol2 = GPIO_NUM_25;

constexpr gpio_num_t kEncoderClk = GPIO_NUM_33;
constexpr gpio_num_t kEncoderDt = GPIO_NUM_32;
constexpr gpio_num_t kEncoderSw = GPIO_NUM_35;

constexpr gpio_num_t kOledSda = GPIO_NUM_21;
constexpr gpio_num_t kOledScl = GPIO_NUM_22;

constexpr gpio_num_t kStatusLed = GPIO_NUM_2;
constexpr gpio_num_t kBatteryAdc = GPIO_NUM_34;
}  // namespace pins
