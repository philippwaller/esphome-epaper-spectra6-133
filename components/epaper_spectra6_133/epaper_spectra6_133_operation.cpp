#include "epaper_spectra6_133_operation.h"

/**
 * @file epaper_spectra6_133_operation.cpp
 * @brief Implements the cooperative display operation state machine.
 */

#include "esp_log.h"
#include "esp_timer.h"
#include "esphome/core/defines.h"
#include "esphome/core/log.h"

#ifdef USE_LOGGER
#include "esphome/components/logger/logger.h"
#endif

#include <cstdio>

#if defined(USE_LOGGER) && defined(USE_LOGGER_RUNTIME_TAG_LEVELS) && !defined(EPAPER_SPECTRA6_133_HOST_TEST_SUPPORT)
namespace esphome::logger {
inline uint8_t Logger::level_for(const char *tag) {
  auto it = this->log_levels_.find(tag);
  if (it != this->log_levels_.end()) {
    return it->second;
  }
  return this->current_level_;
}
}  // namespace esphome::logger
#endif

namespace esphome {
namespace epaper_spectra6_133 {

static const char *const OPERATION_TAG = "epaper_spectra6_133.operation";
static const char *const PERFORMANCE_TAG = "epaper_spectra6_133.perf";

// Number of pixel rows streamed to the panel per step() call. Each row is
// HALF_ROW_BYTES (300 bytes); at 10 MHz SPI this equates to roughly 1.9 ms of
// bus time per call (~200 steps for a full-frame transfer).
static constexpr int OPERATION_ROWS_PER_STEP = 8;

// Timeout for the DRF busy-wait in a refresh operation (microseconds).
static constexpr int64_t REFRESH_BUSY_TIMEOUT_US = 20000LL * 1000LL;  // 20 s

// Post-PON delay before DRF (microseconds).
static constexpr int64_t PRE_REFRESH_DELAY_US = 30LL * 1000LL;  // 30 ms

// Post-refresh delay after a full-frame transfer (microseconds).
static constexpr int64_t POST_FULL_REFRESH_DELAY_US = 10LL * 1000LL;  // 10 ms

// Post-refresh delay after a region transfer (microseconds).
static constexpr int64_t POST_REGION_REFRESH_DELAY_US = 300LL * 1000LL;  // 300 ms

namespace {

const char *operation_type_to_string(DisplayOperationType type) {
  switch (type) {
    case DisplayOperationType::UPDATE:
      return "update";
    case DisplayOperationType::UPDATE_REGION:
      return "update_region";
    case DisplayOperationType::REFRESH:
      return "refresh";
    case DisplayOperationType::REFRESH_REGION:
      return "refresh_region";
    case DisplayOperationType::SLEEP:
      return "sleep";
    case DisplayOperationType::NONE:
    default:
      return "none";
  }
}

const char *operation_result_to_string(DisplayOperationResult result) {
  switch (result) {
    case DisplayOperationResult::COMPLETED:
      return "completed";
    case DisplayOperationResult::SKIPPED:
      return "skipped";
    case DisplayOperationResult::ABORTED:
      return "aborted";
    case DisplayOperationResult::NONE:
    default:
      return "none";
  }
}

const char *performance_bucket_to_string(DisplayPerformanceBucket bucket) {
  switch (bucket) {
    case DisplayPerformanceBucket::INIT:
      return "init";
    case DisplayPerformanceBucket::RENDER:
      return "render";
    case DisplayPerformanceBucket::CHANGE_DETECT:
      return "detect";
    case DisplayPerformanceBucket::TRANSFER_LEFT:
      return "transfer_left";
    case DisplayPerformanceBucket::TRANSFER_RIGHT:
      return "transfer_right";
    case DisplayPerformanceBucket::ARM_UNCHANGED:
      return "arm_unchanged";
    case DisplayPerformanceBucket::POWER_ON:
      return "power_on";
    case DisplayPerformanceBucket::WAIT_POWER_ON:
      return "wait_power_on";
    case DisplayPerformanceBucket::PRE_REFRESH_DELAY:
      return "pre_refresh_delay";
    case DisplayPerformanceBucket::REFRESH:
      return "refresh";
    case DisplayPerformanceBucket::WAIT_REFRESH:
      return "wait_refresh";
    case DisplayPerformanceBucket::POWER_OFF:
      return "power_off";
    case DisplayPerformanceBucket::WAIT_POWER_OFF:
      return "wait_power_off";
    case DisplayPerformanceBucket::DEEP_SLEEP:
      return "deep_sleep";
    case DisplayPerformanceBucket::POST_REFRESH_DELAY:
      return "post_refresh_delay";
    case DisplayPerformanceBucket::FINISH:
      return "finish";
    case DisplayPerformanceBucket::COUNT:
    default:
      return "unknown";
  }
}

DisplayPerformanceBucket stage_to_performance_bucket(DisplayOperationStage stage) {
  switch (stage) {
    case DisplayOperationStage::TRANSFER_LEFT_HALF:
    case DisplayOperationStage::TRANSFER_LEFT_REGION:
      return DisplayPerformanceBucket::TRANSFER_LEFT;
    case DisplayOperationStage::TRANSFER_RIGHT_HALF:
    case DisplayOperationStage::TRANSFER_RIGHT_REGION:
      return DisplayPerformanceBucket::TRANSFER_RIGHT;
    case DisplayOperationStage::ARM_UNCHANGED_HALVES:
      return DisplayPerformanceBucket::ARM_UNCHANGED;
    case DisplayOperationStage::POWER_ON:
      return DisplayPerformanceBucket::POWER_ON;
    case DisplayOperationStage::WAIT_POWER_ON:
      return DisplayPerformanceBucket::WAIT_POWER_ON;
    case DisplayOperationStage::PRE_REFRESH_DELAY:
      return DisplayPerformanceBucket::PRE_REFRESH_DELAY;
    case DisplayOperationStage::REFRESH_SCREEN:
      return DisplayPerformanceBucket::REFRESH;
    case DisplayOperationStage::WAIT_REFRESH:
      return DisplayPerformanceBucket::WAIT_REFRESH;
    case DisplayOperationStage::POWER_OFF:
      return DisplayPerformanceBucket::POWER_OFF;
    case DisplayOperationStage::WAIT_POWER_OFF:
      return DisplayPerformanceBucket::WAIT_POWER_OFF;
    case DisplayOperationStage::DEEP_SLEEP:
      return DisplayPerformanceBucket::DEEP_SLEEP;
    case DisplayOperationStage::POST_REFRESH_DELAY:
      return DisplayPerformanceBucket::POST_REFRESH_DELAY;
    case DisplayOperationStage::FINISHING:
      return DisplayPerformanceBucket::FINISH;
    case DisplayOperationStage::INIT:
    default:
      return DisplayPerformanceBucket::INIT;
  }
}

int64_t bucket_us(const DisplayPerformanceMetrics &metrics, DisplayPerformanceBucket bucket) {
  return metrics.buckets_us[static_cast<uint8_t>(bucket)];
}

int64_t to_ms(int64_t us) { return (us + 500LL) / 1000LL; }

}  // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void DisplayOperationExecutor::step() {
  if (this->active_operation_.state == DisplayOperationState::IDLE) {
    return;
  }
  if (this->active_operation_.state == DisplayOperationState::CANCELLING) {
    if (this->is_in_hw_refresh_stage_()) {
      if (this->active_operation_.stage == DisplayOperationStage::POST_REFRESH_DELAY) {
        const int64_t delay_us =
            this->active_operation_.use_full_frame ? POST_FULL_REFRESH_DELAY_US : POST_REGION_REFRESH_DELAY_US;
        if ((esp_timer_get_time() - this->active_operation_.stage_start_us) < delay_us) {
          return;
        }
      } else if (this->controller_.is_display_busy()) {
        return;
      }
    }
    this->abort_display_operation_();
    return;
  }
  this->process_display_operation_step_();
}

bool DisplayOperationExecutor::request_operation(DisplayOperationType type, int x, int y, int w, int h) {
  if (this->active_operation_.state != DisplayOperationState::IDLE && this->is_in_hw_refresh_stage_()) {
    this->schedule_display_operation_(type, x, y, w, h);
    return true;
  }
  if (!this->host_.ensure_display_ready()) {
    ESP_LOGW(OPERATION_TAG, "Display operation not scheduled: %s display not ready", operation_type_to_string(type));
    return false;
  }
  this->schedule_display_operation_(type, x, y, w, h);
  return true;
}

void DisplayOperationExecutor::cancel() {
  this->pending_operation_ = {};
  if (this->active_operation_.state == DisplayOperationState::IDLE) {
    return;
  }
  this->active_operation_.state = DisplayOperationState::CANCELLING;
}

void DisplayOperationExecutor::schedule_sleep() {
  this->schedule_display_operation_(DisplayOperationType::SLEEP, 0, 0, 0, 0);
}

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

void DisplayOperationExecutor::start_display_operation_(DisplayOperationType type, int x, int y, int w, int h) {
  this->active_operation_ = {};
  this->active_operation_.type = type;
  this->active_operation_.stage = DisplayOperationStage::INIT;
  this->active_operation_.state = DisplayOperationState::RUNNING;
  this->active_operation_.region_x = x;
  this->active_operation_.region_y = y;
  this->active_operation_.region_width = w;
  this->active_operation_.region_height = h;
  this->start_performance_metrics_();
  ESP_LOGD(OPERATION_TAG, "Scheduled display operation: %s", operation_type_to_string(type));
}

void DisplayOperationExecutor::schedule_display_operation_(DisplayOperationType type, int x, int y, int w, int h) {
  if (this->active_operation_.state != DisplayOperationState::IDLE) {
    if (this->is_in_hw_refresh_stage_()) {
      ESP_LOGD(OPERATION_TAG, "Display operation deferred: panel in refresh stage, will start after drain");
      this->active_operation_.state = DisplayOperationState::CANCELLING;
      this->pending_operation_.has_pending = true;
      this->pending_operation_.type = type;
      this->pending_operation_.x = x;
      this->pending_operation_.y = y;
      this->pending_operation_.w = w;
      this->pending_operation_.h = h;
      return;
    }
    ESP_LOGD(OPERATION_TAG, "Cancelled active display operation");
    this->abort_display_operation_();
  }
  this->start_display_operation_(type, x, y, w, h);
}

void DisplayOperationExecutor::abort_display_operation_() {
  // Cancellation can happen from either a half-transfer, a region-transfer, or
  // a drained hardware stage. Release broad panel state so the next operation
  // starts from known CS/PTLW defaults without requiring stage-specific cleanup.
  this->controller_.end_half_transfer();
  if (!this->sleeping_) {
    this->controller_.disable_partial_regions();
  }
  this->finish_performance_metrics_(DisplayOperationResult::ABORTED);
  this->active_operation_ = {};
  ESP_LOGD(OPERATION_TAG, "Display operation aborted");

  if (this->pending_operation_.has_pending) {
    PendingDisplayOperation p = this->pending_operation_;
    this->pending_operation_ = {};
    this->start_display_operation_(p.type, p.x, p.y, p.w, p.h);
    ESP_LOGD(OPERATION_TAG, "Started pending display operation: %s", operation_type_to_string(p.type));
  }
}

void DisplayOperationExecutor::finish_display_operation_() {
  if (!this->active_operation_.use_full_frame && !this->sleeping_) {
    this->controller_.disable_partial_regions();
  }
  this->host_.on_operation_finished(this->active_operation_);
  this->finish_performance_metrics_(DisplayOperationResult::COMPLETED);
  this->active_operation_ = {};
  ESP_LOGD(OPERATION_TAG, "Display operation completed");
}

void DisplayOperationExecutor::transition_to_stage_(DisplayOperationStage stage) {
  if (this->performance_metrics_active_) {
    const int64_t now_us = esp_timer_get_time();
    const auto bucket = stage_to_performance_bucket(this->performance_stage_);
    const int64_t elapsed_us = this->add_current_stage_elapsed_(now_us);
    this->log_performance_stage_(bucket, elapsed_us);
    this->performance_stage_ = stage;
    this->performance_stage_start_us_ = now_us;
  }
  this->active_operation_.stage = stage;
}

void DisplayOperationExecutor::start_performance_metrics_() {
  const uint8_t log_level = this->performance_log_level_();
  this->performance_metrics_active_ = log_level >= ESPHOME_LOG_LEVEL_DEBUG;
  this->performance_live_tracking_active_ = log_level >= ESPHOME_LOG_LEVEL_VERBOSE;
  this->current_performance_metrics_ = {};
  if (!this->performance_metrics_active_) {
    this->performance_live_tracking_active_ = false;
    return;
  }

  const int64_t now_us = esp_timer_get_time();
  this->current_performance_metrics_.type = this->active_operation_.type;
  this->current_performance_metrics_.region_x = this->active_operation_.region_x;
  this->current_performance_metrics_.region_y = this->active_operation_.region_y;
  this->current_performance_metrics_.region_width = this->active_operation_.region_width;
  this->current_performance_metrics_.region_height = this->active_operation_.region_height;
  this->performance_operation_start_us_ = now_us;
  this->performance_stage_start_us_ = now_us;
  this->performance_stage_ = this->active_operation_.stage;
}

void DisplayOperationExecutor::finish_performance_metrics_(DisplayOperationResult result) {
  if (!this->performance_metrics_active_) {
    return;
  }

  const int64_t now_us = esp_timer_get_time();
  const auto bucket = stage_to_performance_bucket(this->performance_stage_);
  const int64_t elapsed_us = this->add_current_stage_elapsed_(now_us);
  this->log_performance_stage_(bucket, elapsed_us);
  this->current_performance_metrics_.valid = true;
  this->current_performance_metrics_.result = result;
  this->current_performance_metrics_.use_full_frame = this->active_operation_.use_full_frame;
  this->current_performance_metrics_.region_x = this->active_operation_.region_x;
  this->current_performance_metrics_.region_y = this->active_operation_.region_y;
  this->current_performance_metrics_.region_width = this->active_operation_.region_width;
  this->current_performance_metrics_.region_height = this->active_operation_.region_height;
  this->current_performance_metrics_.total_us = now_us - this->performance_operation_start_us_;
  this->last_performance_metrics_ = this->current_performance_metrics_;
  this->log_performance_metrics_(this->last_performance_metrics_);
  this->performance_metrics_active_ = false;
  this->performance_live_tracking_active_ = false;
}

int64_t DisplayOperationExecutor::add_current_stage_elapsed_(int64_t now_us) {
  const auto bucket = stage_to_performance_bucket(this->performance_stage_);
  const int64_t elapsed_us = now_us - this->performance_stage_start_us_;
  this->current_performance_metrics_.buckets_us[static_cast<uint8_t>(bucket)] += elapsed_us;
  return elapsed_us;
}

void DisplayOperationExecutor::measure_render_update_() {
  if (!this->performance_metrics_active_) {
    this->host_.render_update();
    return;
  }

  int64_t now_us = esp_timer_get_time();
  const auto bucket = stage_to_performance_bucket(this->performance_stage_);
  int64_t elapsed_us = this->add_current_stage_elapsed_(now_us);
  this->log_performance_stage_(bucket, elapsed_us);
  const int64_t start_us = now_us;
  this->host_.render_update();
  now_us = esp_timer_get_time();
  elapsed_us = now_us - start_us;
  this->current_performance_metrics_.buckets_us[static_cast<uint8_t>(DisplayPerformanceBucket::RENDER)] += elapsed_us;
  this->log_performance_stage_(DisplayPerformanceBucket::RENDER, elapsed_us);
  this->performance_stage_start_us_ = now_us;
}

UpdateRegion DisplayOperationExecutor::measure_detect_changed_region_() {
  if (!this->performance_metrics_active_) {
    return this->host_.detect_changed_region();
  }

  int64_t now_us = esp_timer_get_time();
  const auto bucket = stage_to_performance_bucket(this->performance_stage_);
  int64_t elapsed_us = this->add_current_stage_elapsed_(now_us);
  this->log_performance_stage_(bucket, elapsed_us);
  const int64_t start_us = now_us;
  UpdateRegion changed = this->host_.detect_changed_region();
  now_us = esp_timer_get_time();
  elapsed_us = now_us - start_us;
  this->current_performance_metrics_.buckets_us[static_cast<uint8_t>(DisplayPerformanceBucket::CHANGE_DETECT)] +=
      elapsed_us;
  this->log_performance_stage_(DisplayPerformanceBucket::CHANGE_DETECT, elapsed_us);
  this->performance_stage_start_us_ = now_us;
  return changed;
}

uint8_t DisplayOperationExecutor::performance_log_level_() const {
#if defined(USE_LOGGER) && ESPHOME_LOG_LEVEL >= ESPHOME_LOG_LEVEL_DEBUG
  auto *global_logger = ::esphome::logger::global_logger;
  return global_logger != nullptr ? global_logger->level_for(PERFORMANCE_TAG) : ESPHOME_LOG_LEVEL_NONE;
#else
  return ESPHOME_LOG_LEVEL_NONE;
#endif
}

void DisplayOperationExecutor::log_performance_metrics_(const DisplayPerformanceMetrics &metrics) const {
  const char *path = "full";
  if (metrics.type == DisplayOperationType::SLEEP) {
    path = "sleep";
  } else if (!metrics.use_full_frame) {
    path = "region";
  }

  char region[48] = "";
  if (!metrics.use_full_frame && metrics.type != DisplayOperationType::SLEEP) {
    std::snprintf(region, sizeof(region), " region=%d,%d,%d,%d", metrics.region_x, metrics.region_y,
                  metrics.region_width, metrics.region_height);
  }

  ESP_LOGD(PERFORMANCE_TAG,
           "perf %s %s result=%s%s total=%lldms init=%lldms render=%lldms detect=%lldms transfer_left=%lldms "
           "transfer_right=%lldms arm_unchanged=%lldms power_on=%lldms wait_power_on=%lldms "
           "pre_refresh_delay=%lldms refresh=%lldms wait_refresh=%lldms power_off=%lldms wait_power_off=%lldms "
           "deep_sleep=%lldms post_refresh_delay=%lldms finish=%lldms",
           operation_type_to_string(metrics.type), path, operation_result_to_string(metrics.result), region,
           to_ms(metrics.total_us), to_ms(bucket_us(metrics, DisplayPerformanceBucket::INIT)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::RENDER)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::CHANGE_DETECT)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::TRANSFER_LEFT)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::TRANSFER_RIGHT)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::ARM_UNCHANGED)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::POWER_ON)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::WAIT_POWER_ON)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::PRE_REFRESH_DELAY)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::REFRESH)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::WAIT_REFRESH)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::POWER_OFF)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::WAIT_POWER_OFF)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::DEEP_SLEEP)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::POST_REFRESH_DELAY)),
           to_ms(bucket_us(metrics, DisplayPerformanceBucket::FINISH)));
}

