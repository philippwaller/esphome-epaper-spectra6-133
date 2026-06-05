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

// Number of pixel rows streamed to the panel per loop() call during async
// execution.  Each row is HALF_ROW_BYTES (300 bytes); at 10 MHz SPI this
// equates to roughly 1.9 ms of bus time per call, keeping the ESPHome main
// loop responsive throughout a full-frame transfer (~200 loop steps total).
static constexpr int ASYNC_ROWS_PER_STEP = 8;

// Timeout for the DRF busy-wait in async mode (microseconds).
static constexpr int64_t ASYNC_DRF_TIMEOUT_US = 20000LL * 1000LL;  // 20 s

// Post-PON delay before DRF (microseconds).
static constexpr int64_t ASYNC_POST_PON_DELAY_US = 30LL * 1000LL;  // 30 ms

// Post-refresh delay after a full-frame transfer (microseconds).
static constexpr int64_t ASYNC_POST_FULL_DELAY_US = 10LL * 1000LL;  // 10 ms

// Post-refresh delay after a region transfer (microseconds).
static constexpr int64_t ASYNC_POST_REGION_DELAY_US = 300LL * 1000LL;  // 300 ms

// Full-frame online images draw 1.92M pixels synchronously through ESPHome's
// DisplayBuffer path. Yield often enough that the ESP-IDF idle watchdog can run.
static constexpr uint32_t DRAW_PIXELS_PER_YIELD = 4096;

EpaperSpectra6133::EpaperSpectra6133() : controller_(this->transport_) {}

/**
 * @brief Allocates the framebuffer and initializes GPIO/SPI/EPD hardware.
 *
 * Called once by ESPHome during the component startup phase.
 */
void EpaperSpectra6133::setup() {
  ESP_LOGI(TAG, "Setup started");
  this->init_internal_(FULL_FRAME_SIZE);
  if (this->buffer_ == nullptr) {
    ESP_LOGE(TAG, "Framebuffer allocation failed (%u bytes)", static_cast<unsigned>(FULL_FRAME_SIZE));
    this->mark_failed(LOG_STR("Display framebuffer unavailable"));
    return;
  }

  // On ESP32-S3 the PSRAM address space starts at 0x3C000000. Log a warning
  // if the 960 KB framebuffer somehow landed in internal SRAM (shouldn't
  // happen with ExternalRAMAllocator, but guards against future regressions).
  const bool in_psram = (reinterpret_cast<uintptr_t>(this->buffer_) >= 0x3C000000UL);
  if (!in_psram) {
    ESP_LOGW(TAG, "Framebuffer NOT in PSRAM (addr=0x%08X) — performance may suffer",
             static_cast<unsigned>(reinterpret_cast<uintptr_t>(this->buffer_)));
  }
  ESP_LOGI(TAG, "Framebuffer ready (%u bytes%s)", static_cast<unsigned>(FULL_FRAME_SIZE), in_psram ? ", PSRAM" : "");

  if (this->update_mode_ == UpdateMode::PARTIAL && this->change_detection_mode_ == ChangeDetectionMode::COMPARE) {
    // The previous-frame buffer is allocated on the first successful refresh
    // so the panel state is never assumed before a known-good transfer.
    ESP_LOGI(TAG, "Update mode: partial (compare) — previous-frame buffer deferred to first refresh");
  } else if (this->update_mode_ == UpdateMode::PARTIAL) {
    ESP_LOGI(TAG, "Update mode: partial (track)");
  }

  if (!this->initialize()) {
    ESP_LOGE(TAG, "Initialization failed");
    this->mark_failed();
    return;
  }
  ESP_LOGI(TAG, "Setup complete");
}

/**
 * @brief Prints the full component configuration to the ESPHome log.
 *
 * Outputs pin assignments, SPI host, display resolution, buffer size, and
 * current initialization status at CONFIG log level.
 */
void EpaperSpectra6133::dump_config() {
  ESP_LOGCONFIG(TAG, "epaper_spectra6_133:");
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
                this->change_detection_mode_ == ChangeDetectionMode::COMPARE ? "compare" : "track");
  ESP_LOGCONFIG(TAG, "  Update mode: %s", this->update_mode_ == UpdateMode::PARTIAL ? "partial" : "full");
  ESP_LOGCONFIG(TAG, "  Status: %s",
                this->is_failed() ? "failed" : (this->controller_.is_initialized() ? "ready" : "not initialized"));
  LOG_UPDATE_INTERVAL(this);
}

