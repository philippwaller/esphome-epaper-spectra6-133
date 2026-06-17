#pragma once

/**
 * @file epaper_spectra6_133_operation.h
 * @brief Cooperative display operation state machine.
 *
 * The DisplayOperationExecutor drives display operations (update, refresh,
 * sleep) one bounded step at a time through its step() method. It communicates
 * with the integration layer through DisplayOperationHost.
 */

#include <cstdint>

#include "epaper_spectra6_133_constants.h"
#include "epaper_spectra6_133_controller.h"
#include "epaper_spectra6_133_framebuffer.h"

namespace esphome {
namespace epaper_spectra6_133 {

/** @brief Controls how the component determines which pixels have changed. */
enum class ChangeDetectionMode {
  TRACK,    // Track pixel writes through draw_absolute_pixel_internal.
  COMPARE,  // Compare the current framebuffer against the previous displayed frame.
};

/** @brief Controls whether update() performs full-frame or auto-partial refresh. */
enum class RefreshMode {
  FULL,     // Always transfer the full framebuffer.
  PARTIAL,  // Automatically detect the changed region and refresh only that.
};

/**
 * @brief Deprecated compatibility alias for RefreshMode.
 *
 * @deprecated Use RefreshMode instead. This alias will be removed in version 1.0.
 */
using UpdateMode [[deprecated("Use RefreshMode; UpdateMode will be removed in version 1.0.")]] = RefreshMode;

// ---------------------------------------------------------------------------
// Display operation types and execution stages.
// ---------------------------------------------------------------------------

/** @brief Identifies which display operation is being performed. */
enum class DisplayOperationType {
  NONE,
  UPDATE,          // Full update: render + full-frame or auto-partial transfer.
  UPDATE_REGION,   // Partial update: render + region transfer.
  REFRESH,         // Full refresh: full-frame transfer without rendering.
  REFRESH_REGION,  // Partial refresh: region transfer without rendering.
  SLEEP,           // Enter panel deep sleep without modifying the framebuffer.
};

/**
 * @brief Stage of an in-progress display operation.
 *
 * Stages are progressed in sequence from the executor's step() method.
 * Instantaneous stages (POWER_ON, REFRESH_SCREEN, POWER_OFF) transition immediately;
 * polling stages (WAIT_*) return without advancing until the BUSY pin
 * is released; delay stages (PRE_REFRESH_DELAY, POST_REFRESH_DELAY) use esp_timer_get_time()
 * to achieve non-blocking waits.
 */
enum class DisplayOperationStage {
  // Setup
  INIT,  // Run do_update_() if needed; resolve path; compute PartialRegions.
  // Full-frame transfer
  TRANSFER_LEFT_HALF,   // DTM + row streaming to IC0 (OPERATION_ROWS_PER_STEP rows/call).
  TRANSFER_RIGHT_HALF,  // DTM + row streaming to IC1.
  // Region transfer
  TRANSFER_LEFT_REGION,   // CMD66+PTLW+DTM+rows for IC0 (skipped if !has_region[0]).
  TRANSFER_RIGHT_REGION,  // CMD66+PTLW+DTM+rows for IC1 (skipped if !has_region[1]).
  ARM_UNCHANGED_HALVES,   // Arm dummy PTLWs for ICs with no changed data.
  // Shared refresh sequence
  POWER_ON,            // Send PON command; transition immediately.
  WAIT_POWER_ON,       // Poll is_display_busy(); yield until BUSY goes high.
  PRE_REFRESH_DELAY,   // Non-blocking 30 ms post-PON delay before DRF.
  REFRESH_SCREEN,      // Send DRF command; transition immediately.
  WAIT_REFRESH,        // Poll is_display_busy(); yield until BUSY goes high (up to 20 s).
  POWER_OFF,           // Send POF command; transition immediately.
  WAIT_POWER_OFF,      // Poll is_display_busy(); yield until BUSY goes high.
  DEEP_SLEEP,          // Send DSLP+0xA5 once BUSY is idle-high.
  POST_REFRESH_DELAY,  // Non-blocking post-refresh delay (10 ms full / 300 ms region).
  // Finalization
  FINISHING,  // disable_partial_regions (region operations), update previous frame, clear operation.
};

/** @brief Lifecycle state of a display operation. */
enum class DisplayOperationState {
  IDLE,        // No operation is scheduled or running.
  RUNNING,     // An operation is actively being progressed from step().
  CANCELLING,  // Operation is marked for cancellation; draining before teardown.
};

/** @brief Final outcome recorded for a measured display operation. */
enum class DisplayOperationResult {
  NONE,
  COMPLETED,
  SKIPPED,
  ABORTED,
};

/** @brief Aggregated performance buckets for one display operation. */
enum class DisplayPerformanceBucket : uint8_t {
  INIT,
  RENDER,
  CHANGE_DETECT,
  TRANSFER_LEFT,
  TRANSFER_RIGHT,
  ARM_UNCHANGED,
  POWER_ON,
  WAIT_POWER_ON,
  PRE_REFRESH_DELAY,
  REFRESH,
  WAIT_REFRESH,
  POWER_OFF,
  WAIT_POWER_OFF,
  DEEP_SLEEP,
  POST_REFRESH_DELAY,
  FINISH,
  COUNT,
};

/** @brief Last-operation performance summary used by logs and host tests. */
struct DisplayPerformanceMetrics {
  bool valid{false};
  DisplayOperationType type{DisplayOperationType::NONE};
  DisplayOperationResult result{DisplayOperationResult::NONE};
  bool use_full_frame{true};
  int region_x{0};
  int region_y{0};
  int region_width{0};
  int region_height{0};
  int64_t total_us{0};
  int64_t buckets_us[static_cast<uint8_t>(DisplayPerformanceBucket::COUNT)]{};
};

/**
 * @brief Internal state for one scheduled display operation.
 *
 * Kept small: only the fields needed to resume the current stage across
 * multiple step() invocations are stored here.
 */
struct DisplayOperation {
  DisplayOperationType type{DisplayOperationType::NONE};
  DisplayOperationStage stage{DisplayOperationStage::INIT};
  DisplayOperationState state{DisplayOperationState::IDLE};
  // Region parameters (UPDATE_REGION, REFRESH_REGION; also set by auto-partial UPDATE).
  int region_x{0};
  int region_y{0};
  int region_width{0};
  int region_height{0};
  // Resolved at INIT: true -> full-frame stages; false -> region stages.
  bool use_full_frame{true};
  // Computed at INIT for region-path operations.
  PartialRegion regions[2];
  bool has_region[2]{false, false};
  // Row streaming progress (reset to 0 on each FF/RG stage entry).
  int current_row{0};
  // Microsecond timestamp recorded when entering PRE_REFRESH_DELAY or POST_REFRESH_DELAY.
  int64_t stage_start_us{0};
};

/**
 * @brief Integration boundary implemented by the ESPHome-facing component.
 *
 * The executor owns scheduling and panel sequencing. The host owns framework
 * rendering, framebuffer lifetime, hardware readiness policy, and post-refresh
 * dirty-region bookkeeping.
 */
class DisplayOperationHost {
 public:
  virtual ~DisplayOperationHost() = default;
  /** @brief Ensures the display hardware is initialized and ready for commands. */
  virtual bool ensure_display_ready() = 0;
  /** @brief Renders display content synchronously into the framebuffer. */
  virtual void render_update() = 0;
  /** @brief Returns the currently detected changed region. */
  virtual UpdateRegion detect_changed_region() const = 0;
  /** @brief Clears host-side change tracking state. */
  virtual void reset_change_tracking() = 0;
  /** @brief Finalizes host-side state after a successful operation. */
  virtual void on_operation_finished(const DisplayOperation &operation) = 0;
  /** @brief Returns the externally owned framebuffer pointer. */
  virtual uint8_t *framebuffer() = 0;
};

/**
 * @brief Holds parameters for an operation requested while the panel
 *        was physically committed to a hardware refresh cycle.
 */
struct PendingDisplayOperation {
  bool has_pending{false};
  DisplayOperationType type{DisplayOperationType::NONE};
  int x{0};
  int y{0};
  int w{0};
  int h{0};
};

/**
 * @brief Cooperative display operation state machine.
 *
 * Drives display update/refresh/sleep operations one bounded step at a time.
 * Communicates with the integration layer through DisplayOperationHost.
 * Owns the operation state (active + pending) and the sleeping flag.
 */
class DisplayOperationExecutor {
 public:
  DisplayOperationExecutor(Controller &controller, Transport &transport, DisplayOperationHost &host)
      : controller_(controller), transport_(transport), host_(host) {}

