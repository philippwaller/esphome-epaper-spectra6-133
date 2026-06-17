#pragma once

/**
 * @file log.h
 * @brief Minimal host-test stub for esphome/core/log.h.
 *
 * Provides only the subset of ESPHome's log macros that are used by the
 * epaper_spectra6_133 component sources.
 */

#include "esp_log.h"  // provides ESP_LOGI, ESP_LOGW, ESP_LOGE, ESP_LOGD

#define ESPHOME_LOG_LEVEL_NONE 0
#define ESPHOME_LOG_LEVEL_ERROR 1
#define ESPHOME_LOG_LEVEL_WARN 2
#define ESPHOME_LOG_LEVEL_INFO 3
#define ESPHOME_LOG_LEVEL_CONFIG 4
#define ESPHOME_LOG_LEVEL_DEBUG 5
#define ESPHOME_LOG_LEVEL_VERBOSE 6
#define ESPHOME_LOG_LEVEL_VERY_VERBOSE 7

#ifndef ESPHOME_LOG_LEVEL
#define ESPHOME_LOG_LEVEL ESPHOME_LOG_LEVEL_NONE
#endif

// ESP_LOGCONFIG is ESPHome's structured config-dump variant.
#ifndef ESP_LOGCONFIG
#define ESP_LOGCONFIG(tag, format, ...) ESP_LOGI(tag, format, ##__VA_ARGS__)
#endif

#ifndef ESP_LOGV
#define ESP_LOGV(tag, format, ...) ESP_LOGD(tag, format, ##__VA_ARGS__)
#endif

// LOG_STR stores a literal in flash (PROGMEM) on AVR; on ESP it is a no-op
// wrapper returning the string as-is.
#ifndef LOG_STR
#define LOG_STR(s) (s)
#endif

// HOT is an attribute hint for the compiler; safe to leave empty in tests.
#ifndef HOT
#define HOT
#endif