/** @brief Returns HARDWARE priority so this component is set up before application-level components. */
float EpaperSpectra6133::get_setup_priority() const { return setup_priority::HARDWARE; }

/**
 * @brief Schedules a full-frame display update and returns immediately.
 *
 * Called automatically by ESPHome on the configured update_interval.
 * The display lambda is re-run and the resulting framebuffer is transferred
 * to the panel cooperatively across successive loop() calls.  When
 * update_mode is PARTIAL, only the detected changed region is refreshed.
 *
 * If another display operation is already in progress it is superseded:
 * the previous job is cancelled and this one takes its place.
 */
void EpaperSpectra6133::update() {
  ESP_LOGD(TAG, "Update requested");
  if (!this->ensure_initialized_()) {
    ESP_LOGE(TAG, "Display not ready");
    return;
  }
  this->schedule_async_job_(AsyncJobType::UPDATE, 0, 0, 0, 0);
}

/**
 * @brief Fills the entire framebuffer with a single ESPHome color.
 *
 * Converts @p color to the closest 4-bit EPD nibble value and writes the
 * packed byte across every byte of the framebuffer.  Does not trigger a
 * panel refresh; the next update() or transfer_full_frame() call will
 * push the result to the display.
 */
void EpaperSpectra6133::fill(Color color) { fill_buffer_with_code(this->buffer_, color_to_code(color)); }

/**
 * @brief Performs the full hardware initialization sequence: GPIO → SPI → EPD.
 *
 * Configures output/input GPIO pins, initializes the SPI master bus and
 * device handle, activates LOAD_SW, and sends the complete EPD command
 * sequence (hardware reset + ~16 register writes).  Sets initialized_ = true
 * on success.  Returns false and leaves the component in a recoverable state
 * on any hardware error.
 */
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

  ESP_LOGI(TAG, "Display initialized");
  return true;
}

void EpaperSpectra6133::clear() { this->clear(WHITE); }

void EpaperSpectra6133::clear(Color color) {
  fill_buffer_with_code(this->buffer_, color_to_code(color));
  if (!this->ensure_initialized_()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule clear");
    return;
  }
  this->schedule_async_job_(AsyncJobType::FLUSH, 0, 0, 0, 0);
}

/**
 * @brief Schedules a partial update of a logical rectangle and returns immediately.
 *
 * Re-runs the display lambda, then transfers and refreshes only the specified
 * region cooperatively from loop().
 *
 * If another display operation is already in progress it is superseded.
 */
void EpaperSpectra6133::update_region(int x, int y, int width, int height) {
  ESP_LOGD(TAG, "Region update requested: x=%d y=%d w=%d h=%d", x, y, width, height);
  if (!this->ensure_initialized_()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule update_region");
    return;
  }
  this->schedule_async_job_(AsyncJobType::UPDATE_REGION, x, y, width, height);
}

void EpaperSpectra6133::update_region(UpdateRegion region) {
  this->update_region(region.x, region.y, region.width, region.height);
}

/**
 * @brief Schedules a full framebuffer flush and returns immediately.
 *
 * The display lambda is NOT re-run.  Existing framebuffer contents are
 * transferred and the panel is refreshed cooperatively from loop().
 *
 * If another display operation is already in progress it is superseded.
 */
void EpaperSpectra6133::flush() {
  ESP_LOGD(TAG, "Flush requested");
  if (!this->ensure_initialized_()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule flush");
    return;
  }
  this->schedule_async_job_(AsyncJobType::FLUSH, 0, 0, 0, 0);
}

/**
 * @brief Schedules a partial framebuffer flush and returns immediately.
 *
 * The display lambda is NOT re-run.  Only the specified region is transferred
 * and refreshed cooperatively from loop().
 *
 * If another display operation is already in progress it is superseded.
 */