  // Configuration setters
  void set_change_detection_mode(ChangeDetectionMode mode) { this->change_detection_mode_ = mode; }
  void set_refresh_mode(RefreshMode mode) { this->refresh_mode_ = mode; }
  void set_auto_sleep(bool auto_sleep) { this->auto_sleep_ = auto_sleep; }
  void set_power_off_after_sleep(bool power_off) { this->power_off_after_sleep_ = power_off; }

  // Configuration getters
  ChangeDetectionMode change_detection_mode() const { return this->change_detection_mode_; }
  RefreshMode refresh_mode() const { return this->refresh_mode_; }
  bool auto_sleep() const { return this->auto_sleep_; }
  bool power_off_after_sleep() const { return this->power_off_after_sleep_; }

  /**
   * @brief Progresses the active display operation by one bounded step.
   *
   * Returns immediately when no operation is active.
   */
  void step();

  /**
   * @brief Validates readiness and schedules a display operation.
   *
   * If the panel is in a hardware refresh stage, queues the operation as pending.
   */
  bool request_operation(DisplayOperationType type, int x, int y, int w, int h);

  /** @brief Cancels the current or pending display operation. */
  void cancel();

  /** @brief Schedules a sleep operation (caller must check sleeping/failed state). */
  void schedule_sleep();

