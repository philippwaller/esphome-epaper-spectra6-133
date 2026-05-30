#pragma once

#include <cstdint>

namespace esphome {

struct Color {
  uint8_t red;
  uint8_t green;
  uint8_t blue;

  constexpr Color(uint8_t red = 0, uint8_t green = 0, uint8_t blue = 0) : red(red), green(green), blue(blue) {}
};

}  // namespace esphome
