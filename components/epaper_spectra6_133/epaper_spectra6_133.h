#pragma once

/**
 * @file epaper_spectra6_133.h
 * @brief ESPHome display component for 13.3-inch Spectra 6 e-paper panels.
 *
 * This header defines the main ESPHome-facing component class that ties together
 * the transport, controller, framebuffer, and operation executor layers.
 *
 * Panel: 1200x1600 pixels, 6 colours (Black/White/Yellow/Red/Blue/Green),
 * 4 bits per pixel packed into 960 KB of framebuffer.  Two driver ICs each
 * handle 600 columns, accessed via manual chip-select toggling over SPI.
 *
 * @see epaper_spectra6_133_constants.h  - geometry, palette codes, and register addresses
 * @see epaper_spectra6_133_transport.h  - SPI and GPIO transport layer
 * @see epaper_spectra6_133_controller.h - panel register sequencing and refresh logic
 * @see epaper_spectra6_133_framebuffer.h - pixel packing and colour mapping
 * @see epaper_spectra6_133_operation.h  - cooperative display operation state machine
 */

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "esphome/components/display/display_buffer.h"

#include "epaper_spectra6_133_operation.h"
#include "epaper_spectra6_133_version.h"

namespace esphome {
namespace epaper_spectra6_133 {

/**
 * ESPHome display component for 13.3-inch Spectra 6 e-paper panels.
 *
 * The component keeps a single full-screen framebuffer in ESPHome's logical
 * coordinate space. The controller layer later remaps each row into the two
 * 600-column driver halves expected by the panel.
 *
 * Lifecycle:
 *   setup()  - allocates the framebuffer and configures GPIO/SPI/EPD.
 *   update() - schedules a full-frame update operation; progressed from loop().
 *   loop()   - progresses the active display operation one bounded step at a time.
 */
class EpaperSpectra6133 : public display::DisplayBuffer, private DisplayOperationHost, private PanelRuntimeHooks {
 public:
  // Panel colour palette constants - the six colours supported by the Spectra 6
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
  void set_change_detection_mode(ChangeDetectionMode mode) { this->executor_.set_change_detection_mode(mode); }
  /** @brief Sets whether update() performs full-frame or auto-partial refresh. */
  void set_refresh_mode(RefreshMode mode) { this->executor_.set_refresh_mode(mode); }
  /**
   * @brief Deprecated compatibility wrapper for set_refresh_mode().
   *
   * @deprecated Use set_refresh_mode() instead. This method will be removed in version 1.0.
   */
  [[deprecated("Use set_refresh_mode(); set_update_mode() will be removed in version 1.0.")]]
  void set_update_mode(RefreshMode mode);
  /** @brief Sets the colour used by clear() and ESPHome's automatic pre-render clear. */
  void set_clear_color(Color color) { this->clear_color_ = color; }
  /**
   * @brief Sets whether successful refreshes automatically enter deep sleep.
   *
   * @param auto_sleep  True to send the panel deep-sleep command after each
   * successful refresh; false to leave the panel awake until sleep() is called.
   */
  void set_auto_sleep(bool auto_sleep) { this->executor_.set_auto_sleep(auto_sleep); }
  /**
   * @brief Sets whether deep sleep also disables the external panel load switch.
   *
   * @param power_off_after_sleep  True to drive power_pin low after the panel
   * enters deep sleep; false to leave the panel power rail enabled.
   */
  void set_power_off_after_sleep(bool power_off_after_sleep) {
    this->executor_.set_power_off_after_sleep(power_off_after_sleep);
  }

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
   * When refresh_mode is PARTIAL, the component detects the changed region
   * and refreshes only that area.  When FULL (default), the entire
   * framebuffer is transferred.
   *
   * If another display operation is already in progress it is superseded:
   * the previous operation is cancelled and this one takes its place.
   */
  void update() override;
  /** @brief Progresses any active display operation by one bounded step. */
  void loop() override;
  /** @brief Fills the logical framebuffer with one ESPHome color without refreshing immediately. */
  void fill(Color color) override;
  /** @brief Reports that this display exposes a multi-colour output surface. */
  display::DisplayType get_display_type() override { return display::DISPLAY_TYPE_COLOR; }

