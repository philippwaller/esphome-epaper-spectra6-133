# Benchmarks

This project uses Google Benchmark for host-side performance checks. The
benchmark target links the same production C++ framebuffer, controller, and
display component sources used by the host tests, with the existing test stubs
standing in for ESP-IDF, ESPHome, SPI, GPIO, timers, and hardware refreshes.

No real display, Wi-Fi, serial port, ESPHome runtime, or ESP32 hardware is
required.

## Covered Hot Paths

- RGB-to-panel colour mapping with realistic mixed image pixels.
- Nibble-packed framebuffer pixel writes.
- Image-to-framebuffer conversion using production colour mapping and packing.
- Display draw calls with change tracking enabled.
- Full-frame fills and built-in display preparation patterns.
- Dirty-region comparison for unchanged, small, medium, and full-frame changes.
- Partial-region alignment for single-half, split-boundary, clipped, and full-screen regions.

The input variants cover small UI/icon-sized inputs, medium dashboard/image
regions, and the 1200x1600 worst-case panel size where applicable.

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
benchmark suite in simulation mode for pushes to `main`, pull requests, and
manual backtest runs. CodSpeed tracks benchmark history and reports performance
changes directly on pull requests.

The workflow uses GitHub's OpenID Connect integration, so no repository API key
is required.