void EpaperSpectra6133::flush_region(int x, int y, int width, int height) {
  ESP_LOGD(TAG, "Flush region requested: x=%d y=%d w=%d h=%d", x, y, width, height);
  if (!this->ensure_initialized_()) {
    ESP_LOGE(TAG, "Display not ready, cannot schedule flush_region");
    return;
  }
  this->schedule_async_job_(AsyncJobType::FLUSH_REGION, x, y, width, height);
}

// ---------------------------------------------------------------------------
// Change detection API
// ---------------------------------------------------------------------------

UpdateRegion EpaperSpectra6133::detect_changed_region() const {
  if (this->change_detection_mode_ == ChangeDetectionMode::COMPARE && this->previous_frame_buffer_ != nullptr &&
      this->buffer_ != nullptr) {
    return find_changed_region(this->buffer_, this->previous_frame_buffer_);
  }
  return this->tracked_region_;
}

void EpaperSpectra6133::reset_change_tracking() { this->tracked_region_ = {}; }

/** @brief Writes a single pixel at (x, y) into the framebuffer.
 *
 * Called by ESPHome's drawing primitives (lines, shapes, text, images) for
 * every pixel they generate.  Out-of-bounds coordinates and a null buffer
 * are silently ignored.  The actual panel update happens later in update().
 *
 * @param x      Logical pixel column (0 = left edge).
 * @param y      Logical pixel row (0 = top edge).
 * @param color  ESPHome RGB color; mapped to the nearest 4-bit EPD nibble.
 */