  /** @brief Returns true while any operation is active or pending. */
  bool is_processing() const {
    return this->active_operation_.state != DisplayOperationState::IDLE || this->pending_operation_.has_pending;
  }

  /** @brief Returns true if the active operation is in a hardware refresh stage. */
  bool is_in_hw_refresh_stage() const { return this->is_in_hw_refresh_stage_(); }

  /** @brief Returns whether the panel has been placed in deep sleep. */
  bool is_sleeping() const { return this->sleeping_; }

  /** @brief Clears the sleeping flag (called after wake/initialize succeeds). */
  void clear_sleeping() { this->sleeping_ = false; }

  /** @brief Provides read access to the active operation for test inspection. */
  const DisplayOperation &active_operation() const { return this->active_operation_; }
  /** @brief Provides read access to the pending operation for test inspection. */
  const PendingDisplayOperation &pending_operation() const { return this->pending_operation_; }
  /** @brief Provides read access to the last captured performance metrics for host tests. */
  const DisplayPerformanceMetrics &last_performance_metrics() const { return this->last_performance_metrics_; }

 private:
  void start_display_operation_(DisplayOperationType type, int x, int y, int w, int h);
  void schedule_display_operation_(DisplayOperationType type, int x, int y, int w, int h);
  void abort_display_operation_();
  void finish_display_operation_();
  bool is_in_hw_refresh_stage_() const;
  void process_display_operation_step_();
  void transition_to_stage_(DisplayOperationStage stage);
  void start_performance_metrics_();
  void finish_performance_metrics_(DisplayOperationResult result);
  int64_t add_current_stage_elapsed_(int64_t now_us);
  void measure_render_update_();
  UpdateRegion measure_detect_changed_region_();
  uint8_t performance_log_level_() const;
  void log_performance_metrics_(const DisplayPerformanceMetrics &metrics) const;
  void log_performance_stage_(DisplayPerformanceBucket bucket, int64_t elapsed_us) const;

  // Per-stage step handlers
  void process_init_stage_();
  void process_transfer_left_half_stage_();
  void process_transfer_right_half_stage_();
  void process_transfer_left_region_stage_();
  void process_transfer_right_region_stage_();
  void process_arm_unchanged_halves_stage_();
  void process_power_on_stage_();
  void process_wait_power_on_stage_();
  void process_pre_refresh_delay_stage_();
  void process_refresh_screen_stage_();
  void process_wait_refresh_stage_();
  void process_power_off_stage_();
  void process_wait_power_off_stage_();
  void process_deep_sleep_stage_();
  void process_post_refresh_delay_stage_();

  Controller &controller_;
  Transport &transport_;
  DisplayOperationHost &host_;
  ChangeDetectionMode change_detection_mode_{ChangeDetectionMode::TRACK};
  RefreshMode refresh_mode_{RefreshMode::FULL};
  bool auto_sleep_{true};
  bool power_off_after_sleep_{false};
  bool sleeping_{false};
  DisplayOperation active_operation_{};
  PendingDisplayOperation pending_operation_{};
  bool performance_metrics_active_{false};
  bool performance_live_tracking_active_{false};
  DisplayOperationStage performance_stage_{DisplayOperationStage::INIT};
  int64_t performance_operation_start_us_{0};
  int64_t performance_stage_start_us_{0};
  DisplayPerformanceMetrics current_performance_metrics_{};
  DisplayPerformanceMetrics last_performance_metrics_{};

  friend class EpaperSpectra6133ComponentTest;
};

}  // namespace epaper_spectra6_133
}  // namespace esphome