void DisplayOperationExecutor::log_performance_stage_(DisplayPerformanceBucket bucket, int64_t elapsed_us) const {
  if (!this->performance_live_tracking_active_) {
    return;
  }

  const char *path = "full";
  if (this->active_operation_.type == DisplayOperationType::SLEEP) {
    path = "sleep";
  } else if (!this->active_operation_.use_full_frame) {
    path = "region";
  }

  ESP_LOGV(PERFORMANCE_TAG, "perf stage %s %s %s=%lldms total=%lldms",
           operation_type_to_string(this->active_operation_.type), path, performance_bucket_to_string(bucket),
           to_ms(elapsed_us), to_ms(esp_timer_get_time() - this->performance_operation_start_us_));
}

bool DisplayOperationExecutor::is_in_hw_refresh_stage_() const {
  switch (this->active_operation_.stage) {
    case DisplayOperationStage::POWER_ON:
    case DisplayOperationStage::WAIT_POWER_ON:
    case DisplayOperationStage::PRE_REFRESH_DELAY:
    case DisplayOperationStage::REFRESH_SCREEN:
    case DisplayOperationStage::WAIT_REFRESH:
    case DisplayOperationStage::POWER_OFF:
    case DisplayOperationStage::WAIT_POWER_OFF:
    case DisplayOperationStage::DEEP_SLEEP:
    case DisplayOperationStage::POST_REFRESH_DELAY:
      return true;
    default:
      return false;
  }
}

