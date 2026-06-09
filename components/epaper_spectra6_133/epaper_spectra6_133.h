#pragma once

/**
 * @file epaper_spectra6_133.h
 * @brief ESPHome display component for 13.3-inch Spectra 6 e-paper panels.
 *
 * This header defines the main ESPHome-facing component class that ties together
 * the transport, controller, and framebuffer layers.
 *
 * Panel: 1200×1600 pixels, 6 colours (Black/White/Yellow/Red/Blue/Green),
 * 4 bits per pixel packed into 960 KB of framebuffer.  Two driver ICs each
 * handle 600 columns, accessed via manual chip-select toggling over SPI.
 *
 * @see epaper_spectra6_133_constants.h  — geometry, palette codes, and register addresses
 * @see epaper_spectra6_133_transport.h  — SPI and GPIO transport layer
 * @see epaper_spectra6_133_controller.h — panel register sequencing and refresh logic
 * @see epaper_spectra6_133_framebuffer.h — pixel packing and colour mapping
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "esphome/components/display/display_buffer.h"

#include "epaper_spectra6_133_controller.h"
#include "epaper_spectra6_133_framebuffer.h"
#include "epaper_spectra6_133_version.h"

namespace esphome {
namespace epaper_spectra6_133 {

/** @brief Controls how the component determines which pixels have changed. */
enum class ChangeDetectionMode {
  TRACK,    // Track pixel writes through draw_absolute_pixel_internal.
  COMPARE,  // Compare the current framebuffer against the previous displayed frame.
};

/** @brief Controls whether update() performs full-frame or auto-partial refresh. */
enum class UpdateMode {
  FULL,     // Always transfer the full framebuffer.
  PARTIAL,  // Automatically detect the changed region and refresh only that.
};

// ---------------------------------------------------------------------------
// Async job types and execution stages.
//
// An AsyncJob describes one scheduled display operation (update, flush, or
// their region variants).  It is progressed from loop() one bounded step at
// a time so that the ESPHome main loop is never blocked for long.
//
// Only one AsyncJob may be active at any time.  Starting a new async
// operation while one is running cancels the old job first.
// ---------------------------------------------------------------------------

/** @brief Identifies which display operation an async job is performing. */
enum class AsyncJobType {
  NONE,
  UPDATE,         // Full update: run lambda + full-frame or auto-partial transfer.
  UPDATE_REGION,  // Partial update: run lambda + region transfer.
  FLUSH,          // Full flush: full-frame transfer without running lambda.
  FLUSH_REGION,   // Partial flush: region transfer without running lambda.
  SLEEP,          // Enter panel deep sleep without modifying the framebuffer.
};

/**
 * @brief Stage of an in-progress async job.
 *
 * Stages are progressed in sequence from the component's loop() method.
 * Instantaneous stages (RF_PON, RF_DRF, RF_POF) transition immediately;
 * polling stages (RF_WAIT_*) return without advancing until the BUSY pin
 * is released; delay stages (RF_DRF_DELAY, RF_POST) use esp_timer_get_time()
 * to achieve non-blocking waits.
 */
enum class AsyncStage {
  // Setup
  INIT,  // Run do_update_() if needed; resolve path; compute PartialRegions.
  // Full-frame transfer
  FF_CS0,  // DTM + row streaming to IC0 (ASYNC_ROWS_PER_STEP rows/call).
  FF_CS1,  // DTM + row streaming to IC1.
  // Region transfer
  RG_IC0,    // CMD66+PTLW+DTM+rows for IC0 (skipped if !has_region[0]).
  RG_IC1,    // CMD66+PTLW+DTM+rows for IC1 (skipped if !has_region[1]).
  RG_DUMMY,  // Arm dummy PTLWs for ICs with no changed data.
  // Shared refresh sequence
  RF_PON,         // Send PON command; transition immediately.
  RF_WAIT_PON,    // Poll is_display_busy(); yield until BUSY goes high.
  RF_DRF_DELAY,   // Non-blocking 30 ms post-PON delay before DRF.
  RF_DRF,         // Send DRF command; transition immediately.
  RF_WAIT_DRF,    // Poll is_display_busy(); yield until BUSY goes high (up to 20 s).
  RF_POF,         // Send POF command; transition immediately.
  RF_WAIT_POF,    // Poll is_display_busy(); yield until BUSY goes high.
  SL_DEEP_SLEEP,  // Send DSLP+0xA5 once BUSY is idle-high.
  RF_POST,        // Non-blocking post-refresh delay (10 ms full / 300 ms region).
  // Finalization
  FINISHING,  // disable_partial_regions (region jobs), update previous frame, clear job.
};

