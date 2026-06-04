#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <benchmark/benchmark.h>

#include "epaper_spectra6_133.h"
#include "epaper_spectra6_133_framebuffer.h"
#include "transport_test_support.h"

namespace esphome {
namespace epaper_spectra6_133 {
namespace {

/// Panel palette codes used to feed already-quantized pixels into packing benchmarks.
constexpr std::array<uint8_t, 6> kPanelCodes = {
    COLOR_BLACK, COLOR_WHITE, COLOR_YELLOW, COLOR_RED, COLOR_BLUE, COLOR_GREEN,
};

/// Builds the byte value used to initialize both pixels stored in one framebuffer byte.
uint8_t packed_fill_byte(uint8_t color_code) {
  const uint8_t nibble = static_cast<uint8_t>(color_code & 0x0F);
  return static_cast<uint8_t>((nibble << 4) | nibble);
}

/**
 * Builds deterministic pseudo-image pixels for palette-mapping benchmarks.
 *
 * The generated pattern has enough RGB variation to exercise nearest-palette
 * decisions without making the benchmark depend on external image assets.
 *
 * @param width Logical image width in pixels.
 * @param height Logical image height in pixels.
 * @return RGB pixels in row-major order.
 */
std::vector<Color> make_mixed_pixels(int width, int height) {
  std::vector<Color> pixels;
  pixels.reserve(static_cast<size_t>(width) * static_cast<size_t>(height));

  for (int y = 0; y < height; y++) {
    for (int x = 0; x < width; x++) {
      const uint8_t red = static_cast<uint8_t>((x * 17 + y * 3 + (x / 23) * 31) & 0xFF);
      const uint8_t green = static_cast<uint8_t>((x * 5 + y * 19 + (y / 17) * 29) & 0xFF);
      const uint8_t blue = static_cast<uint8_t>((x * 11 + y * 7 + ((x + y) / 31) * 43) & 0xFF);
      pixels.emplace_back(red, green, blue);
    }
  }

  return pixels;
}

/**
 * Writes a clipped rectangle into a packed framebuffer using production pixel packing.
 *
 * Changed-region benchmarks use this helper so their inputs match the byte
 * layout produced by normal rendering.
 *
 * @param buffer Full-frame packed framebuffer to mutate.
 * @param x Left edge of the requested logical rectangle.
 * @param y Top edge of the requested logical rectangle.
 * @param width Requested rectangle width in pixels.
 * @param height Requested rectangle height in pixels.
 * @param color_code 4-bit panel palette code to write.
 */
void write_rect(std::vector<uint8_t> &buffer, int x, int y, int width, int height, uint8_t color_code) {
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(EPD_WIDTH, x + width);
  const int y1 = std::min(EPD_HEIGHT, y + height);

  for (int row = y0; row < y1; row++) {
    for (int col = x0; col < x1; col++) {
      write_pixel_to_buffer(buffer.data(), col, row, color_code);
    }
  }
}

/**
 * Test-only display wrapper that exposes the protected draw hook to benchmarks.
 *
 * The wrapper allocates the normal framebuffer but never initializes transport
 * hardware, allowing the benchmark to include production color mapping, pixel
 * packing, and dirty tracking without scheduling panel transfers.
 */
class BenchmarkDisplay : public EpaperSpectra6133 {
 public:
  /// Allocates the full framebuffer used by EpaperSpectra6133 drawing code.
  BenchmarkDisplay() { this->init_internal_(FULL_FRAME_SIZE); }

  /// Releases the framebuffer allocated by DisplayBuffer::init_internal_().
  ~BenchmarkDisplay() override { ::operator delete(this->buffer_); }

  /// Forwards one logical pixel through the production draw pipeline.
  void draw_pixel(int x, int y, Color color) { this->draw_absolute_pixel_internal(x, y, color); }
};

/**
 * Benchmarks RGB-to-Spectra-6 palette mapping without framebuffer writes.
 *
 * This isolates color classification cost for image-heavy screens before
 * pixel packing or dirty-region tracking are considered.
 */
void BM_ColorMapping_MixedPixels(benchmark::State &state) {
  const auto pixels = make_mixed_pixels(static_cast<int>(state.range(0)), static_cast<int>(state.range(1)));
  uint32_t checksum = 0;

  for (auto _ : state) {
    for (const Color &color : pixels) {
      checksum += color_to_code(color);
    }
    benchmark::DoNotOptimize(checksum);
  }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(pixels.size()));
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pixels.size() * 3U));
}