void HOT EpaperSpectra6133::draw_absolute_pixel_internal(int x, int y, Color color) {
  if (x < 0 || x >= EPD_WIDTH || y < 0 || y >= EPD_HEIGHT || this->buffer_ == nullptr) {
    return;
  }

  write_pixel_to_buffer(this->buffer_, x, y, color_to_code(color));
  if (++this->draw_pixels_since_yield_ >= DRAW_PIXELS_PER_YIELD) {
    this->draw_pixels_since_yield_ = 0;
    App.feed_wdt();
    taskYIELD();
  }

  if (this->change_detection_mode_ == ChangeDetectionMode::TRACK ||
      (this->change_detection_mode_ == ChangeDetectionMode::COMPARE && this->previous_frame_buffer_ == nullptr)) {
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

/**
 * @brief Ensures the display is initialized before any operation that requires it.
 *
 * Returns false immediately if the component is in a failed state (no retry).
 * Returns true if already initialized. Otherwise it runs the panel init
 * sequence on demand so later updates can recover from a cold start.
 */
bool EpaperSpectra6133::ensure_initialized_() {
  // Guard against infinite re-initialization: once mark_failed() has been called,
  // is_failed() returns true permanently and we must not retry.
  if (this->is_failed()) {
    return false;
  }
  if (this->controller_.is_initialized()) {
    return true;
  }
  return this->initialize();
}

/** @brief Copies the current framebuffer to the previous-frame buffer (compare mode). */
void EpaperSpectra6133::update_previous_frame_() {
  if (this->buffer_ == nullptr) {
    return;
  }
  if (this->change_detection_mode_ != ChangeDetectionMode::COMPARE) {
    return;
  }
  if (this->previous_frame_buffer_ == nullptr) {
    this->previous_frame_buffer_ =
        static_cast<uint8_t *>(heap_caps_malloc(FULL_FRAME_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (this->previous_frame_buffer_ == nullptr) {
      ESP_LOGW(TAG, "Previous-frame buffer allocation failed — falling back to track mode");
      this->change_detection_mode_ = ChangeDetectionMode::TRACK;
      return;
    }
    ESP_LOGI(TAG, "Previous-frame buffer allocated (%u bytes, PSRAM)", static_cast<unsigned>(FULL_FRAME_SIZE));
  }
  std::memcpy(this->previous_frame_buffer_, this->buffer_, FULL_FRAME_SIZE);
}
// =============================================================================
// Cooperative display pipeline
// =============================================================================

/**
 * @brief Progresses the active display job by one bounded step.
 *
 * Called every ESPHome main-loop cycle.  Returns immediately when no display
 * job is active so the overhead during idle periods is negligible.
 *
 * When a job is in CANCELLING state while the panel is physically busy (e.g.
 * in the middle of a DRF refresh), this method keeps polling until BUSY is
 * released before tearing down hardware state.  This prevents sending SPI
 * commands to a busy panel and ensures software state stays in sync with
 * the physical hardware.
 */
void EpaperSpectra6133::loop() {
  if (this->async_job_.state == JobState::IDLE) {
    return;
  }
  if (this->async_job_.state == JobState::CANCELLING) {
    // If the panel is physically committed to a refresh (BUSY pin LOW), wait
    // for it to complete before tearing down.  Calling disable_partial_regions()
    // or starting a new job while BUSY is asserted is undefined per the datasheet.
    if (this->is_in_hw_refresh_stage_() && this->controller_.is_display_busy()) {
      return;  // panel still busy; poll again next loop()
    }
    this->abort_async_job_();
    return;
  }
  this->process_async_step_();
}

// ---------------------------------------------------------------------------
// Cancellation
// ---------------------------------------------------------------------------

/**
 * @brief Cancels the current or pending display operation.
 *
 * If no job is active and no job is pending, this is a safe no-op.
 * Cancellation takes effect at the next loop() call boundary (i.e. after the
 * current in-flight SPI transaction completes).  CS pins and partial-region
 * state are cleaned up in the following loop() invocation.
 * If the panel is physically in a hardware refresh stage (BUSY pin LOW), the
 * job is marked as CANCELLING and loop() drains it safely once BUSY clears.
 */
void EpaperSpectra6133::cancel() {
  this->pending_job_ = {};  // discard any job waiting behind the active one
  if (this->async_job_.state == JobState::IDLE) {
    return;
  }
  this->async_job_.state = JobState::CANCELLING;
}

// ---------------------------------------------------------------------------
// Async job management helpers
// ---------------------------------------------------------------------------

/**
 * @brief Cancels any active async job and initialises a new one of @p type.
 *
 * If the active job is in a hardware-refresh stage where the panel is
 * physically busy (RF_WAIT_PON through RF_WAIT_POF), the hardware cannot be
 * interrupted mid-cycle.  In that case the active job is marked cancelled so
 * loop() can drain it gracefully, and the new request is stored as a pending
 * job.  abort_async_job_() will start the pending job automatically once the
 * draining refresh completes and BUSY is released.
 *
 * For all other stages (data-transfer or idle) the active job is aborted
 * inline and the new job starts immediately.
 */
void EpaperSpectra6133::schedule_async_job_(AsyncJobType type, int x, int y, int w, int h) {
  if (this->async_job_.state != JobState::IDLE) {
    if (this->is_in_hw_refresh_stage_()) {
      // Panel is physically committed to a refresh; we cannot abort inline.
      // Queue the incoming request so it starts after the refresh drains.
      ESP_LOGD(TAG, "Display job deferred: panel in refresh stage, will start after drain");
      this->async_job_.state = JobState::CANCELLING;
      this->pending_job_.has_pending = true;
      this->pending_job_.type = type;
      this->pending_job_.x = x;
      this->pending_job_.y = y;
      this->pending_job_.w = w;
      this->pending_job_.h = h;
      return;
    }
    // Safe to abort inline: CS can be released, no refresh in progress.
    ESP_LOGD(TAG, "Cancelled active display job");
    this->abort_async_job_();
  }
  this->async_job_ = {};
  this->async_job_.type = type;
  this->async_job_.stage = AsyncStage::INIT;
  this->async_job_.state = JobState::RUNNING;
  this->async_job_.region_x = x;
  this->async_job_.region_y = y;
  this->async_job_.region_width = w;
  this->async_job_.region_height = h;
  ESP_LOGD(TAG, "Scheduled display job (type=%d)", static_cast<int>(type));
}

/**
 * @brief Cleans up hardware state after a cancelled job and clears async_job_.
 *
 * Always releases all CS pins and disables partial-region mode so the
 * display controller is left in a known-safe state for the next operation,
 * regardless of which stage was interrupted.  By the time this is called,
 * the BUSY pin has already been released (loop() waits for it before calling
 * here), so sending disable_partial_regions() via SPI is safe.
 *
 * If a pending job was queued while we were draining a refresh stage, it is
 * started immediately so is_async_busy() remains true without a gap.
 */
void EpaperSpectra6133::abort_async_job_() {
  this->controller_.end_half_transfer();        // safe to call even if CS was already high
  this->controller_.disable_partial_regions();  // safe: BUSY has cleared before we reach here
  this->async_job_ = {};
  ESP_LOGD(TAG, "Async display job aborted");

  // Start any pending job that was queued while we were draining a refresh.
  if (this->pending_job_.has_pending) {
    PendingAsyncJob p = this->pending_job_;
    this->pending_job_ = {};
    this->async_job_.type = p.type;
    this->async_job_.stage = AsyncStage::INIT;
    this->async_job_.state = JobState::RUNNING;
    this->async_job_.region_x = p.x;
    this->async_job_.region_y = p.y;
    this->async_job_.region_width = p.w;
    this->async_job_.region_height = p.h;
    ESP_LOGD(TAG, "Started pending display job (type=%d)", static_cast<int>(p.type));
  }
}

/**
 * @brief Finalises a completed async job: cleanup, update previous frame, reset tracking, and clear.
 */
void EpaperSpectra6133::finish_async_job_() {
  if (!this->async_job_.use_full_frame) {
    this->controller_.disable_partial_regions();
  }
  this->update_previous_frame_();
  this->tracked_region_ = {};
  this->async_job_ = {};
  ESP_LOGD(TAG, "Async display job completed");
}

// ---------------------------------------------------------------------------
// Async step dispatcher
// ---------------------------------------------------------------------------

/**
 * @brief Returns true if the current job stage may have the panel physically busy.
 *
 * Stages from RF_PON through RF_WAIT_POF encompass the panel's power-on,
 * display-refresh, and power-off sequence.  During these stages the BUSY pin
 * may be LOW and SPI commands must not be sent until it goes HIGH again.
 * Cancellation checks this before calling abort_async_job_() to avoid
 * interleaving new SPI traffic with an in-progress hardware refresh.
 */
bool EpaperSpectra6133::is_in_hw_refresh_stage_() const {
  switch (this->async_job_.stage) {
    case AsyncStage::RF_PON:
    case AsyncStage::RF_WAIT_PON:
    case AsyncStage::RF_DRF_DELAY:
    case AsyncStage::RF_DRF:
    case AsyncStage::RF_WAIT_DRF:
    case AsyncStage::RF_POF:
    case AsyncStage::RF_WAIT_POF:
      return true;
    default:
      return false;
  }
}

void EpaperSpectra6133::process_async_step_() {
  switch (this->async_job_.stage) {
    case AsyncStage::INIT:
      this->process_init_stage_();
      break;
    case AsyncStage::FF_CS0:
      this->process_ff_cs0_stage_();
      break;
    case AsyncStage::FF_CS1:
      this->process_ff_cs1_stage_();
      break;
    case AsyncStage::RG_IC0:
      this->process_rg_ic0_stage_();
      break;
    case AsyncStage::RG_IC1:
      this->process_rg_ic1_stage_();
      break;
    case AsyncStage::RG_DUMMY:
      this->process_rg_dummy_stage_();
      break;
    case AsyncStage::RF_PON:
      this->process_rf_pon_stage_();
      break;
    case AsyncStage::RF_WAIT_PON:
      this->process_rf_wait_pon_stage_();
      break;
    case AsyncStage::RF_DRF_DELAY:
      this->process_rf_drf_delay_stage_();
      break;
    case AsyncStage::RF_DRF:
      this->process_rf_drf_stage_();
      break;
    case AsyncStage::RF_WAIT_DRF:
      this->process_rf_wait_drf_stage_();
      break;
    case AsyncStage::RF_POF:
      this->process_rf_pof_stage_();
      break;
    case AsyncStage::RF_WAIT_POF:
      this->process_rf_wait_pof_stage_();
      break;
    case AsyncStage::RF_POST:
      this->process_rf_post_stage_();
      break;
    case AsyncStage::FINISHING:
      this->finish_async_job_();
      break;
  }
}

// ---------------------------------------------------------------------------
// Per-stage step handlers
// ---------------------------------------------------------------------------

/**
 * @brief INIT: run the display lambda if needed; resolve full-frame vs region path.
 *
 * For UPDATE and UPDATE_REGION the display lambda is called here (fast,
 * synchronous — the lambda just writes pixels into the framebuffer).
 * For UPDATE with update_mode PARTIAL, the changed region is detected and the
 * job can complete immediately if no pixels changed.
 * Computes PartialRegion descriptors for region-path jobs.
 */
void EpaperSpectra6133::process_init_stage_() {
  const bool needs_lambda =
      (this->async_job_.type == AsyncJobType::UPDATE || this->async_job_.type == AsyncJobType::UPDATE_REGION);

  if (needs_lambda) {
    // In track mode, reset the accumulator before running the lambda so we
    // capture only the pixels drawn during this update cycle.
    if (this->change_detection_mode_ == ChangeDetectionMode::TRACK) {
      this->tracked_region_ = {};
    }
    this->do_update_();
  }

  // For UPDATE with partial update mode, detect the changed region and
  // potentially skip the hardware refresh entirely if nothing changed.
  if (this->async_job_.type == AsyncJobType::UPDATE && this->update_mode_ == UpdateMode::PARTIAL) {
    const UpdateRegion changed = this->detect_changed_region();
    if (changed.empty()) {
      ESP_LOGI(TAG, "No changes detected \u2014 skipping async refresh");
      this->async_job_ = {};  // clear without calling finish_async_job_ (no hardware to clean up)
      return;
    }
    // If the changed region covers the full display, use full-frame path.
    if (changed.x <= 0 && changed.y <= 0 && changed.width >= EPD_WIDTH && changed.height >= EPD_HEIGHT) {
      this->async_job_.use_full_frame = true;
    } else {
      // Use the detected rectangle for the region transfer path.
      this->async_job_.region_x = changed.x;
      this->async_job_.region_y = changed.y;
      this->async_job_.region_width = changed.width;
      this->async_job_.region_height = changed.height;
      this->async_job_.use_full_frame = false;
    }
  } else if (this->async_job_.type == AsyncJobType::UPDATE_REGION ||
             this->async_job_.type == AsyncJobType::FLUSH_REGION) {
    this->async_job_.use_full_frame = false;
  }
  // else: UPDATE (full mode) and FLUSH use the full-frame path.

  if (this->async_job_.use_full_frame) {
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::FF_CS0;
  } else {
    this->controller_.compute_partial_regions(this->async_job_.region_x, this->async_job_.region_y,
                                              this->async_job_.region_width, this->async_job_.region_height,
                                              this->async_job_.regions, this->async_job_.has_region);

    if (!this->async_job_.has_region[0] && !this->async_job_.has_region[1]) {
      ESP_LOGW(TAG, "Async partial refresh skipped: rectangle outside panel");
      this->async_job_ = {};
      return;
    }
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RG_IC0;
  }
}

/**
 * @brief FF_CS0: stream ASYNC_ROWS_PER_STEP rows from the left IC0 half.
 *
 * On the first call (current_row == 0) the DTM command is sent to IC0 before
 * streaming begins.  CS remains LOW across all calls for this stage.
 * On completion CS is released and the stage advances to FF_CS1.
 */
void EpaperSpectra6133::process_ff_cs0_stage_() {
  if (this->async_job_.current_row == 0) {
    if (!this->controller_.begin_half_transfer(0)) {
      ESP_LOGE(TAG, "Async FF CS0 begin failed");
      this->abort_async_job_();
      return;
    }
  }

  const int remaining = EPD_HEIGHT - this->async_job_.current_row;
  const int to_send = remaining < ASYNC_ROWS_PER_STEP ? remaining : ASYNC_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_half_row(this->buffer_, this->async_job_.current_row, 0)) {
      ESP_LOGE(TAG, "Async FF CS0 row %d write failed", this->async_job_.current_row);
      this->abort_async_job_();
      return;
    }
    this->async_job_.current_row++;
  }

  if (this->async_job_.current_row >= EPD_HEIGHT) {
    this->controller_.end_half_transfer();
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::FF_CS1;
  }
}

/**
 * @brief FF_CS1: stream ASYNC_ROWS_PER_STEP rows from the right IC1 half.
 *
 * Mirror of FF_CS0 for IC1.  On completion transitions to RF_PON.
 */
void EpaperSpectra6133::process_ff_cs1_stage_() {
  if (this->async_job_.current_row == 0) {
    if (!this->controller_.begin_half_transfer(1)) {
      ESP_LOGE(TAG, "Async FF CS1 begin failed");
      this->abort_async_job_();
      return;
    }
  }

  const int remaining = EPD_HEIGHT - this->async_job_.current_row;
  const int to_send = remaining < ASYNC_ROWS_PER_STEP ? remaining : ASYNC_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_half_row(this->buffer_, this->async_job_.current_row, 1)) {
      ESP_LOGE(TAG, "Async FF CS1 row %d write failed", this->async_job_.current_row);
      this->abort_async_job_();
      return;
    }
    this->async_job_.current_row++;
  }

  if (this->async_job_.current_row >= EPD_HEIGHT) {
    this->controller_.end_half_transfer();
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RF_PON;
  }
}