// ---------------------------------------------------------------------------
// Step dispatcher
// ---------------------------------------------------------------------------

void DisplayOperationExecutor::process_display_operation_step_() {
  switch (this->active_operation_.stage) {
    case DisplayOperationStage::INIT:
      this->process_init_stage_();
      break;
    case DisplayOperationStage::TRANSFER_LEFT_HALF:
      this->process_transfer_left_half_stage_();
      break;
    case DisplayOperationStage::TRANSFER_RIGHT_HALF:
      this->process_transfer_right_half_stage_();
      break;
    case DisplayOperationStage::TRANSFER_LEFT_REGION:
      this->process_transfer_left_region_stage_();
      break;
    case DisplayOperationStage::TRANSFER_RIGHT_REGION:
      this->process_transfer_right_region_stage_();
      break;
    case DisplayOperationStage::ARM_UNCHANGED_HALVES:
      this->process_arm_unchanged_halves_stage_();
      break;
    case DisplayOperationStage::POWER_ON:
      this->process_power_on_stage_();
      break;
    case DisplayOperationStage::WAIT_POWER_ON:
      this->process_wait_power_on_stage_();
      break;
    case DisplayOperationStage::PRE_REFRESH_DELAY:
      this->process_pre_refresh_delay_stage_();
      break;
    case DisplayOperationStage::REFRESH_SCREEN:
      this->process_refresh_screen_stage_();
      break;
    case DisplayOperationStage::WAIT_REFRESH:
      this->process_wait_refresh_stage_();
      break;
    case DisplayOperationStage::POWER_OFF:
      this->process_power_off_stage_();
      break;
    case DisplayOperationStage::WAIT_POWER_OFF:
      this->process_wait_power_off_stage_();
      break;
    case DisplayOperationStage::DEEP_SLEEP:
      this->process_deep_sleep_stage_();
      break;
    case DisplayOperationStage::POST_REFRESH_DELAY:
      this->process_post_refresh_delay_stage_();
      break;
    case DisplayOperationStage::FINISHING:
      this->finish_display_operation_();
      break;
  }
}

