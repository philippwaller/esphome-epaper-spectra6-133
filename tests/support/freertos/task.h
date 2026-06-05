#pragma once

#include <cstdint>

inline void vTaskDelay(uint32_t) {}
#define taskYIELD() do {} while (0)