/** @brief RG_IC0: stream region rows for IC0 (skipped instantly if !has_region[0]). */
void EpaperSpectra6133::process_rg_ic0_stage_() {
  if (!this->async_job_.has_region[0]) {
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RG_IC1;
    return;
  }

  const PartialRegion &region = this->async_job_.regions[0];

  if (this->async_job_.current_row == 0) {
    if (!this->controller_.begin_region_transfer(region)) {
      ESP_LOGE(TAG, "Async RG IC0 begin failed");
      this->abort_async_job_();
      return;
    }
  }

  const int remaining = static_cast<int>(region.height) - this->async_job_.current_row;
  const int to_send = remaining < ASYNC_ROWS_PER_STEP ? remaining : ASYNC_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_region_row(this->buffer_, region, this->async_job_.current_row)) {
      ESP_LOGE(TAG, "Async RG IC0 row %d write failed", this->async_job_.current_row);
      this->controller_.end_region_transfer(region.cs_index);
      this->abort_async_job_();
      return;
    }
    this->async_job_.current_row++;
  }

  if (this->async_job_.current_row >= static_cast<int>(region.height)) {
    this->controller_.end_region_transfer(region.cs_index);
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RG_IC1;
  }
}

/** @brief RG_IC1: stream region rows for IC1 (skipped instantly if !has_region[1]). */
void EpaperSpectra6133::process_rg_ic1_stage_() {
  if (!this->async_job_.has_region[1]) {
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RG_DUMMY;
    return;
  }

  const PartialRegion &region = this->async_job_.regions[1];

  if (this->async_job_.current_row == 0) {
    if (!this->controller_.begin_region_transfer(region)) {
      ESP_LOGE(TAG, "Async RG IC1 begin failed");
      this->abort_async_job_();
      return;
    }
  }

  const int remaining = static_cast<int>(region.height) - this->async_job_.current_row;
  const int to_send = remaining < ASYNC_ROWS_PER_STEP ? remaining : ASYNC_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_region_row(this->buffer_, region, this->async_job_.current_row)) {
      ESP_LOGE(TAG, "Async RG IC1 row %d write failed", this->async_job_.current_row);
      this->controller_.end_region_transfer(region.cs_index);
      this->abort_async_job_();
      return;
    }
    this->async_job_.current_row++;
  }

  if (this->async_job_.current_row >= static_cast<int>(region.height)) {
    this->controller_.end_region_transfer(region.cs_index);
    this->async_job_.current_row = 0;
    this->async_job_.stage = AsyncStage::RG_DUMMY;
  }
}