// ---------------------------------------------------------------------------
// Per-stage step handlers
// ---------------------------------------------------------------------------

void DisplayOperationExecutor::process_init_stage_() {
  if (this->active_operation_.type == DisplayOperationType::SLEEP) {
    this->transition_to_stage_(DisplayOperationStage::DEEP_SLEEP);
    return;
  }

  if (!this->host_.ensure_display_ready()) {
    ESP_LOGE(OPERATION_TAG, "Display not ready, aborting %s operation",
             operation_type_to_string(this->active_operation_.type));
    this->abort_display_operation_();
    return;
  }

  const bool needs_render = (this->active_operation_.type == DisplayOperationType::UPDATE ||
                             this->active_operation_.type == DisplayOperationType::UPDATE_REGION);

  if (needs_render) {
    if (this->change_detection_mode_ == ChangeDetectionMode::TRACK) {
      this->host_.reset_change_tracking();
    }
    this->measure_render_update_();
  }

  if (this->active_operation_.type == DisplayOperationType::UPDATE && this->refresh_mode_ == RefreshMode::PARTIAL) {
    UpdateRegion changed = this->measure_detect_changed_region_();
    if (changed.empty()) {
      ESP_LOGI(OPERATION_TAG, "No changes detected — skipping refresh operation");
      this->finish_performance_metrics_(DisplayOperationResult::SKIPPED);
      this->active_operation_ = {};
      return;
    }
    if (changed.x <= 0 && changed.y <= 0 && changed.width >= EPD_WIDTH && changed.height >= EPD_HEIGHT) {
      this->active_operation_.use_full_frame = true;
    } else {
      this->active_operation_.region_x = changed.x;
      this->active_operation_.region_y = changed.y;
      this->active_operation_.region_width = changed.width;
      this->active_operation_.region_height = changed.height;
      this->active_operation_.use_full_frame = false;
    }
  } else if (this->active_operation_.type == DisplayOperationType::UPDATE_REGION ||
             this->active_operation_.type == DisplayOperationType::REFRESH_REGION) {
    this->active_operation_.use_full_frame = false;
  } else {
    this->active_operation_.use_full_frame = true;
  }

  if (this->active_operation_.use_full_frame) {
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::TRANSFER_LEFT_HALF);
  } else {
    this->controller_.compute_partial_regions(
        this->active_operation_.region_x, this->active_operation_.region_y, this->active_operation_.region_width,
        this->active_operation_.region_height, this->active_operation_.regions, this->active_operation_.has_region);

    if (!this->active_operation_.has_region[0] && !this->active_operation_.has_region[1]) {
      ESP_LOGW(OPERATION_TAG, "Partial refresh operation skipped: rectangle outside panel");
      this->finish_performance_metrics_(DisplayOperationResult::SKIPPED);
      this->active_operation_ = {};
      return;
    }
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::TRANSFER_LEFT_REGION);
  }
}