  /** @brief Fills the framebuffer with the configured clear colour without refreshing. */
  void clear() override;

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
   * @brief Schedules a full framebuffer refresh and returns immediately.
   *
   * The display lambda is NOT re-run.  Existing framebuffer contents are
   * transferred and the panel is refreshed cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded.
   */
  void refresh();

  /**
   * @brief Deprecated compatibility wrapper for refresh().
   *
   * @deprecated Use refresh() instead. This method will be removed in version 1.0.
   */
  [[deprecated("Use refresh(); flush() will be removed in version 1.0.")]]
  void flush();

  /**
   * @brief Schedules a partial framebuffer refresh and returns immediately.
   *
   * The display lambda is NOT re-run.  Only the specified region is
   * transferred and refreshed cooperatively from loop().
   *
   * If another display operation is already in progress it is superseded.
   *
   * @param x, y, width, height  Logical panel rectangle (pixels).
   */
  void refresh_region(int x, int y, int width, int height);

  /**
   * @brief Deprecated compatibility wrapper for refresh_region().
   *
   * @deprecated Use refresh_region() instead. This method will be removed in version 1.0.
   */
  [[deprecated("Use refresh_region(); flush_region() will be removed in version 1.0.")]]
  void flush_region(int x, int y, int width, int height);

  /** @brief Returns whether the panel hardware has been initialized and is ready for explicit updates. */
  bool is_ready() const { return this->controller_.is_initialized(); }
  /**
   * @brief Returns whether the panel was successfully placed in deep sleep.
   *
   * @return True after the panel has entered deep sleep; false while awake or
   * while a sleep request is still pending.
   */
  bool is_sleeping() const { return this->executor_.is_sleeping(); }

  // -------------------------------------------------------------------------
  // Change detection
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
  UpdateRegion detect_changed_region() const override;

  /** @brief Resets the tracked change region to empty. */
  void reset_change_tracking() override;

  // -------------------------------------------------------------------------
  // Display pipeline control
  // -------------------------------------------------------------------------

  /**
   * @brief Returns true while any display operation is scheduled, running, or draining.
   *
   * Returns false only when the display pipeline is fully idle: no operation is
   * active and no pending operation is waiting to start.
   */
  bool is_processing() const { return this->executor_.is_processing(); }

  /**
   * @brief Deprecated compatibility wrapper for is_processing().
   *
   * @deprecated Use is_processing() instead. This method will be removed in version 1.0.
   */
  [[deprecated("Use is_processing(); is_busy() will be removed in version 1.0.")]]
  bool is_busy() const;

  /**
   * @brief Cancels the current or pending display operation.
   *
   * If no operation is active and no operation is pending, this is a safe no-op.
   *
   * If an operation is scheduled but not yet physically refreshing the panel, it is
   * cancelled at the next loop() call boundary.
   *
   * If the panel is physically inside a hardware refresh stage (BUSY pin LOW),
   * the current operation is marked as cancelling and drains safely before teardown.
   * Any pending operation queued behind it is also discarded.
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
  bool initialize();
  bool ensure_initialized_();
  void update_previous_frame_(const DisplayOperation &operation);

  // DisplayOperationHost implementation.
  bool ensure_display_ready() override;
  void render_update() override;
  void on_operation_finished(const DisplayOperation &operation) override;
  uint8_t *framebuffer() override;

  // PanelRuntimeHooks implementation.
  void yield_for_milliseconds(uint32_t ms) override;

  Transport transport_;
  Controller controller_;
  DisplayOperationExecutor executor_;
  Color clear_color_{WHITE};
  UpdateRegion tracked_region_{};
  uint8_t *previous_frame_buffer_{nullptr};
  uint32_t draw_pixels_since_yield_{0};

  // Allow the host-test fixture to access private members for state verification.
  friend class EpaperSpectra6133ComponentTest;
};

}  // namespace epaper_spectra6_133
}  // namespace esphome