/** @brief RG_DUMMY: arm a minimal 16x2 PTLW on every IC that had no pixel changes. */
void EpaperSpectra6133::process_rg_dummy_stage_() {
  for (uint8_t ic = 0; ic < 2; ic++) {
    if (this->async_job_.has_region[ic]) {
      continue;
    }
    if (!this->controller_.arm_dummy_region(ic)) {
      ESP_LOGE(TAG, "Async RG dummy IC%u failed", ic);
      this->abort_async_job_();
      return;
    }
  }
  this->async_job_.stage = AsyncStage::RF_PON;
}

/** @brief RF_PON: send the PON command; transitions to RF_WAIT_PON immediately. */
void EpaperSpectra6133::process_rf_pon_stage_() {
  if (!this->controller_.send_refresh_pon()) {
    ESP_LOGE(TAG, "Async RF PON send failed");
    this->abort_async_job_();
    return;
  }
  this->async_job_.stage = AsyncStage::RF_WAIT_PON;
}

/** @brief RF_WAIT_PON: yield each loop() call until BUSY goes high (panel powered on). */
void EpaperSpectra6133::process_rf_wait_pon_stage_() {
  if (this->controller_.is_display_busy()) {
    return;  // still busy; try again next loop()
  }
  this->async_job_.stage_start_us = esp_timer_get_time();
  this->async_job_.stage = AsyncStage::RF_DRF_DELAY;
}