/** @brief Lifecycle state of an async display job. */
enum class JobState {
  IDLE,        // No job is scheduled or running.
  RUNNING,     // A job is actively being progressed from loop().
  CANCELLING,  // Job is marked for cancellation; draining before teardown.
};

/**
 * @brief Internal state for one scheduled display operation.
 *
 * Kept small: only the fields needed to resume the current stage across
 * multiple loop() invocations are stored here.
 */
struct AsyncJob {
  AsyncJobType type{AsyncJobType::NONE};
  AsyncStage stage{AsyncStage::INIT};
  JobState state{JobState::IDLE};
  // Region parameters (UPDATE_REGION, FLUSH_REGION; also set by auto-partial UPDATE).
  int region_x{0};
  int region_y{0};
  int region_width{0};
  int region_height{0};
  // Resolved at INIT: true → FF_* stages; false → RG_* stages.
  bool use_full_frame{true};
  // Computed at INIT for region-path jobs.
  PartialRegion regions[2];
  bool has_region[2]{false, false};
  // Row streaming progress (reset to 0 on each FF/RG stage entry).
  int current_row{0};
  // Microsecond timestamp recorded when entering RF_DRF_DELAY or RF_POST.
  int64_t stage_start_us{0};
};

/**
 * @brief Holds parameters for an async job that was requested while the panel
 *        was physically committed to a hardware refresh cycle.
 *
 * When a new async job arrives while the active job is in an RF_* stage and
 * the BUSY pin is still LOW, the new job cannot start immediately — the
 * hardware cannot be interrupted.  The incoming request is stored here and
 * started automatically by abort_async_job_() once BUSY is released and
 * the draining job has been torn down.
 *
 * Only one pending job is kept at a time.  A second replacement overwrites
 * the first, consistent with the existing single-job-wins semantics.
 */
struct PendingAsyncJob {
  bool has_pending{false};
  AsyncJobType type{AsyncJobType::NONE};
  int x{0};
  int y{0};
  int w{0};
  int h{0};
};

/**
 * ESPHome display component for 13.3-inch Spectra 6 e-paper panels.
 *
 * The component keeps a single full-screen framebuffer in ESPHome's logical
 * coordinate space. The controller layer later remaps each row into the two
 * 600-column driver halves expected by the panel.
 *
 * Lifecycle:
 *   setup()  - allocates the framebuffer and configures GPIO/SPI/EPD.
 *   update() - schedules a full-frame update job; progressed from loop().
 *   loop()   - progresses the active display job one bounded step at a time.
 */
class EpaperSpectra6133 : public display::DisplayBuffer {
 public:
  // Panel colour palette constants — the six colours supported by the Spectra 6
  // hardware, expressed as ESPHome Color values for use with any drawing API.
  static constexpr Color BLACK = Color(0, 0, 0);
  static constexpr Color WHITE = Color(255, 255, 255);
  static constexpr Color YELLOW = Color(255, 255, 0);
  static constexpr Color RED = Color(255, 0, 0);
  static constexpr Color BLUE = Color(0, 0, 255);
  static constexpr Color GREEN = Color(0, 255, 0);

