#pragma once

/**
 * @file display_buffer.h
 * @brief Minimal host-test stub for esphome::display::DisplayBuffer.
 *
 * Provides only the subset of the real ESPHome DisplayBuffer API that
 * EpaperSpectra6133 actually uses.  All virtual methods have no-op
 * implementations so the host-test executables link without requiring
 * the full ESPHome framework.
 */

#include "esphome/core/color.h"

// Suppress the LOG_UPDATE_INTERVAL macro used in dump_config().
#ifndef LOG_UPDATE_INTERVAL
#define LOG_UPDATE_INTERVAL(x)
#endif

namespace esphome {
namespace display {

enum DisplayType {
  DISPLAY_TYPE_GRAYSCALE,
  DISPLAY_TYPE_COLOR,
};

class DisplayBuffer {
 public:
  virtual ~DisplayBuffer() = default;

  // ---- ESPHome Component lifecycle (stubbed) ----
  virtual void setup() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }
  virtual void loop() {}
  virtual void update() {}

  // ---- ESPHome DisplayBuffer interface (stubbed) ----
  virtual DisplayType get_display_type() { return DISPLAY_TYPE_COLOR; }
  virtual void fill(Color) {}
  virtual void clear() {}

  // ---- Internal framebuffer management ----
  uint8_t *buffer_{nullptr};

  void init_internal_(size_t size) {
    // Allocate and zero-initialise the framebuffer for testing.
    this->buffer_ = static_cast<uint8_t *>(::operator new(size));
    if (this->buffer_ != nullptr) {
      __builtin_memset(this->buffer_, 0, size);
    }
  }

  // Called by update() to invoke the display lambda.  No-op in tests.
  void do_update_() {}

  // ---- ESPHome Component failure state ----
  void mark_failed(const char * /*reason*/ = nullptr) { this->failed_ = true; }
  bool is_failed() const { return this->failed_; }

 protected:
  virtual int get_width_internal() = 0;
  virtual int get_height_internal() = 0;
  virtual void draw_absolute_pixel_internal(int x, int y, Color color) = 0;

 private:
  bool failed_{false};
};

}  // namespace display
}  // namespace esphome