/** @brief RF_DRF_DELAY: non-blocking 30 ms delay required between PON and DRF. */
void EpaperSpectra6133::process_rf_drf_delay_stage_() {
  if ((esp_timer_get_time() - this->async_job_.stage_start_us) < ASYNC_POST_PON_DELAY_US) {
    return;  // delay not elapsed; try again next loop()
  }
  this->async_job_.stage = AsyncStage::RF_DRF;
}

/** @brief RF_DRF: send the DRF command; records start time; transitions to RF_WAIT_DRF. */
void EpaperSpectra6133::process_rf_drf_stage_() {
  if (!this->controller_.send_refresh_drf()) {
    ESP_LOGE(TAG, "Async RF DRF send failed");
    this->abort_async_job_();
    return;
  }
  this->async_job_.stage_start_us = esp_timer_get_time();
  this->async_job_.stage = AsyncStage::RF_WAIT_DRF;
}

/**
 * @brief RF_WAIT_DRF: yield each loop() call until BUSY goes high (panel refreshed).
 *
 * A full e-paper refresh can take up to ~20 s.  The timeout guard here is
 * intentionally generous so normal refresh cycles always complete.  If the
 * panel is unresponsive the job is aborted to avoid an infinite loop.
 */
void EpaperSpectra6133::process_rf_wait_drf_stage_() {
  if (this->controller_.is_display_busy()) {
    if ((esp_timer_get_time() - this->async_job_.stage_start_us) > ASYNC_DRF_TIMEOUT_US) {
      ESP_LOGE(TAG, "Async RF DRF timeout \u2014 BUSY never released");
      this->abort_async_job_();
    }
    return;  // still busy; try again next loop()
  }
  this->async_job_.stage = AsyncStage::RF_POF;
}

