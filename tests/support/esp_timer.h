#pragma once

#include <cstdint>

// Mutable global so individual tests can advance simulated time without
// requiring a real clock.  Each test that cares about timing should reset
// this to 0 in its SetUp / at the start of the test case.
inline int64_t g_mock_timer_us = 0;

inline int64_t esp_timer_get_time() { return g_mock_timer_us; }
