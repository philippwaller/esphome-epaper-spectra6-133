#include "esphome/core/application.h"

/**
 * @file epaper_spectra6_133.cpp
 * @brief Implements the ESPHome-facing display component lifecycle and drawing hooks.
 */

#include "epaper_spectra6_133.h"
#include "epaper_spectra6_133_framebuffer.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esphome/core/log.h"

namespace esphome {
namespace epaper_spectra6_133 {

static const char *const TAG = "epaper_spectra6_133";

// Full-frame online images draw 1.92M pixels synchronously through ESPHome's
// DisplayBuffer path. Insert real scheduler delays so the ESP-IDF idle task
// can run during long draws and the task watchdog does not reset the device.
static constexpr uint32_t DRAW_PIXELS_PER_DELAY = 4096;
#ifdef USE_ESP_IDF
// ESPHome's generic 30 ms blocking warning is too strict for a synchronous
// 1200x1600 DisplayBuffer render. Panel I/O remains split into loop() stages;
// this threshold keeps warnings for genuinely pathological component calls.
static constexpr uint16_t DISPLAY_BLOCKING_WARNING_THRESHOLD_MS = 30000;
#endif

namespace {

struct HorizontalInterval {
  int x0;
  int x1;
};

bool partial_regions_contain_tracked_region(const PartialRegion regions[2], const bool has_region[2],
                                            const UpdateRegion &tracked) {
  if (tracked.empty()) {
    return true;
  }

  const int target_x0 = tracked.x;
  const int target_y0 = tracked.y;
  const int target_x1 = tracked.x + tracked.width;
  const int target_y1 = tracked.y + tracked.height;
  HorizontalInterval intervals[2];
  int interval_count = 0;

  for (int i = 0; i < 2; i++) {
    if (!has_region[i]) {
      continue;
    }
    const PartialRegion &region = regions[i];
    const int region_y0 = region.y_start;
    const int region_y1 = static_cast<int>(region.y_start) + static_cast<int>(region.height);
    if (region_y0 > target_y0 || region_y1 < target_y1) {
      continue;
    }

    const int half_start = region.cs_index == 0 ? 0 : EPD_WIDTH / 2;
    intervals[interval_count++] = {
        half_start + static_cast<int>(region.x_start),
        half_start + static_cast<int>(region.x_start) + static_cast<int>(region.width),
    };
  }

  if (interval_count == 2 && intervals[1].x0 < intervals[0].x0) {
    const HorizontalInterval tmp = intervals[0];
    intervals[0] = intervals[1];
    intervals[1] = tmp;
  }

  int covered_until = target_x0;
  for (int i = 0; i < interval_count; i++) {
    if (intervals[i].x1 <= covered_until) {
      continue;
    }
    if (intervals[i].x0 > covered_until) {
      return false;
    }
    covered_until = intervals[i].x1;
    if (covered_until >= target_x1) {
      return true;
    }
  }
  return false;
}

}  // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

EpaperSpectra6133::EpaperSpectra6133()
    : controller_(this->transport_), executor_(this->controller_, this->transport_, *this) {
#ifdef USE_ESP_IDF
  this->warn_if_blocking_over_ = DISPLAY_BLOCKING_WARNING_THRESHOLD_MS;
#endif
  this->transport_.set_runtime_hooks(this);
}

// ---------------------------------------------------------------------------
// ESPHome lifecycle
// ---------------------------------------------------------------------------

void EpaperSpectra6133::setup() {
  ESP_LOGI(TAG, "Setup started");
  this->init_internal_(FULL_FRAME_SIZE);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Framebuffer allocation failed (%u bytes)", static_cast<unsigned>(FULL_FRAME_SIZE));
    this->mark_failed(LOG_STR("Display framebuffer unavailable"));
    return;
  }

  const bool in_psram = (reinterpret_cast<uintptr_t>(this->buffer_) >= 0x3C000000UL);
  if (!in_psram) {
    ESP_LOGW(TAG, "Framebuffer NOT in PSRAM (addr=0x%08X) — performance may suffer",
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(this->buffer_)));
  }
  ESP_LOGI(TAG, "Framebuffer ready (%u bytes%s)", static_cast<unsigned>(FULL_FRAME_SIZE), in_psram ? ", PSRAM" : "");