void DisplayOperationExecutor::process_transfer_left_half_stage_() {
  if (this->active_operation_.current_row == 0) {
    if (!this->controller_.begin_half_transfer(0)) {
      ESP_LOGE(OPERATION_TAG, "Left-half transfer begin failed");
      this->abort_display_operation_();
      return;
    }
  }

  const int remaining = EPD_HEIGHT - this->active_operation_.current_row;
  const int to_send = remaining < OPERATION_ROWS_PER_STEP ? remaining : OPERATION_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_half_row(this->host_.framebuffer(), this->active_operation_.current_row, 0)) {
      ESP_LOGE(OPERATION_TAG, "Left-half transfer row %d write failed", this->active_operation_.current_row);
      this->abort_display_operation_();
      return;
    }
    this->active_operation_.current_row++;
  }

  if (this->active_operation_.current_row >= EPD_HEIGHT) {
    this->controller_.end_half_transfer();
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::TRANSFER_RIGHT_HALF);
  }
}

void DisplayOperationExecutor::process_transfer_right_half_stage_() {
  if (this->active_operation_.current_row == 0) {
    if (!this->controller_.begin_half_transfer(1)) {
      ESP_LOGE(OPERATION_TAG, "Right-half transfer begin failed");
      this->abort_display_operation_();
      return;
    }
  }

  const int remaining = EPD_HEIGHT - this->active_operation_.current_row;
  const int to_send = remaining < OPERATION_ROWS_PER_STEP ? remaining : OPERATION_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_half_row(this->host_.framebuffer(), this->active_operation_.current_row, 1)) {
      ESP_LOGE(OPERATION_TAG, "Right-half transfer row %d write failed", this->active_operation_.current_row);
      this->abort_display_operation_();
      return;
    }
    this->active_operation_.current_row++;
  }

  if (this->active_operation_.current_row >= EPD_HEIGHT) {
    this->controller_.end_half_transfer();
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::POWER_ON);
  }
}

