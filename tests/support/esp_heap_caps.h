#pragma once

#include <cstddef>
#include <cstdlib>

// Capability flags — ignored in the host test environment; malloc always
// allocates from the regular heap regardless of which flags are requested.
static constexpr int MALLOC_CAP_SPIRAM = 0;
static constexpr int MALLOC_CAP_8BIT = 0;

inline void *heap_caps_malloc(size_t size, int /*caps*/) { return std::malloc(size); }