  if (this->executor_.refresh_mode() == RefreshMode::PARTIAL &&
      this->executor_.change_detection_mode() == ChangeDetectionMode::COMPARE) {
    ESP_LOGI(TAG, "Refresh mode: partial (compare) — previous-frame buffer deferred to first refresh");
  } else if (this->executor_.refresh_mode() == RefreshMode::PARTIAL) {
    ESP_LOGI(TAG, "Refresh mode: partial (track)");
  }

  if (!this->initialize()) {
    ESP_LOGE(TAG, "Initialization failed");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Setup complete");
}

void EpaperSpectra6133::dump_config() {
  ESP_LOGCONFIG(TAG, "epaper_spectra6_133:");
  ESP_LOGCONFIG(TAG, "  Version: %s", VERSION);
  ESP_LOGCONFIG(TAG, "  Resolution: %dx%d", EPD_WIDTH, EPD_HEIGHT);
  ESP_LOGCONFIG(TAG, "  SPI host: %d (SPI3_HOST=2)", static_cast<int>(this->transport_.spi_host()));
  ESP_LOGCONFIG(TAG, "  CS0 pin: %d", this->transport_.cs0_pin());
  ESP_LOGCONFIG(TAG, "  CS1 pin: %d", this->transport_.cs1_pin());
  ESP_LOGCONFIG(TAG, "  CLK pin: %d", this->transport_.clk_pin());
  ESP_LOGCONFIG(TAG, "  DATA0 pin: %d", this->transport_.data0_pin());
  ESP_LOGCONFIG(TAG, "  DATA1 pin: %d", this->transport_.data1_pin());
  ESP_LOGCONFIG(TAG, "  BUSY pin: %d", this->transport_.busy_pin());
  ESP_LOGCONFIG(TAG, "  RESET pin: %d", this->transport_.reset_pin());
  ESP_LOGCONFIG(TAG, "  POWER pin: %d", this->transport_.power_pin());
  ESP_LOGCONFIG(TAG, "  Buffer: %u bytes", static_cast<unsigned>(FULL_FRAME_SIZE));
  ESP_LOGCONFIG(TAG, "  Change detection mode: %s",
                this->executor_.change_detection_mode() == ChangeDetectionMode::COMPARE ? "compare" : "track");
  ESP_LOGCONFIG(TAG, "  Refresh mode: %s", this->executor_.refresh_mode() == RefreshMode::PARTIAL ? "partial" : "full");
  ESP_LOGCONFIG(TAG, "  Clear color: rgb(%u, %u, %u)", this->clear_color_.red, this->clear_color_.green,
                this->clear_color_.blue);
  ESP_LOGCONFIG(TAG, "  Auto sleep: %s", this->executor_.auto_sleep() ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Power off after sleep: %s", this->executor_.power_off_after_sleep() ? "yes" : "no");
  ESP_LOGCONFIG(TAG, "  Status: %s",
                this->is_failed() ? "failed"
                                  : (this->executor_.is_sleeping()
                                         ? "sleeping"
                                         : (this->controller_.is_initialized() ? "ready" : "not initialized")));
  LOG_UPDATE_INTERVAL(this);
}

float EpaperSpectra6133::get_setup_priority() const { return setup_priority::HARDWARE; }

void EpaperSpectra6133::set_update_mode(RefreshMode mode) {
  ESP_LOGW(TAG, "set_update_mode() is deprecated and will be removed in version 1.0; use set_refresh_mode()");
  this->set_refresh_mode(mode);
}

// ---------------------------------------------------------------------------
// Display operations — thin delegation to executor
// ---------------------------------------------------------------------------

void EpaperSpectra6133::update() {
  ESP_LOGD(TAG, "Update requested");
  if (this->executor_.request_operation(DisplayOperationType::UPDATE, 0, 0, 0, 0) || this->executor_.is_processing()) {
    this->enable_loop();
  }
}

void EpaperSpectra6133::loop() {
  this->executor_.step();
  if (!this->executor_.is_processing()) {
    this->disable_loop();
  }
}

void EpaperSpectra6133::fill(Color color) {
  fill_buffer_with_code(this->buffer_, color_to_code(color.red, color.green, color.blue));
}

void EpaperSpectra6133::clear() {
  fill_buffer_with_code(this->buffer_,
                        color_to_code(this->clear_color_.red, this->clear_color_.green, this->clear_color_.blue));
}

void EpaperSpectra6133::update_region(int x, int y, int width, int height) {
  ESP_LOGD(TAG, "Region update requested: x=%d y=%d w=%d h=%d", x, y, width, height);
  if (this->executor_.request_operation(DisplayOperationType::UPDATE_REGION, x, y, width, height) ||
      this->executor_.is_processing()) {
    this->enable_loop();
  }
}

void EpaperSpectra6133::update_region(UpdateRegion region) {
  this->update_region(region.x, region.y, region.width, region.height);
}

void EpaperSpectra6133::refresh() {
  ESP_LOGD(TAG, "Refresh requested");
  if (this->executor_.request_operation(DisplayOperationType::REFRESH, 0, 0, 0, 0) || this->executor_.is_processing()) {
    this->enable_loop();
  }
}

void EpaperSpectra6133::flush() {
  ESP_LOGW(TAG, "flush() is deprecated and will be removed in version 1.0; use refresh()");
  this->refresh();
}

void EpaperSpectra6133::refresh_region(int x, int y, int width, int height) {
  ESP_LOGD(TAG, "Refresh region requested: x=%d y=%d w=%d h=%d", x, y, width, height);
  if (this->executor_.request_operation(DisplayOperationType::REFRESH_REGION, x, y, width, height) ||
      this->executor_.is_processing()) {
    this->enable_loop();
  }
}

void EpaperSpectra6133::flush_region(int x, int y, int width, int height) {
  ESP_LOGW(TAG, "flush_region() is deprecated and will be removed in version 1.0; use refresh_region()");
  this->refresh_region(x, y, width, height);
}

bool EpaperSpectra6133::is_busy() const {
  ESP_LOGW(TAG, "is_busy() is deprecated and will be removed in version 1.0; use is_processing()");
  return this->is_processing();
}

void EpaperSpectra6133::cancel() {
  this->executor_.cancel();
  if (!this->executor_.is_processing()) {
    this->disable_loop();
  }
}

void EpaperSpectra6133::sleep() {
  ESP_LOGD(TAG, "Deep sleep requested");
  if (this->is_failed()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule sleep");
    return;
  }
  if (this->executor_.is_sleeping()) {
    return;
  }
  if (!this->controller_.is_initialized() && !this->initialize()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule sleep");
    return;
  }
  this->executor_.schedule_sleep();
  if (this->executor_.is_processing()) {
    this->enable_loop();
  }
}

bool EpaperSpectra6133::wake() {
  if (this->is_failed()) {
    return false;
  }
  if (this->controller_.is_initialized() && !this->executor_.is_sleeping()) {
    return true;
  }
  ESP_LOGD(TAG, "Waking display from deep sleep");
  return this->initialize();
}

// ---------------------------------------------------------------------------
// Change detection
// ---------------------------------------------------------------------------

UpdateRegion EpaperSpectra6133::detect_changed_region() const {
  if (this->executor_.change_detection_mode() == ChangeDetectionMode::COMPARE &&
      this->previous_frame_buffer_ != nullptr && this->buffer_ != nullptr) {
    return find_changed_region(this->buffer_, this->previous_frame_buffer_);
  }
  return this->tracked_region_;
}

void EpaperSpectra6133::reset_change_tracking() { this->tracked_region_ = {}; }

// ---------------------------------------------------------------------------
// Pixel drawing
// ---------------------------------------------------------------------------

void HOT EpaperSpectra6133::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT || this->buffer_ == nullptr) {
    return;
  }