void DisplayOperationExecutor::process_transfer_left_region_stage_() {
  if (!this->active_operation_.has_region[0]) {
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::TRANSFER_RIGHT_REGION);
    return;
  }

  const PartialRegion &region = this->active_operation_.regions[0];

  if (this->active_operation_.current_row == 0) {
    if (!this->controller_.begin_region_transfer(region)) {
      ESP_LOGE(OPERATION_TAG, "Left-region transfer begin failed");
      this->abort_display_operation_();
      return;
    }
  }

  const int remaining = static_cast<int>(region.height) - this->active_operation_.current_row;
  const int to_send = remaining < OPERATION_ROWS_PER_STEP ? remaining : OPERATION_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_region_row(this->host_.framebuffer(), region, this->active_operation_.current_row)) {
      ESP_LOGE(OPERATION_TAG, "Left-region transfer row %d write failed", this->active_operation_.current_row);
      this->controller_.end_region_transfer(region.cs_index);
      this->abort_display_operation_();
      return;
    }
    this->active_operation_.current_row++;
  }

  if (this->active_operation_.current_row >= static_cast<int>(region.height)) {
    this->controller_.end_region_transfer(region.cs_index);
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::TRANSFER_RIGHT_REGION);
  }
}

void DisplayOperationExecutor::process_transfer_right_region_stage_() {
  if (!this->active_operation_.has_region[1]) {
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::ARM_UNCHANGED_HALVES);
    return;
  }

  const PartialRegion &region = this->active_operation_.regions[1];

  if (this->active_operation_.current_row == 0) {
    if (!this->controller_.begin_region_transfer(region)) {
      ESP_LOGE(OPERATION_TAG, "Right-region transfer begin failed");
      this->abort_display_operation_();
      return;
    }
  }

  const int remaining = static_cast<int>(region.height) - this->active_operation_.current_row;
  const int to_send = remaining < OPERATION_ROWS_PER_STEP ? remaining : OPERATION_ROWS_PER_STEP;
  for (int i = 0; i < to_send; i++) {
    if (!this->controller_.write_region_row(this->host_.framebuffer(), region, this->active_operation_.current_row)) {
      ESP_LOGE(OPERATION_TAG, "Right-region transfer row %d write failed", this->active_operation_.current_row);
      this->controller_.end_region_transfer(region.cs_index);
      this->abort_display_operation_();
      return;
    }
    this->active_operation_.current_row++;
  }

  if (this->active_operation_.current_row >= static_cast<int>(region.height)) {
    this->controller_.end_region_transfer(region.cs_index);
    this->active_operation_.current_row = 0;
    this->transition_to_stage_(DisplayOperationStage::ARM_UNCHANGED_HALVES);
  }
}

