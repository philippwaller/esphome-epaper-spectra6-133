# Benchmarks

This project uses Google Benchmark for host-side performance checks. The
benchmark target links the same production C++ framebuffer, controller, and
display component sources used by the host tests, with the existing test stubs
standing in for ESP-IDF, ESPHome, SPI, GPIO, timers, and hardware refreshes.

No real display, Wi-Fi, serial port, ESPHome runtime, or ESP32 hardware is
required.

## Covered Hot Paths

- RGB-to-panel color mapping for mixed RGB, uniformly distributed palette, and representative palette pixels.
- Nibble-packed framebuffer writes and full-frame fills.
- General primitive drawing in row-major order.
- Image drawing in the column-major order used by ESPHome's `Image::draw()`.
- Repeated drawing inside an already tracked changed region.
- Built-in framebuffer patterns.
- Dirty-region comparison for unchanged, small, medium, and full-frame changes.
- Partial-region alignment for single-half, split-boundary, clipped, and full-screen regions.

The input variants cover small UI/icon-sized inputs, medium dashboard/image
regions, and the 1200x1600 worst-case panel size where applicable.

## Workload Naming

Benchmark names describe both the production path and the input workload:

- `MixedRGB` uses arbitrary RGB values and primarily exercises nearest-palette matching.
- `UniformPalette` distributes the six exact Spectra 6 colors evenly.
- `RepresentativePalette` uses a deterministic full-frame distribution based on the example image: 46% black, 20% white, 11% each red, green, and blue, and 1% yellow.
- `PrimitiveRowMajor` models general drawing primitives.
- `ImageColumnMajor` matches the traversal order used by ESPHome's image component.
- `ContainedOverdraw` measures repeated drawing after the changed-region bounds are already established.

Image clipping is not benchmarked separately because ESPHome clips images
before pixels reach the display component. Hardware refresh and RGB565 image
decoding are also outside the host benchmark scope.

## Run Locally

```bash
bash ./scripts/run-benchmarks.sh
```

The script configures a Release CMake build in `build/benchmarks`, builds the
`epaper_spectra6_133_benchmarks` target, runs it, and writes Google Benchmark
JSON to:

```text
build/benchmarks/results.json
```

Useful variants:

```bash
# Run only dirty-region benchmarks
bash ./scripts/run-benchmarks.sh --filter 'ChangeDetection/.*'

# Use a longer sample time for local investigation
bash ./scripts/run-benchmarks.sh --min-time 0.2s

# Write results somewhere else
bash ./scripts/run-benchmarks.sh --output /tmp/epaper-benchmarks.json
```

Extra Google Benchmark arguments can be passed after `--`:

```bash
bash ./scripts/run-benchmarks.sh -- --benchmark_repetitions=3
```

## CI and CodSpeed

The `CodSpeed` workflow in `.github/workflows/codspeed.yml` builds and runs the
benchmark suite in simulation mode for every push to `main` and every manual
backtest run. On pull requests, the benchmark job runs only when files that can
change the measured host-side performance are modified:

- production C++ component sources and headers,
- benchmark sources,
- host C++ test support used by the benchmark target,
- benchmark CMake wiring, or
- the CodSpeed workflow itself.

Documentation-only, Python code generation, YAML example, and project metadata
pull requests skip the benchmark job because they do not affect the measured C++
hot paths. CodSpeed tracks benchmark history and reports performance changes
directly on pull requests where those measurements are relevant.

The workflow uses GitHub's OpenID Connect integration, so no repository API key
is required.