  write_pixel_to_buffer(this->buffer_, x, y, color_to_code(color.red, color.green, color.blue));
  if (++this->draw_pixels_since_yield_ >= DRAW_PIXELS_PER_DELAY) {
    this->draw_pixels_since_yield_ = 0;
    App.feed_wdt();
    vTaskDelay(pdMS_TO_TICKS(1));
  }

  if (this->executor_.change_detection_mode() == ChangeDetectionMode::TRACK ||
      (this->executor_.change_detection_mode() == ChangeDetectionMode::COMPARE &&
       this->previous_frame_buffer_ == nullptr)) {
    if (!this->tracked_region_.empty()) {
      const int rx = this->tracked_region_.x;
      const int ry = this->tracked_region_.y;
      const int rw = this->tracked_region_.width;
      const int rh = this->tracked_region_.height;
      if (x >= rx && x < rx + rw && y >= ry && y < ry + rh) {
        return;
      }
      const int nx = x < rx ? x : rx;
      const int ny = y < ry ? y : ry;
      this->tracked_region_.x = nx;
      this->tracked_region_.y = ny;
      this->tracked_region_.width = (x >= rx + rw ? x + 1 : rx + rw) - nx;
      this->tracked_region_.height = (y >= ry + rh ? y + 1 : ry + rh) - ny;
    } else {
      this->tracked_region_ = {x, y, 1, 1};
    }
  }
}