void DisplayOperationExecutor::process_arm_unchanged_halves_stage_() {
  for (uint8_t ic = 0; ic < 2; ic++) {
    if (this->active_operation_.has_region[ic]) {
      continue;
    }
    if (!this->controller_.arm_dummy_region(ic)) {
      ESP_LOGE(OPERATION_TAG, "Unchanged-half arm IC%u failed", ic);
      this->abort_display_operation_();
      return;
    }
  }
  this->transition_to_stage_(DisplayOperationStage::POWER_ON);
}

void DisplayOperationExecutor::process_power_on_stage_() {
  if (!this->controller_.send_refresh_pon()) {
    ESP_LOGE(OPERATION_TAG, "Refresh power-on command failed");
    this->abort_display_operation_();
    return;
  }
  this->transition_to_stage_(DisplayOperationStage::WAIT_POWER_ON);
}

void DisplayOperationExecutor::process_wait_power_on_stage_() {
  if (this->controller_.is_display_busy()) {
    return;
  }
  this->active_operation_.stage_start_us = esp_timer_get_time();
  this->transition_to_stage_(DisplayOperationStage::PRE_REFRESH_DELAY);
}

void DisplayOperationExecutor::process_pre_refresh_delay_stage_() {
  if ((esp_timer_get_time() - this->active_operation_.stage_start_us) < PRE_REFRESH_DELAY_US) {
    return;
  }
  this->transition_to_stage_(DisplayOperationStage::REFRESH_SCREEN);
}