BENCHMARK(BM_ColorMapping_MixedPixels)
    ->Name("ColorMapping/MixedPixels")
    ->Args({64, 64})
    ->Args({480, 320})
    ->Args({EPD_WIDTH, EPD_HEIGHT});

/**
 * Benchmarks nibble packing for pixels that are already panel palette codes.
 *
 * This models simple UI and layout drawing where colors are known up front and
 * no RGB palette search is needed.
 */
void BM_FramebufferGeneration_WritePixels(benchmark::State &state) {
  const int width = static_cast<int>(state.range(0));
  const int height = static_cast<int>(state.range(1));
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));

  for (auto _ : state) {
    size_t palette_index = 0;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        write_pixel_to_buffer(buffer.data(), x, y, kPanelCodes[palette_index]);
        palette_index++;
        if (palette_index == kPanelCodes.size()) {
          palette_index = 0;
        }
      }
    }
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(width) * height);
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>((width * height + 1) / 2));
}

BENCHMARK(BM_FramebufferGeneration_WritePixels)
    ->Name("FramebufferGeneration/WritePixelRegion")
    ->Args({64, 64})
    ->Args({480, 320})
    ->Args({EPD_WIDTH, EPD_HEIGHT});

/**
 * Benchmarks image preparation from RGB pixels into the packed framebuffer.
 *
 * Each iteration maps deterministic RGB input to the fixed Spectra 6 palette
 * and immediately writes the resulting panel code into the framebuffer.
 */
void BM_FramebufferGeneration_ImageToFramebuffer(benchmark::State &state) {
  const int width = static_cast<int>(state.range(0));
  const int height = static_cast<int>(state.range(1));
  const auto pixels = make_mixed_pixels(width, height);
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));

  for (auto _ : state) {
    size_t pixel_index = 0;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        write_pixel_to_buffer(buffer.data(), x, y, color_to_code(pixels[pixel_index]));
        pixel_index++;
      }
    }
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(pixels.size()));
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pixels.size() * 3U));
}

BENCHMARK(BM_FramebufferGeneration_ImageToFramebuffer)
    ->Name("FramebufferGeneration/ImageToFramebuffer")
    ->Args({64, 64})
    ->Args({480, 320})
    ->Args({EPD_WIDTH, EPD_HEIGHT});

/**
 * Benchmarks the display draw hook used by tracked partial updates.
 *
 * Compared with the raw image-to-framebuffer benchmark, this path also includes
 * DisplayBuffer dispatch and the tracked changed-region accumulator.
 */
void BM_DisplayDrawPipeline_TrackedPixels(benchmark::State &state) {
  const int width = static_cast<int>(state.range(0));
  const int height = static_cast<int>(state.range(1));
  const auto pixels = make_mixed_pixels(width, height);
  BenchmarkDisplay display;

  for (auto _ : state) {
    display.reset_change_tracking();

    size_t pixel_index = 0;
    for (int y = 0; y < height; y++) {
      for (int x = 0; x < width; x++) {
        display.draw_pixel(x, y, pixels[pixel_index]);
        pixel_index++;
      }
    }

    const UpdateRegion region = display.detect_changed_region();
    int region_checksum = region.x + region.y + region.width + region.height;
    benchmark::DoNotOptimize(region_checksum);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations() * static_cast<int64_t>(pixels.size()));
  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(pixels.size() * 3U));
}

BENCHMARK(BM_DisplayDrawPipeline_TrackedPixels)
    ->Name("DisplayPreparation/TrackedDrawPixels")
    ->Args({64, 64})
    ->Args({480, 320})
    ->Args({EPD_WIDTH, EPD_HEIGHT});

/**
 * Benchmarks filling the full framebuffer with one packed panel color.
 *
 * This is the fast path used by full refresh preparation and explicit
 * clear(color) calls. Each capture supplies one real Spectra 6 palette code.
 *
 * @param color_code 4-bit panel palette code used for every pixel.
 */
