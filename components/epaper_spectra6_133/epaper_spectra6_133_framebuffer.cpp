#include "epaper_spectra6_133_framebuffer.h"

/**
 * @file epaper_spectra6_133_framebuffer.cpp
 * @brief Implements framebuffer colour mapping and reference test patterns.
 */

#include <algorithm>
#include <array>
#include <cstring>

namespace esphome {
namespace epaper_spectra6_133 {

/**
 * @brief Maps a generic RGB colour into the nearest supported panel palette entry.
 *
 * The panel supports only six colours, so arbitrary RGB values are mapped by
 * nearest-distance matching in RGB space.
 */
uint8_t color_to_code(const Color &color) {
  const uint32_t rgb = (static_cast<uint32_t>(color.red) << 16) | (static_cast<uint32_t>(color.green) << 8) |
                       static_cast<uint32_t>(color.blue);

  // Fast path for exact Spectra 6 palette colors.
  switch (rgb) {
    case 0x00000000:
      return COLOR_BLACK;
    case 0x00FFFFFF:
      return COLOR_WHITE;
    case 0x00FFFF00:
      return COLOR_YELLOW;
    case 0x00FF0000:
      return COLOR_RED;
    case 0x0000FF00:
      return COLOR_GREEN;
    case 0x000000FF:
      return COLOR_BLUE;
    default:
      break;
  }

  struct PaletteEntry {
    uint8_t code;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
  };

  static constexpr PaletteEntry palette[] = {
      {COLOR_BLACK, 0, 0, 0}, {COLOR_WHITE, 255, 255, 255}, {COLOR_YELLOW, 255, 255, 0},
      {COLOR_RED, 255, 0, 0}, {COLOR_GREEN, 0, 255, 0},     {COLOR_BLUE, 0, 0, 255},
  };

  auto squared_delta = [](uint8_t lhs, uint8_t rhs) -> uint32_t {
    const int delta = static_cast<int>(lhs) - static_cast<int>(rhs);
    return static_cast<uint32_t>(delta * delta);
  };

  uint8_t best_code = COLOR_BLACK;
  uint32_t best_distance = UINT32_MAX;
  for (const auto &entry : palette) {
    const uint32_t distance = squared_delta(color.red, entry.red) + squared_delta(color.green, entry.green) +
                              squared_delta(color.blue, entry.blue);
    if (distance < best_distance) {
      best_distance = distance;
      best_code = entry.code;
    }
  }

  return best_code;
}
/**
 * @brief Writes one logical pixel into the nibble-packed framebuffer.
 *
 * Two pixels are packed per byte: even x in the high nibble, odd x in the
 * low nibble.  The byte order within each row is left-to-right so that
 * bytes [0..299] correspond to CS0 and [300..599] to CS1.
 *
 * The framebuffer is written left-to-right. Controller scan direction is set
 * during panel initialization and must match this logical layout.
 */
void write_pixel_to_buffer(uint8_t *buffer, int x, int y, uint8_t color_code) {
  if (buffer == nullptr) {
    return;
  }

  // Even logical x values occupy the high nibble; odd values the low nibble.
  const size_t byte_index = static_cast<size_t>(y) * ROW_BYTES + static_cast<size_t>(x / 2);
  if ((x & 1) == 0) {
    buffer[byte_index] = static_cast<uint8_t>((buffer[byte_index] & 0x0F) | (color_code << 4));
  } else {
    buffer[byte_index] = static_cast<uint8_t>((buffer[byte_index] & 0xF0) | (color_code & 0x0F));
  }
}

/** @brief Fills every pixel in the framebuffer with the same packed panel colour. */
void fill_buffer_with_code(uint8_t *buffer, uint8_t color_code) {
  if (buffer == nullptr) {
    return;
  }

  const uint8_t packed = static_cast<uint8_t>((color_code << 4) | color_code);
  std::memset(buffer, packed, FULL_FRAME_SIZE);
}

/** @brief Draws six horizontal colour bands matching the vendor demo pattern. */
void draw_color_bar(uint8_t *buffer) {
  if (buffer == nullptr) {
    return;
  }
  static constexpr uint8_t colors[6] = {COLOR_BLACK, COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_BLUE, COLOR_GREEN};
  const int band_height = EPD_HEIGHT / 6;

  for (int y = 0; y < EPD_HEIGHT; y++) {
    const int band = std::min(5, y / band_height);
    const uint8_t nibble = colors[band];
    const uint8_t packed = static_cast<uint8_t>((nibble << 4) | nibble);
    std::memset(buffer + static_cast<size_t>(y) * ROW_BYTES, packed, ROW_BYTES);
  }
}

/** @brief Draws a repeating six-colour tiled checker pattern across the full panel. */
void draw_checkerboard(uint8_t *buffer) {
  if (buffer == nullptr) {
    return;
  }
  static constexpr uint8_t colors[6] = {COLOR_BLACK, COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_BLUE, COLOR_GREEN};
  static constexpr int grid_cols = 6;
  static constexpr int grid_rows = 8;
  static constexpr int cell_width = EPD_WIDTH / grid_cols;    // 200
  static constexpr int cell_height = EPD_HEIGHT / grid_rows;  // 200

  for (int y = 0; y < EPD_HEIGHT; y++) {
    const int grid_y = std::min(grid_rows - 1, y / cell_height);
    uint8_t *row_ptr = buffer + static_cast<size_t>(y) * ROW_BYTES;

    // Each cell is 200 px = 100 bytes wide.  Fill cell-by-cell with memset.
    for (int col = 0; col < grid_cols; col++) {
      const uint8_t nibble = colors[(col + grid_y) % 6];
      const uint8_t packed = static_cast<uint8_t>((nibble << 4) | nibble);
      const size_t byte_offset = static_cast<size_t>(col * cell_width / 2);
      const size_t byte_count = static_cast<size_t>(cell_width / 2);
      std::memset(row_ptr + byte_offset, packed, byte_count);
    }
  }
}

/**
 * @brief Compares two full framebuffers byte-by-byte and returns the minimal
 *        bounding box (in logical panel coordinates) of all differing pixels.
 *
 * Byte column N in the framebuffer holds logical pixels 2*N and 2*N+1.
 * After finding the min/max differing byte column and row, the range is
 * converted to logical pixel coordinates directly.
 *
 * Uses memcmp per row for fast early-exit detection, then narrows to the
 * exact differing byte-column range within changed rows.
 */
UpdateRegion find_changed_region(const uint8_t *current, const uint8_t *previous_frame) {
  UpdateRegion rect;

  if (current == nullptr || previous_frame == nullptr) {
    return rect;
  }

  int row_min = EPD_HEIGHT;
  int row_max = -1;
  int col_byte_min = static_cast<int>(ROW_BYTES);
  int col_byte_max = -1;

  for (int row = 0; row < EPD_HEIGHT; row++) {
    const size_t row_offset = static_cast<size_t>(row) * ROW_BYTES;
    const uint8_t *cur_row = current + row_offset;
    const uint8_t *prev_row = previous_frame + row_offset;

    // Fast row-level scan to detect whether this row differs at all.
    if (std::memcmp(cur_row, prev_row, ROW_BYTES) == 0) {
      continue;
    }

    // Row has at least one differing byte — find the exact byte-column range.
    if (row < row_min) {
      row_min = row;
    }
    row_max = row;

    // Scan forward for the first differing byte in this row.
    for (int col = 0; col < static_cast<int>(ROW_BYTES); col++) {
      if (cur_row[col] != prev_row[col]) {
        if (col < col_byte_min) {
          col_byte_min = col;
        }
        break;
      }
    }

    // Scan backward for the last differing byte in this row.
    for (int col = static_cast<int>(ROW_BYTES) - 1; col >= 0; col--) {
      if (cur_row[col] != prev_row[col]) {
        if (col > col_byte_max) {
          col_byte_max = col;
        }
        break;
      }
    }
  }

  if (row_max < 0) {
    // No differences found.
    return rect;
  }

  // Byte column N contains logical pixels 2*N and 2*N+1.
  const int x_min = col_byte_min * 2;
  const int x_max = col_byte_max * 2 + 1;

  rect.x = x_min;
  rect.y = row_min;
  rect.width = x_max - x_min + 1;
  rect.height = row_max - row_min + 1;

  return rect;
}

}  // namespace epaper_spectra6_133
}  // namespace esphome