/** @brief RF_POF: send the POF command; transitions to RF_WAIT_POF. */
void EpaperSpectra6133::process_rf_pof_stage_() {
  if (!this->controller_.send_refresh_pof()) {
    ESP_LOGE(TAG, "Async RF POF send failed");
    this->abort_async_job_();
    return;
  }
  this->async_job_.stage = AsyncStage::RF_WAIT_POF;
}

/** @brief RF_WAIT_POF: yield each loop() call until BUSY goes high (panel powered off). */
void EpaperSpectra6133::process_rf_wait_pof_stage_() {
  if (this->controller_.is_display_busy()) {
    return;  // still busy; try again next loop()
  }
  this->async_job_.stage_start_us = esp_timer_get_time();
  this->async_job_.stage = AsyncStage::RF_POST;
}

/**
 * @brief RF_POST: non-blocking post-refresh delay before finalisation.
 *
 * Full-frame transfers use a 10 ms delay; region transfers use 300 ms.
 * These match the vTaskDelay() calls in the blocking transfer_full_frame()
 * and transfer_region() implementations.
 */
void EpaperSpectra6133::process_rf_post_stage_() {
  const int64_t delay_us = this->async_job_.use_full_frame ? ASYNC_POST_FULL_DELAY_US : ASYNC_POST_REGION_DELAY_US;
  if ((esp_timer_get_time() - this->async_job_.stage_start_us) < delay_us) {
    return;  // delay not elapsed; try again next loop()
  }
  this->async_job_.stage = AsyncStage::FINISHING;
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