// ---------------------------------------------------------------------------
// Hardware initialization
// ---------------------------------------------------------------------------

bool EpaperSpectra6133::ensure_initialized_() {
  if (this->is_failed()) {
    return false;
  }
  if (this->controller_.is_initialized()) {
    return true;
  }
  if (this->executor_.is_in_hw_refresh_stage()) {
    return false;
  }
  if (this->executor_.is_sleeping()) {
    return this->wake();
  }
  return this->initialize();
}

bool EpaperSpectra6133::ensure_display_ready() { return this->ensure_initialized_(); }

void EpaperSpectra6133::render_update() { this->do_update_(); }

void EpaperSpectra6133::on_operation_finished(const DisplayOperation &operation) {
  this->update_previous_frame_(operation);
  if (operation.use_full_frame) {
    this->reset_change_tracking();
  } else if (partial_regions_contain_tracked_region(operation.regions, operation.has_region, this->tracked_region_)) {
    this->reset_change_tracking();
  }
}

uint8_t *EpaperSpectra6133::framebuffer() { return this->buffer_; }

void EpaperSpectra6133::yield_for_milliseconds(uint32_t ms) {
  App.feed_wdt();
  vTaskDelay(pdMS_TO_TICKS(ms));
}

bool EpaperSpectra6133::initialize() {
  ESP_LOGI(TAG, "Initializing (BUSY=%d)", this->transport_.busy_level());
  if (!this->transport_.init_gpio()) {
    return false;
  }
  if (!this->transport_.init_spi()) {
    return false;
  }

  this->transport_.set_load_switch(1);
  this->transport_.set_cs_all(1);
  ESP_LOGD(TAG, "LOAD_SW active, CS idle, BUSY before reset=%d", this->transport_.busy_level());

  if (!this->controller_.init_panel()) {
    return false;
  }

  this->executor_.clear_sleeping();
  ESP_LOGI(TAG, "Display initialized");
  return true;
}

// ---------------------------------------------------------------------------
// Previous-frame buffer management
// ---------------------------------------------------------------------------

void EpaperSpectra6133::update_previous_frame_(const DisplayOperation &operation) {
  if (this->buffer_ == nullptr) {
    return;
  }
  if (this->executor_.change_detection_mode() != ChangeDetectionMode::COMPARE) {
    return;
  }
  if (this->previous_frame_buffer_ == nullptr) {
    this->previous_frame_buffer_ =
        static_cast<uint8_t *>(heap_caps_malloc(FULL_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->previous_frame_buffer_ == nullptr) {
      ESP_LOGW(TAG, "Previous-frame buffer allocation failed — falling back to track mode");
      this->executor_.set_change_detection_mode(ChangeDetectionMode::TRACK);
      return;
    }
    ESP_LOGI(TAG, "Previous-frame buffer allocated (%u bytes, PSRAM)", static_cast<unsigned>(FULL_FRAME_SIZE));
  }

  if (operation.use_full_frame) {
    std::memcpy(this->previous_frame_buffer_, this->buffer_, FULL_FRAME_SIZE);
  } else {
    for (int i = 0; i < 2; i++) {
      if (!operation.has_region[i]) {
        continue;
      }
      const PartialRegion &region = operation.regions[i];
      for (int row = 0; row < static_cast<int>(region.height); row++) {
        const size_t offset =
            (static_cast<size_t>(region.y_start) + static_cast<size_t>(row)) * ROW_BYTES + region.row_byte_offset;
        std::memcpy(this->previous_frame_buffer_ + offset, this->buffer_ + offset, region.row_byte_count);
      }
    }
  }
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