  /** @brief Returns the component version string. */
  static const char *get_version() { return VERSION; }

  /**
   * @brief Creates the component and binds the controller to the transport.
   *
   * Pin assignments and the SPI host are supplied later by generated
   * ESPHome configuration code before setup() runs.
   */
  EpaperSpectra6133();

  /** @brief Selects the ESP-IDF SPI host used for panel communication. */
  void set_spi_host(int host) { this->transport_.set_spi_host(host); }
  /** @brief Sets the chip-select pin for the first driver IC. */
  void set_cs0_pin(int pin) { this->transport_.set_cs0_pin(pin); }
  /** @brief Sets the chip-select pin for the second driver IC. */
  void set_cs1_pin(int pin) { this->transport_.set_cs1_pin(pin); }
  /** @brief Sets the SPI clock pin. */
  void set_clk_pin(int pin) { this->transport_.set_clk_pin(pin); }
  /** @brief Sets the primary SPI data output pin connected to DATA0. */
  void set_data0_pin(int pin) { this->transport_.set_data0_pin(pin); }
  /** @brief Sets the secondary SPI data pin connected to DATA1. */
  void set_data1_pin(int pin) { this->transport_.set_data1_pin(pin); }
  /** @brief Sets the active-low BUSY input pin. */
  void set_busy_pin(int pin) { this->transport_.set_busy_pin(pin); }
  /** @brief Sets the hardware reset pin. */
  void set_reset_pin(int pin) { this->transport_.set_reset_pin(pin); }
  /** @brief Sets the external power-supply enable pin for the panel. */
  void set_power_pin(int pin) { this->transport_.set_power_pin(pin); }
  /** @brief Sets the change detection strategy used by detect_changed_region(). */
  void set_change_detection_mode(ChangeDetectionMode mode) { this->change_detection_mode_ = mode; }
  /** @brief Sets whether update() performs full-frame or auto-partial refresh. */
  void set_update_mode(UpdateMode mode) { this->update_mode_ = mode; }
  /**
   * @brief Sets whether successful refreshes automatically enter deep sleep.
   *
   * @param auto_sleep  True to send the panel deep-sleep command after each
   * successful refresh; false to leave the panel awake until sleep() is called.
   */
  void set_auto_sleep(bool auto_sleep) { this->auto_sleep_ = auto_sleep; }
  /**
   * @brief Sets whether deep sleep also disables the external panel load switch.
   *
   * @param power_off_after_sleep  True to drive power_pin low after the panel
   * enters deep sleep; false to leave the panel power rail enabled.
   */
  void set_power_off_after_sleep(bool power_off_after_sleep) { this->power_off_after_sleep_ = power_off_after_sleep; }