void BM_FramebufferFill(benchmark::State &state, uint8_t color_code) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, packed_fill_byte(COLOR_BLACK));

  for (auto _ : state) {
    fill_buffer_with_code(buffer.data(), color_code);
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK_CAPTURE(BM_FramebufferFill, black, COLOR_BLACK)->Name("FramebufferGeneration/FillFullFrame/black");
BENCHMARK_CAPTURE(BM_FramebufferFill, white, COLOR_WHITE)->Name("FramebufferGeneration/FillFullFrame/white");
BENCHMARK_CAPTURE(BM_FramebufferFill, yellow, COLOR_YELLOW)->Name("FramebufferGeneration/FillFullFrame/yellow");
BENCHMARK_CAPTURE(BM_FramebufferFill, red, COLOR_RED)->Name("FramebufferGeneration/FillFullFrame/red");
BENCHMARK_CAPTURE(BM_FramebufferFill, blue, COLOR_BLUE)->Name("FramebufferGeneration/FillFullFrame/blue");
BENCHMARK_CAPTURE(BM_FramebufferFill, green, COLOR_GREEN)->Name("FramebufferGeneration/FillFullFrame/green");

/**
 * Benchmarks generation of the vendor-style color-bar test pattern.
 *
 * The implementation fills whole rows and should remain close to full-frame
 * memset performance.
 */
void BM_DisplayPattern_ColorBar(benchmark::State &state) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, packed_fill_byte(COLOR_BLACK));

  for (auto _ : state) {
    draw_color_bar(buffer.data());
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_DisplayPattern_ColorBar)->Name("DisplayPreparation/ColorBarPattern");

/**
 * Benchmarks generation of the tiled checkerboard panel test pattern.
 *
 * This pattern is more segmented than the color bar, so it catches regressions
 * in row and cell filling behavior.
 */
void BM_DisplayPattern_Checkerboard(benchmark::State &state) {
  std::vector<uint8_t> buffer(FULL_FRAME_SIZE, packed_fill_byte(COLOR_BLACK));

  for (auto _ : state) {
    draw_checkerboard(buffer.data());
    benchmark::DoNotOptimize(buffer.data());
    benchmark::ClobberMemory();
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_DisplayPattern_Checkerboard)->Name("DisplayPreparation/CheckerboardPattern");

/**
 * Benchmarks changed-region detection when current and previous frames match.
 *
 * This is compare-mode's best case: no bytes changed, but the implementation
 * still scans row-by-row to protect the common "skip refresh" path.
 */
void BM_ChangedRegion_IdenticalFullFrame(benchmark::State &state) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));
  std::vector<uint8_t> previous(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));

  for (auto _ : state) {
    const UpdateRegion region = find_changed_region(current.data(), previous.data());
    int region_checksum = region.x + region.y + region.width + region.height;
    benchmark::DoNotOptimize(region_checksum);
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_ChangedRegion_IdenticalFullFrame)->Name("ChangeDetection/IdenticalFullFrame");

/**
 * Benchmarks changed-region detection for a small clock/status patch.
 *
 * The dirty area is near the right side of the display and is tiny relative to
 * the full framebuffer, but compare mode must still locate it inside a full
 * frame scan.
 */
void BM_ChangedRegion_SmallClockPatch(benchmark::State &state) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));
  std::vector<uint8_t> previous(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));
  write_rect(current, 1010, 48, 96, 176, COLOR_RED);

  for (auto _ : state) {
    const UpdateRegion region = find_changed_region(current.data(), previous.data());
    int region_checksum = region.x + region.y + region.width + region.height;
    benchmark::DoNotOptimize(region_checksum);
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_ChangedRegion_SmallClockPatch)->Name("ChangeDetection/SmallClockPatch");

/**
 * Benchmarks changed-region detection for a medium dashboard-card update.
 *
 * The dirty area spans many rows while still being small enough to benefit
 * from partial refresh instead of full-frame transfer.
 */
void BM_ChangedRegion_MediumDashboardCard(benchmark::State &state) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));
  std::vector<uint8_t> previous(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));
  write_rect(current, 280, 460, 520, 320, COLOR_BLUE);

  for (auto _ : state) {
    const UpdateRegion region = find_changed_region(current.data(), previous.data());
    int region_checksum = region.x + region.y + region.width + region.height;
    benchmark::DoNotOptimize(region_checksum);
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_ChangedRegion_MediumDashboardCard)->Name("ChangeDetection/MediumDashboardCard");