void DisplayOperationExecutor::process_refresh_screen_stage_() {
  if (!this->controller_.send_refresh_drf()) {
    ESP_LOGE(OPERATION_TAG, "Refresh command failed");
    this->abort_display_operation_();
    return;
  }
  this->active_operation_.stage_start_us = esp_timer_get_time();
  this->transition_to_stage_(DisplayOperationStage::WAIT_REFRESH);
}

void DisplayOperationExecutor::process_wait_refresh_stage_() {
  if (this->controller_.is_display_busy()) {
    if ((esp_timer_get_time() - this->active_operation_.stage_start_us) > REFRESH_BUSY_TIMEOUT_US) {
      ESP_LOGE(OPERATION_TAG, "Refresh timeout — BUSY never released");
      this->abort_display_operation_();
    }
    return;
  }
  this->transition_to_stage_(DisplayOperationStage::POWER_OFF);
}

void DisplayOperationExecutor::process_power_off_stage_() {
  if (!this->controller_.send_refresh_pof()) {
    ESP_LOGE(OPERATION_TAG, "Refresh power-off command failed");
    this->abort_display_operation_();
    return;
  }
  this->transition_to_stage_(DisplayOperationStage::WAIT_POWER_OFF);
}

void DisplayOperationExecutor::process_wait_power_off_stage_() {
  if (this->controller_.is_display_busy()) {
    return;
  }
  this->active_operation_.stage_start_us = esp_timer_get_time();
  this->transition_to_stage_(this->auto_sleep_ ? DisplayOperationStage::DEEP_SLEEP
                                               : DisplayOperationStage::POST_REFRESH_DELAY);
}

void DisplayOperationExecutor::process_deep_sleep_stage_() {
  if (this->controller_.is_display_busy()) {
    return;
  }

  if (!this->active_operation_.use_full_frame) {
    this->controller_.disable_partial_regions();
  }
  if (!this->controller_.send_deep_sleep()) {
    ESP_LOGE(OPERATION_TAG, "Deep sleep command failed");
    this->abort_display_operation_();
    return;
  }

  this->controller_.reset();
  this->sleeping_ = true;
  if (this->power_off_after_sleep_) {
    this->transport_.set_load_switch(0);
  }
  this->active_operation_.stage_start_us = esp_timer_get_time();
  this->transition_to_stage_(this->active_operation_.type == DisplayOperationType::SLEEP
                                 ? DisplayOperationStage::FINISHING
                                 : DisplayOperationStage::POST_REFRESH_DELAY);
}

void DisplayOperationExecutor::process_post_refresh_delay_stage_() {
  const int64_t delay_us =
      this->active_operation_.use_full_frame ? POST_FULL_REFRESH_DELAY_US : POST_REGION_REFRESH_DELAY_US;
  if ((esp_timer_get_time() - this->active_operation_.stage_start_us) < delay_us) {
    return;
  }
  this->transition_to_stage_(DisplayOperationStage::FINISHING);
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
