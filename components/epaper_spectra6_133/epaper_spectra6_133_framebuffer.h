#pragma once

/**
 * @file epaper_spectra6_133_framebuffer.h
 * @brief Helpers for mapping logical drawing operations into the packed panel framebuffer.
 */

#include "esphome/core/color.h"

#include "epaper_spectra6_133_constants.h"

namespace esphome {
namespace epaper_spectra6_133 {

/**
 * @brief Maps an ESPHome RGB color to the closest 4-bit panel palette entry.
 *
 * The mapping is heuristic because the physical panel only supports six
 * fixed colours rather than arbitrary RGB output.
 */
uint8_t color_to_code(const Color &color);
/**
 * @brief Writes one logical pixel into the nibble-packed framebuffer.
 *
 * @param buffer Target framebuffer.
 * @param x Logical x coordinate.
 * @param y Logical y coordinate.
 * @param color_code 4-bit panel palette entry.
 */
void write_pixel_to_buffer(uint8_t *buffer, int x, int y, uint8_t color_code);
/** @brief Fills the whole packed framebuffer with one repeated palette entry. */
void fill_buffer_with_code(uint8_t *buffer, uint8_t color_code);
/** @brief Draws the vendor-style vertical colour bar test pattern into the framebuffer. */
void draw_color_bar(uint8_t *buffer);
/** @brief Draws a tiled six-colour checker pattern into the framebuffer. */
void draw_checkerboard(uint8_t *buffer);

/**
 * @brief Bounding box describing a rectangular region of the display.
 *
 * Used for change detection results and manual partial-update regions.
 * Coordinates are in the logical panel space (x=0 left, y=0 top).
 */
struct UpdateRegion {
  int x{0};
  int y{0};
  int width{0};
  int height{0};

  /** @brief Returns true if the region has zero or negative area. */
  bool empty() const { return width <= 0 || height <= 0; }
};

/**
 * @brief Compares two full framebuffers and returns the bounding box of all differing pixels.
 *
 * The comparison operates on the nibble-packed framebuffer layout and
 * converts the result back to logical panel coordinates. Uses memcmp per row
 * for fast early-exit detection, then narrows to the exact byte-column range.
 *
 * @param current        The newly rendered framebuffer.
 * @param previous_frame The last successfully displayed frame.
 * @return UpdateRegion covering all changed pixels, or an empty region if identical.
 */
UpdateRegion find_changed_region(const uint8_t *current, const uint8_t *previous_frame);

}  // namespace epaper_spectra6_133
}  // namespace esphome