/**
 * Benchmarks changed-region detection when every row differs.
 *
 * This is the compare-mode full-frame change shape. It should stay fast because
 * the first and last differing byte in each changed row are found quickly.
 */
void BM_ChangedRegion_FullFrameChanged(benchmark::State &state) {
  std::vector<uint8_t> current(FULL_FRAME_SIZE, packed_fill_byte(COLOR_BLACK));
  std::vector<uint8_t> previous(FULL_FRAME_SIZE, packed_fill_byte(COLOR_WHITE));

  for (auto _ : state) {
    const UpdateRegion region = find_changed_region(current.data(), previous.data());
    int region_checksum = region.x + region.y + region.width + region.height;
    benchmark::DoNotOptimize(region_checksum);
  }

  state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(FULL_FRAME_SIZE));
}

BENCHMARK(BM_ChangedRegion_FullFrameChanged)->Name("ChangeDetection/FullFrameChanged");

/// Logical rectangle fixture used by partial-region computation benchmarks.
struct RegionCase {
  /// Requested left edge in logical panel coordinates.
  int x;
  /// Requested top edge in logical panel coordinates.
  int y;
  /// Requested logical rectangle width in pixels.
  int width;
  /// Requested logical rectangle height in pixels.
  int height;
};

/**
 * Benchmarks conversion from logical dirty rectangles to controller regions.
 *
 * This covers clipping, splitting across the two controller halves, horizontal
 * alignment, and even-row adjustment. Each benchmark iteration batches many
 * computations so tiny operations produce stable trend data for CodSpeed.
 *
 * @param input Logical rectangle shape to perturb and convert repeatedly.
 */
void BM_PartialRegion_Computation(benchmark::State &state, RegionCase input) {
  static constexpr int kComputationsPerIteration = 1024;
  Transport transport;
  Controller controller(transport);
  test_support::reset_transport_state(transport);
  PartialRegion regions[2]{};
  bool has_region[2]{false, false};

  for (auto _ : state) {
    size_t region_checksum = 0;
    for (int index = 0; index < kComputationsPerIteration; index++) {
      const int x = input.x + (index & 0x03);
      const int y = input.y + ((index >> 2) & 0x03);
      controller.compute_partial_regions(x, y, input.width, input.height, regions, has_region);
      if (has_region[0]) {
        region_checksum += regions[0].row_byte_offset + regions[0].row_byte_count;
      }
      if (has_region[1]) {
        region_checksum += regions[1].row_byte_offset + regions[1].row_byte_count;
      }
      region_checksum += static_cast<size_t>(has_region[0]) + static_cast<size_t>(has_region[1]);
    }
    benchmark::DoNotOptimize(region_checksum);
  }

  state.SetItemsProcessed(state.iterations() * kComputationsPerIteration);
}

BENCHMARK_CAPTURE(BM_PartialRegion_Computation, small_single_half, RegionCase{32, 48, 96, 176})
    ->Name("DisplayPreparation/PartialRegion/small_single_half");
BENCHMARK_CAPTURE(BM_PartialRegion_Computation, medium_dashboard_card, RegionCase{280, 460, 520, 320})
    ->Name("DisplayPreparation/PartialRegion/medium_dashboard_card");
BENCHMARK_CAPTURE(BM_PartialRegion_Computation, split_boundary, RegionCase{560, 100, 160, 180})
    ->Name("DisplayPreparation/PartialRegion/split_boundary");
BENCHMARK_CAPTURE(BM_PartialRegion_Computation, clipped_edge, RegionCase{1140, 1500, 220, 220})
    ->Name("DisplayPreparation/PartialRegion/clipped_edge");
BENCHMARK_CAPTURE(BM_PartialRegion_Computation, full_frame, RegionCase{0, 0, EPD_WIDTH, EPD_HEIGHT})
    ->Name("DisplayPreparation/PartialRegion/full_frame");

}  // namespace
}  // namespace epaper_spectra6_133
}  // namespace esphome
