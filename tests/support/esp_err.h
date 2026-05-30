#pragma once

using esp_err_t = int;

static constexpr esp_err_t ESP_OK = 0;
static constexpr esp_err_t ESP_FAIL = -1;

inline const char *esp_err_to_name(esp_err_t error) {
  switch (error) {
    case ESP_OK:
      return "ESP_OK";
    case ESP_FAIL:
      return "ESP_FAIL";
    default:
      return "ESP_ERR_UNKNOWN";
  }
}
