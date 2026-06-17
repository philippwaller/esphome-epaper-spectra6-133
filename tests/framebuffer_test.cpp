#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "epaper_spectra6_133_framebuffer.h"

namespace esphome {
namespace epaper_spectra6_133 {
namespace {

uint8_t read_pixel_code(const std::vector<uint8_t> &buffer, int x, int y) {
  const size_t byte_index = static_cast<size_t>(y) * ROW_BYTES + static_cast<size_t>(x / 2);
  const uint8_t packed = buffer[byte_index];
  const uint8_t nibble = static_cast<uint8_t>((x & 1) == 0 ? ((packed >> 4) & 0x0F) : (packed & 0x0F));
  return static_cast<uint8_t>((nibble << 4) | nibble);
}

TEST(ColorToCodeTest, MapsCanonicalPanelColors) {
  EXPECT_EQ(color_to_code(0, 0, 0), COLOR_BLACK);
  EXPECT_EQ(color_to_code(255, 255, 255), COLOR_WHITE);
  EXPECT_EQ(color_to_code(255, 255, 0), COLOR_YELLOW);
  EXPECT_EQ(color_to_code(255, 0, 0), COLOR_RED);
  EXPECT_EQ(color_to_code(0, 0, 255), COLOR_BLUE);
  EXPECT_EQ(color_to_code(0, 255, 0), COLOR_GREEN);
}

TEST(ColorToCodeTest, MapsEdgeCaseColorsToClosestPaletteMatch) {
  EXPECT_EQ(color_to_code(125, 158, 131), COLOR_WHITE);
  EXPECT_EQ(color_to_code(125, 165, 128), COLOR_GREEN);
  EXPECT_EQ(color_to_code(180, 10, 240), COLOR_BLUE);
}

TEST(WritePixelToBufferTest, WritesEvenAndOddPixelsIntoSeparateNibbles) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, 0xAA);

  write_pixel_to_buffer(buffer.data(), 10, 20, COLOR_RED);
  write_pixel_to_buffer(buffer.data(), 11, 20, COLOR_GREEN);

  const size_t byte_index = static_cast<size_t>(20) * ROW_BYTES + 5;
  EXPECT_EQ(buffer[byte_index], 0x36);
}

TEST(FillBufferWithCodeTest, FillsEntireFramebuffer) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, 0x00);

  fill_buffer_with_code(buffer.data(), COLOR_YELLOW);

  EXPECT_TRUE(std::all_of(buffer.begin(), buffer.end(), [](uint8_t value) { return value == COLOR_YELLOW; }));
}

TEST(DrawColorBarTest, DrawsSixHorizontalBands) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, 0x00);

  draw_color_bar(buffer.data());

  EXPECT_EQ(read_pixel_code(buffer, 0, 0), COLOR_BLACK);
  EXPECT_EQ(read_pixel_code(buffer, 0, EPD_HEIGHT / 6), COLOR_WHITE);
  EXPECT_EQ(read_pixel_code(buffer, 0, (EPD_HEIGHT / 6) * 2), COLOR_YELLOW);
  EXPECT_EQ(read_pixel_code(buffer, 0, (EPD_HEIGHT / 6) * 3), COLOR_RED);
  EXPECT_EQ(read_pixel_code(buffer, 0, (EPD_HEIGHT / 6) * 4), COLOR_BLUE);
  EXPECT_EQ(read_pixel_code(buffer, 0, EPD_HEIGHT - 1), COLOR_GREEN);
}

TEST(FindChangedRegionTest, ReturnsEmptyRegionForIdenticalBuffers) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, COLOR_WHITE);
  std::vector<uint8_t> previous_frame(FULL_FRAME_SIZE, COLOR_WHITE);

  const UpdateRegion region = find_changed_region(current.data(), previous_frame.data());

  EXPECT_TRUE(region.empty());
  EXPECT_EQ(region.x, 0);
  EXPECT_EQ(region.y, 0);
  EXPECT_EQ(region.width, 0);
  EXPECT_EQ(region.height, 0);
}

TEST(FindChangedRegionTest, ExpandsChangedBytesToLogicalPixels) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, COLOR_WHITE);
  std::vector<uint8_t> previous_frame(FULL_FRAME_SIZE, COLOR_WHITE);

  write_pixel_to_buffer(current.data(), 17, 11, COLOR_RED);
  write_pixel_to_buffer(current.data(), 44, 13, COLOR_BLUE);

  const UpdateRegion region = find_changed_region(current.data(), previous_frame.data());

  EXPECT_FALSE(region.empty());
  EXPECT_EQ(region.x, 16);
  EXPECT_EQ(region.y, 11);
  EXPECT_EQ(region.width, 30);
  EXPECT_EQ(region.height, 3);
}

}  // namespace
}  // namespace epaper_spectra6_133
}  // namespace esphome