  /** @brief Allocates the framebuffer and configures the hardware. */
  void setup() override;
  /** @brief Logs the active hardware configuration and driver status. */
  void dump_config() override;
  /** @brief Requests hardware-priority setup so the display is ready early in boot. */
  float get_setup_priority() const override;
  /**
   * @brief Schedules a display update and returns immediately.
   *
   * Called automatically by ESPHome on the configured update_interval.
   * Re-runs the display lambda inside loop() and transfers the resulting
   * framebuffer to the panel cooperatively across successive loop() calls.
   *
   * When update_mode is PARTIAL, the component detects the changed region
   * and refreshes only that area.  When FULL (default), the entire
   * framebuffer is transferred.
   *
   * If another display operation is already in progress it is superseded:
   * the previous job is cancelled and this one takes its place.
   */
  void update() override;
  /** @brief Progresses any active async job by one bounded step. */
  void loop() override;
  /** @brief Fills the logical framebuffer with one ESPHome color without refreshing immediately. */
  void fill(Color color) override;
  /** @brief Reports that this display exposes a multi-colour output surface. */
  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }

  /**
   * @brief Clears the framebuffer to white and schedules a full-frame flush.
   *
   * Overrides the inherited display clear behaviour so clear() uses white as
   * the panel's default background state instead of black.
   */
  void clear() override;

  /**
   * @brief Clears the framebuffer to a specific colour and schedules a full-frame flush.
   *
   * Maps @p color to the nearest panel colour, fills the framebuffer
   * uniformly, and schedules a full-frame flush job that is progressed
   * cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded:
   * the previous job is cancelled and this one takes its place.
   *
   * @param color  ESPHome Color to clear to.
   */
  void clear(Color color);

  /**
   * @brief Schedules a partial update of a logical rectangle and returns immediately.
   *
   * Re-runs the display lambda, then transfers and refreshes only the specified
   * region cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded.
   *
   * @param x, y, width, height  Logical panel rectangle (pixels).
   */
  void update_region(int x, int y, int width, int height);

  /**
   * @brief Schedules a partial update using a pre-computed region.
   *
   * Convenience overload that accepts an UpdateRegion value directly.
   * Equivalent to calling update_region(region.x, region.y, region.width, region.height).
   */
  void update_region(UpdateRegion region);

  /**
   * @brief Schedules a full framebuffer flush and returns immediately.
   *
   * The display lambda is NOT re-run.  Existing framebuffer contents are
   * transferred and the panel is refreshed cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded.
   */
  void flush();

  /**
   * @brief Schedules a partial framebuffer flush and returns immediately.
   *
   * The display lambda is NOT re-run.  Only the specified region is
   * transferred and refreshed cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded.
   *
   * @param x, y, width, height  Logical panel rectangle (pixels).
   */
  void flush_region(int x, int y, int width, int height);

  /** @brief Returns whether the panel hardware has been initialized and is ready for explicit updates. */
  bool is_ready() const { return this->controller_.is_initialized(); }
  /**
   * @brief Returns whether the panel was successfully placed in deep sleep.
   *
   * @return True after the panel has entered deep sleep; false while awake or
   * while a sleep request is still pending.
   */
  bool is_sleeping() const { return this->sleeping_; }

  // -------------------------------------------------------------------------
  // Change detection
  //
  // The component tracks which pixels have changed since the last successful
  // display update.  The detection strategy is selected via
  // change_detection_mode (track or compare).  detect_changed_region() is
  // always available regardless of update_mode.
  // -------------------------------------------------------------------------

  /**
   * @brief Returns the detected changed region based on the configured detection mode.
   *
   * In TRACK mode, returns the bounding box of all pixels written through
   * draw_absolute_pixel_internal since the last successful update.
   *
   * In COMPARE mode, compares the current framebuffer against the previous
   * displayed frame and returns the bounding box of all differing pixels.
   *
   * Returns an empty region if no changes are detected.
   */
  UpdateRegion detect_changed_region() const;

  /** @brief Resets the tracked change region to empty. */
  void reset_change_tracking();

  // -------------------------------------------------------------------------
  // Display pipeline control
  //
  // All display operations schedule a cooperative job and return immediately.
  // Only one job may be active at a time.  Starting a new operation while one
  // is in progress supersedes it: the previous job is cancelled and the new
  // one takes its place.  If the panel is physically in a hardware refresh
  // stage, the incoming job is queued as pending and starts automatically
  // once the refresh drains safely.
  //
  // Use is_busy() to check whether any display operation is scheduled,
  // running, or draining.
  // -------------------------------------------------------------------------

  /**
   * @brief Returns true while any display operation is scheduled, running, or draining.
   *
   * Returns false only when the display pipeline is fully idle: no job is
   * active and no pending job is waiting to start.
   */
  bool is_busy() const { return this->async_job_.state != JobState::IDLE || this->pending_job_.has_pending; }

  /**
   * @brief Cancels the current or pending display operation.
   *
   * If no job is active and no job is pending, this is a safe no-op.
   *
   * If a job is scheduled but not yet physically refreshing the panel, it is
   * cancelled at the next loop() call boundary.
   *
   * If the panel is physically inside a hardware refresh stage (BUSY pin LOW),
   * the current job is marked as cancelling and drains safely before teardown.
   * Any pending job queued behind it is also discarded.
   */
  void cancel();

  /**
   * @brief Schedules the panel to enter deep sleep and returns immediately.
   *
   * If a refresh is physically in progress, the sleep request waits until the
   * BUSY line is released before sending the datasheet-defined DSLP command.
   */
  void sleep();

  /**
   * @brief Wakes a sleeping panel by rerunning the full hardware init sequence.
   *
   * @return True when the display is initialized and ready for a new transfer;
   * false if initialization fails or the component is in a failed state.
   */
  bool wake();

 protected:
  /** @brief Returns the logical panel width reported to ESPHome drawing primitives. */
  int get_width_internal() override { return EPD_WIDTH; }
  /** @brief Returns the logical panel height reported to ESPHome drawing primitives. */
  int get_height_internal() override { return EPD_HEIGHT; }
  /** @brief Writes one logical pixel into the packed framebuffer. */
  void draw_absolute_pixel_internal(int x, int y, Color color) override;

 private:
  // Runs the reference initialization sequence once the transport is ready.
  bool initialize();
  // Lazily initializes the panel on the first operation that needs hardware access.
  bool ensure_initialized_();
  // Copies the current framebuffer to the previous-frame buffer (compare mode).
  void update_previous_frame_();

  // -------------------------------------------------------------------------
  // Async job management helpers.
  // -------------------------------------------------------------------------

  // Cancels any active async job and initialises a new one of the given type.
  void schedule_async_job_(AsyncJobType type, int x, int y, int w, int h);
  // Dispatches one bounded step for the current async stage.
  void process_async_step_();
  // Cleans up CS/PTLW state and clears the async job after cancellation.
  // If a pending job was queued during a refresh-stage drain, starts it.
  void abort_async_job_();
  // Cleans up PTLW state, syncs shadow, and clears the async job on completion.
  void finish_async_job_();
  // Returns true if the current job stage may have the panel physically busy
  // (BUSY pin potentially LOW).  Cancellation must wait until BUSY clears.
  bool is_in_hw_refresh_stage_() const;

  // Per-stage step handlers — each does a bounded amount of work and returns.
  void process_init_stage_();
  void process_ff_cs0_stage_();
  void process_ff_cs1_stage_();
  void process_rg_ic0_stage_();
  void process_rg_ic1_stage_();
  void process_rg_dummy_stage_();
  void process_rf_pon_stage_();
  void process_rf_wait_pon_stage_();
  void process_rf_drf_delay_stage_();
  void process_rf_drf_stage_();
  void process_rf_wait_drf_stage_();
  void process_rf_pof_stage_();
  void process_rf_wait_pof_stage_();
  void process_sl_deep_sleep_stage_();
  void process_rf_post_stage_();

  Transport transport_;
  Controller controller_;
  ChangeDetectionMode change_detection_mode_{ChangeDetectionMode::TRACK};
  UpdateMode update_mode_{UpdateMode::FULL};
  bool auto_sleep_{true};
  bool power_off_after_sleep_{false};
  bool sleeping_{false};
  UpdateRegion tracked_region_{};
  uint8_t *previous_frame_buffer_{nullptr};
  uint32_t draw_pixels_since_yield_{0};
  AsyncJob async_job_{};
  // Holds a job that arrived while the panel was in a hardware refresh stage.
  // Started automatically after the draining refresh completes.
  PendingAsyncJob pending_job_{};

  // Allow the host-test fixture to access private members for state verification.
  // TEST_F subclasses cannot use friend access directly; they call through fixture methods.
  friend class EpaperSpectra6133ComponentTest;
};

}  // namespace epaper_spectra6_133
}  // namespace esphome
