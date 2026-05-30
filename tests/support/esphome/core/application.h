#pragma once

namespace esphome {

struct TestApplication {
  int feed_wdt_calls{0};

  void feed_wdt() { ++this->feed_wdt_calls; }
};

inline TestApplication App;

namespace setup_priority {
static constexpr float HARDWARE = 250.0f;
}  // namespace setup_priority

}  // namespace esphome
