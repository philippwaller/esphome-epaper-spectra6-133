<div align="center">

# 🖼️ esphome-epaper-spectra6-133

### 13.3″ Spectra 6 E-Paper Display for ESPHome

[![License](https://img.shields.io/badge/license-Apache%202.0-blue.svg)](LICENSE)
<!-- Don't update the ESPHome support snippets manually. Update requirements.txt and run ./.venv/bin/python scripts/esphome-versions.py sync. -->
<!-- x-esphome-version-specifier -->
[![ESPHome](https://img.shields.io/badge/ESPHome-%3E%3D%202026.2.0-000000?logo=esphome&logoColor=white)](https://esphome.io)
[![GitHub Release](https://img.shields.io/github/v/release/philippwaller/esphome-epaper-spectra6-133?logo=github)](https://github.com/philippwaller/esphome-epaper-spectra6-133/releases)
[![CI](https://github.com/philippwaller/esphome-epaper-spectra6-133/actions/workflows/ci.yml/badge.svg)](https://github.com/philippwaller/esphome-epaper-spectra6-133/actions/workflows/ci.yml)
[![CodSpeed](https://img.shields.io/endpoint?url=https://codspeed.io/badge.json)](https://codspeed.io/philippwaller/esphome-epaper-spectra6-133?utm_source=badge)


**Build dashboards, photoframes, and data displays on 13.3″ Spectra 6 e-paper panels with the ESPHome display model you already know.**

[Quick Start](#quick-start) · [Supported Panels](#supported-panels) · [Board Packages](#using-board-packages) · [Reference](#reference) · [How to use this component](#how-to-use-this-component) · [Image Preparation](#3-prepare-images-before-displaying-them) · [Examples](#example-configurations)

</div>

---

## ✨ What this component does

A ESPHome display component for large-format 13.3″ Spectra 6 e-paper panels. It keeps the familiar ESPHome drawing model, adds an asynchronous refresh pipeline, and gives you precise control over full-screen and partial updates.

| Capability | What it means |
| --- | --- |
| **Draw like any ESPHome display** | Use the standard `DisplayBuffer` API for text, shapes, images, pages, and custom layouts. |
| **Refresh without blocking ESPHome** | Long-running panel updates are handled as cooperative jobs across `loop()` iterations, so WiFi, sensors, API traffic, and automations keep running. |
| **Update only what changed** | Choose full refreshes for complete frames or partial refreshes for the detected changed region. |

---

## 📋 Requirements

<!-- Don't update the ESPHome requirements block manually. Update requirements.txt and run ./.venv/bin/python scripts/esphome-versions.py sync. -->
<!-- x-esphome-version-specifier-start -->
| What | Details |
| ------ | --------- |
| **ESPHome** | >= 2026.2.0 |
| **Board** | ESP32 with PSRAM |
| **Display** | 13.3″ Spectra 6 panel |
| **Framework** | ESP-IDF (Arduino is **not** supported) |
<!-- x-esphome-version-specifier-end -->

### Supported Panels

| Vendor | Model | Status |
|---|---|---|
| Goo Display | GDEP133C02 | ✅ Tested |
| Waveshare | 13.3inch e-Paper (F) | 🔄 Not yet confirmed |
| Seeed Studio | 13.3″ ePaper Display | 🔄 Not yet confirmed |

Panels marked 🔄 are expected to work, but have not been confirmed with hardware. If you own one of these panels, please [open an issue](https://github.com/philippwaller/esphome-epaper-spectra6-133/issues) with your test results.

<details>
<summary><strong>Preconfigured Boards</strong></summary>

| Board | MCU | PSRAM | Notes |
|---|---|---|---|
| [Goo Display ESP32-133C02](packages/boards/goo_display/esp32_133c02.yaml) | ESP32-S3 | 16 MB | Revision 1.0, 2.0 |

See [Using Board Packages](#using-board-packages) for import instructions and copy-paste examples.

</details>

---

## 🚀 Quick Start


Use this component by importing it directly into your ESPHome configuration. ESPHome will fetch the component from GitHub, build it together with your YAML file, and flash everything to your device.

<!-- x-release-please-start-version -->
```yaml
external_components:
  - source:
      type: git
      url: https://github.com/philippwaller/esphome-epaper-spectra6-133
      ref: v0.1.2
```
<!-- x-release-please-end -->

<details>
<summary><strong>Need unreleased features?</strong> Use the latest development version</summary>

If you want to test changes that are not part of a release yet, point `ref` to `main` instead of a version tag. Adding `refresh: 0s` tells ESPHome to check the Git source on every build.

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/philippwaller/esphome-epaper-spectra6-133
      ref: main
      refresh: 0s
```

For regular setups, prefer a released version tag. Use `main` when you intentionally want the newest development state.

</details>

### 1. Create your ESPHome YAML

Create a file such as `spectra6-hello.yaml` and paste the configuration below. The example draws a simple `HELLO MAKER` screen and refreshes the panel once after boot.

<!-- x-release-please-start-version -->
```yaml
substitutions:
  device_name: spectra6-hello
  friendly_name: Spectra 6 Hello

  # Adjust these pins to match your board.
  epd_cs0_pin: "18"
  epd_cs1_pin: "17"
  epd_clk_pin: "9"
  epd_data0_pin: "41"
  epd_data1_pin: "40"
  epd_busy_pin: "7"
  epd_reset_pin: "6"
  epd_power_pin: "45"

esphome:
  name: ${device_name}
  friendly_name: ${friendly_name}
  platformio_options:
    board_build.psram: enabled
  on_boot:
    priority: -100
    then:
      - component.update: epd

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf

psram:
  mode: octal
  speed: 80MHz

logger:

wifi:
  ssid: !secret wifi_ssid
  password: !secret wifi_password

ota:
  - platform: esphome
    password: !secret ota_password

external_components:
  - source:
      type: git
      url: https://github.com/philippwaller/esphome-epaper-spectra6-133
      ref: v0.1.2

font:
  - file: gfonts://Bebas Neue
    id: font_hero
    size: 250

display:
  - platform: epaper_spectra6_133
    id: epd
    update_interval: never
    cs0_pin: ${epd_cs0_pin}
    cs1_pin: ${epd_cs1_pin}
    clk_pin: ${epd_clk_pin}
    data0_pin: ${epd_data0_pin}
    data1_pin: ${epd_data1_pin}
    busy_pin: ${epd_busy_pin}
    reset_pin: ${epd_reset_pin}
    power_pin: ${epd_power_pin}
    lambda: |-
      // Textbox with double border and yellow fill
      it.filled_rectangle(134, 413, 931, 773, Color(0, 0, 0));
      it.filled_rectangle(137, 416, 925, 767, Color(255, 255, 255));
      it.filled_rectangle(147, 426, 905, 747, Color(0, 0, 0));  
      it.filled_rectangle(159, 438, 881, 723, Color(255, 255, 0));  

      it.print(it.get_width() / 2, 637, id(font_hero), Color(0, 0, 0), TextAlign::CENTER, "HELLO");
      it.print(it.get_width() / 2, 965, id(font_hero), Color(255, 0, 0), TextAlign::CENTER, "MAKER");
```
<!-- x-release-please-end -->


### 2. Add your secrets

Make sure your ESPHome `secrets.yaml` contains these values:

```yaml
wifi_ssid: "YOUR_WIFI_NAME"
wifi_password: "YOUR_WIFI_PASSWORD"
ota_password: "CHANGE_ME"
```

### 3. Flash and enjoy the first render

Connect the ESP32-S3 board by USB and run:

```bash
esphome run spectra6-hello.yaml
```

After flashing, the display should render the `HELLO MAKER` screen once.

## Using Board Packages

Board packages are optional shortcuts for known ESP32/display boards. You can always configure the board, PSRAM, logger, and display pins manually, as shown in the Quick Start example. If your board is listed here, the package gives you those hardware defaults and pin variables without copying them into every device YAML.

Use a board package when you want ESPHome to import the board setup for you. The package defines board-specific ESPHome options and named substitutions such as `${epd_cs0_pin}` and `${epd_busy_pin}`. You can use those names directly in your `display:` block, or override individual substitutions in your own YAML when your wiring differs.

You still need [`external_components`](https://esphome.io/components/external_components/) to download the custom `epaper_spectra6_133` display component. The [`packages`](https://esphome.io/components/packages/) entry only adds the board configuration. Your own YAML still contains the device-specific parts such as `esphome:`, WiFi, API/OTA, fonts, images, and the display lambda.

Add the custom component as shown in the [Quick Start](`#quick-start`) section. Then add the matching board package from the table below:

<!-- x-release-please-start-version -->
```yaml
packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/<package-path>@v0.1.2
```
<!-- x-release-please-end -->

For example, for the Goo Display ESP32-133C02:

<!-- x-release-please-start-version -->
```yaml
packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/goo_display/esp32_133c02.yaml@v0.1.2
```
<!-- x-release-please-end -->

With the package in place, the display block can use the predefined pin substitutions:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epaper_display
    update_interval: never
    cs0_pin: ${epd_cs0_pin}
    cs1_pin: ${epd_cs1_pin}
    clk_pin: ${epd_clk_pin}
    data0_pin: ${epd_data0_pin}
    data1_pin: ${epd_data1_pin}
    busy_pin: ${epd_busy_pin}
    reset_pin: ${epd_reset_pin}
    power_pin: ${epd_power_pin}
    lambda: |-
      it.fill(Color(255, 255, 255));
      it.print(40, 80, id(font_title), Color(0, 0, 0), "Hello Spectra 6");
```

### Available packages

| Vendor | Board | Notes | Package path |
| --- | --- | --- | --- |
| Goo Display | ESP32-133C02 | Tested on revision 1.0; revision 2.0 is expected to work | [`goo_display/esp32_133c02.yaml`](packages/boards/goo_display/esp32_133c02.yaml) |

Use the `Package path` value in the `github://.../packages/boards/<package-path>@version` import above. Open the linked package file if you want to inspect the exact ESP32, PSRAM, logger, and pin settings it imports.

> [!TIP]
> Have a working setup for another board? Turn it into a reusable package and help the next user get started faster. Open a pull request with your board package, or share your pinout, PSRAM settings, and board notes in an issue so it can be added here.

<details>
<summary><strong>Override package pins</strong></summary>

Values in your device YAML can override package defaults. In particular, substitutions with the same name replace the package value, which is useful if your wiring differs from the preconfigured board.

If your board revision or wiring uses different pins, define the affected substitutions in your own YAML:

```yaml
substitutions:
  epd_busy_pin: "7"
  epd_reset_pin: "6"
  epd_power_pin: "45"

packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/goo_display/esp32_133c02.yaml@v0.1.2
```

Keep overrides close to the top of your file so it is obvious which hardware assumptions differ from the package.

</details>

<details>
<summary><strong>Use packages while developing locally</strong></summary>

The example configs in this repository use local includes because they are built from a checkout:

```yaml
packages:
  board: !include ../packages/boards/goo_display/esp32_133c02.yaml
```

Use this local form only when the package file is present next to your ESPHome YAML. For a Home Assistant ESPHome add-on or a standalone ESPHome config that should fetch files from GitHub, use the `github://...@version` form above.

</details>

## ⚙️ Reference

### Configuration

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `spi_host` | string | `SPI3_HOST` | SPI peripheral (`SPI2_HOST`, `SPI3_HOST`, or raw integer) |
| `cs0_pin` | pin | *required* | Chip-select for the left half |
| `cs1_pin` | pin | *required* | Chip-select for the right half |
| `clk_pin` | pin | *required* | SPI clock |
| `data0_pin` | pin | *required* | SPI MOSI |
| `data1_pin` | pin | *required* | SPI MISO |
| `busy_pin` | pin | *required* | Panel BUSY signal (active low) |
| `reset_pin` | pin | *required* | Hardware reset |
| `power_pin` | pin | *required* | Controls the power supply to the panel |
| `update_mode` | string | `full` | Whether `update()` transfers the full frame (`full`) or only the detected changed region (`partial`) |
| `change_detection_mode` | string | `track` | How to detect changed pixels: `track` (pixel-write accumulator) or `compare` (frame comparison, requires an additional 960 KB of PSRAM) |
| `update_interval` | time | `never` | How often to re-render the display. Use `never` to update only on demand. Accepts values like `30s`, `5min`, `1h`. |
| `auto_clear_enabled` | bool | `true` | Fill the canvas with white before the lambda runs on each `update()` call. Has no effect on `flush()` or `flush_region()`. |
| `lambda` | lambda | — | Drawing code that runs on every `update()` call. Receives the display as `it`. Use `it.print()`, `it.filled_rectangle()`, `it.image()`, etc. to compose the screen. |
| `pages` | list | — | Standard ESPHome [display pages](https://esphome.io/components/display/#pages) |

### Methods

| Method | Description |
|--------|-------------|
| `update()` | Runs the lambda, updates the framebuffer, then refreshes the display according to `update_mode` |
| `update_region(x, y, w, h)` | Runs the lambda, then refreshes only the specified rectangle |
| `update_region(region)` | Same as `update_region(x, y, w, h)`, but uses an `UpdateRegion` object |
| `flush()` | Refreshes the display from the current framebuffer without running the lambda |
| `flush_region(x, y, w, h)` | Refreshes the specified rectangle from the current framebuffer without running the lambda |
| `fill(color)` | Fills the framebuffer with a colour; does not refresh the display |
| `clear()` | Fills the framebuffer with white and schedules a full refresh |
| `clear(color)` | Fills the framebuffer with the given colour and schedules a full refresh |
| `detect_changed_region()` | Returns the currently detected changed rectangle |
| `reset_change_tracking()` | Clears the accumulated changed rectangle |
| `is_busy()` | Returns `true` while a display job is active or waiting to run |
| `is_ready()` | Returns `true` after the display has been initialised |
| `cancel()` | Cancels the current or pending display job where it is safe to do so |

---

## 🧠 How to use this component
### 1. Compose the screen in the lambda

The display `lambda` block is the common place to build the screen. Whenever `update()` is called, ESPHome runs the lambda, writes the drawing commands into the framebuffer, and schedules the result to be shown on the panel:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epd
    lambda: |-
      it.print(600, 800, id(my_font), Color(0, 0, 0), TextAlign::CENTER, "Hello!");
```

With the default setting `auto_clear_enabled: true`, the framebuffer is cleared to white before the lambda runs. Each update therefore starts from a clean canvas instead of drawing on top of the previous frame.

### 2. Draw with the standard ESPHome display API

The component behaves like a regular ESPHome `DisplayBuffer`. You can use the standard [display drawing primitives](https://esphome.io/components/display/) for text, lines, rectangles, images, pages, and custom layouts.

```yaml
lambda: |-
  it.fill(Color(255, 255, 255));

  it.filled_rectangle(50, 50, 200, 200, Color(255, 0, 0));   // red box
  it.rectangle(10, 10, 300, 300, Color(0, 0, 255));           // blue outline
  it.line(0, 0, 1200, 1600, Color(0, 0, 0));                  // black diagonal

  it.print(600, 800, id(my_font), Color(0, 0, 0), TextAlign::CENTER, "Hello!");

  it.image(0, 0, id(my_image));
```

Spectra 6 panels render a fixed six-colour palette. For predictable output, draw with the exact RGB values supported by the panel:

| Colour | Code | Hex |
|--------|------|-----|
| ⬛ Black | `Color(0, 0, 0)` | `#000000` |
| ⬜ White | `Color(255, 255, 255)` | `#FFFFFF` |
| 🟨 Yellow | `Color(255, 255, 0)` | `#FFFF00` |
| 🟥 Red | `Color(255, 0, 0)` | `#FF0000` |
| 🟦 Blue | `Color(0, 0, 255)` | `#0000FF` |
| 🟩 Green | `Color(0, 255, 0)` | `#00FF00` |

> Intermediate RGB values are mapped to the nearest palette colour. For the cleanest result, use the exact values above instead of relying on automatic colour matching.

The same colours are also available as component constants: `id(epd).BLACK` · `id(epd).WHITE` · `id(epd).YELLOW` · `id(epd).RED` · `id(epd).BLUE` · `id(epd).GREEN`

### 3. Prepare images before displaying them

ESPHome can draw images through its normal `image:` component, but Spectra 6 panels can only render a fixed six-colour palette. This library includes the optional `scripts/convert_image.py` helper for this step, but it is only one possible workflow. You can also use an image editor, an online converter, or your own asset pipeline.

The goal is always the same: resize the asset, map it to the panel palette, and save it as an image ESPHome can import.

 A typical workflow with the included script looks like this:

1. Start with a regular source image, for example a PNG or JPEG.
2. Convert it to the Spectra 6 palette using one of the presets. The script resizes the image to the target canvas and reduces it to the six panel colours, using dithering where needed to approximate intermediate tones:

   ```bash
   python scripts/convert_image.py image.jpg --preset default --output configs/images/my-image.png
   ```

3. Import the converted PNG through ESPHome's `image:` component.
4. Draw it from the display lambda.

```yaml
image:
  - file: configs/images/my-image.png
    id: img_artwork
    type: RGB565

display:
  - platform: epaper_spectra6_133
    id: epd
    lambda: |-
      // Draw the image centred on the display.
      const int x = (it.get_width()  - id(img_artwork).get_width())  / 2;
      const int y = (it.get_height() - id(img_artwork).get_height()) / 2;
      it.image(x, y, id(img_artwork));
```

The conversion step is especially useful for photos, gradients, screenshots, and anti-aliased graphics. For simple UI assets, icons, and logos, manually designing directly in the six supported colours can produce cleaner results than automatic quantization and dithering.

> [!TIP]
> Start with `--preset default` for general images, `--preset vivid` for stronger colour impact, `--preset graphics` for posters and UI artwork, and `--preset accurate` when colour fidelity and fine detail matter most. See [Image Conversion](#image-conversion) for all script options.

### 4. Enable partial updates for `update()`

By default, `update()` refreshes the complete 1200×1600 framebuffer. For screens where only a small area changes, such as clocks or dashboards, `update_mode: partial` lets the driver transfer only the detected changed rectangle:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epd
    update_mode: partial
```

Partial updates control how much framebuffer data is transferred to the panel. They do not make a Spectra 6 panel behave like a fast monochrome e-paper display.

| Mode | What happens | Best for |
|------|--------------|----------|
| `full` | Transfers the complete 1200×1600 framebuffer | Photos, full-screen layouts, first bring-up |
| `partial` | Transfers only the detected changed rectangle | Clocks, dashboards, mostly-static screens |

> [!NOTE]
> `update_mode` only governs what `update()` does. When you call `flush_region()` directly, you specify the region yourself — `update_mode` has no effect.

> [!IMPORTANT]
> Partial updates can produce temporary artefacts such as colour drift inside the refreshed region or boundary artefacts around the refreshed rectangle. For clocks, dashboards, and status screens this is usually acceptable. For colour-critical images, use a full refresh.

### 5. Choose a change detection mode

When partial mode is enabled, the driver needs to know which pixels changed. This is controlled by `change_detection_mode`.

| Mode | Memory cost | How it works | Use when |
|------|-------------|--------------|----------|
| `track` | No additional memory | Tracks pixel writes through the drawing pipeline and accumulates a bounding box | You deliberately draw only the areas that changed |
| `compare` | ~960 KB PSRAM | Compares the current framebuffer with the last successfully displayed frame | You render full screens or images and want exact bounds automatically |

#### Track mode

`track` is the default because it is lightweight:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epd
    update_mode: partial
    change_detection_mode: track
```

In this mode, the changed region comes from the pixels touched by drawing operations. This works best when the lambda only redraws the area that really changed.

> [!WARNING]
> The config option `auto_clear_enabled: true` fills every pixel with white before each render, which expands the tracked region to the full screen and defeats partial updates.

For real partial updates in `track` mode, disable automatic clearing and redraw only the changed area:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epd
    update_mode: partial
    change_detection_mode: track
    auto_clear_enabled: false
    lambda: |-
      it.filled_rectangle(100, 700, 500, 200, Color(255, 255, 255));
      it.print(350, 800, id(font_status), Color(0, 0, 0),
               TextAlign::CENTER, "Updated");
```

#### Compare mode

`compare` stores the last successfully displayed frame and computes the changed rectangle by comparing it with the current framebuffer. This is usually the easiest mode for application-style screens where the lambda redraws the whole layout every time:

```yaml
display:
  - platform: epaper_spectra6_133
    id: epd
    update_mode: partial
    change_detection_mode: compare
```

> [!NOTE]
> The comparison buffer is allocated lazily on first refresh. Make sure your board has enough PSRAM before using this mode.

### 6. Work with the display outside the lambda

Automations, button handlers, and Home Assistant triggers sometimes need to change the screen without re-running the display lambda. In that case, draw directly on the component, then push the framebuffer with `flush()` or `flush_region()`.

> [!WARNING]
> Don't use `update()` for this workflow — it re-runs the lambda first and overwrites everything you just drew.

Display calls such as `update()`, `update_region()`, `flush()`, and `flush_region()` schedule cooperative jobs and return immediately. The hardware work is progressed from ESPHome's `loop()` in small steps: rows are streamed to the panel, the BUSY pin is polled, and command delays are handled without blocking the main loop for the full refresh duration.

This matters because a Spectra 6 refresh can take several seconds. During that time ESPHome continues to handle WiFi, API traffic, sensors, and automations.

Only one display job can be active at a time. Starting a new job supersedes the previous request. `is_busy()` remains `true` while a job is active or pending.

> [!TIP]
> Treat display calls as asynchronous jobs. Use `is_busy()` when you need to know whether the display is still processing a previous request.

### 7. Draw directly, then push the framebuffer

`fill()`, `print()`, `image()`, and the other drawing functions modify the framebuffer in memory. They do not necessarily refresh the physical panel by themselves. After drawing directly on the component, use `flush()` or `flush_region()` to send the current framebuffer to the display.

Use `flush()` when the complete framebuffer should be transferred:

```yaml
button:
  - platform: template
    name: "Show alert"
    on_press:
      - lambda: |-
          id(epd).fill(Color(255, 255, 255));
          id(epd).print(600, 800, id(font_alert), Color(255, 0, 0),
                        TextAlign::CENTER, "ALERT");
          id(epd).flush();
```

Use `flush_region()` when you know exactly which area changed:

```yaml
button:
  - platform: template
    name: "Update status"
    on_press:
      - lambda: |-
          id(epd).filled_rectangle(100, 700, 500, 200, Color(255, 255, 255));
          id(epd).print(350, 800, id(font_status), Color(0, 0, 0),
                        TextAlign::CENTER, "Updated");
          id(epd).flush_region(100, 700, 500, 200);
```

### 8. Detect a changed region and flush it manually

When you draw outside the lambda but do not want to calculate the bounds yourself, reset the change tracker, draw the new content, ask the driver for the detected region, and pass that region to `flush_region()`:

```yaml
button:
  - platform: template
    name: "Auto-detect status region"
    on_press:
      - lambda: |-
          id(epd).reset_change_tracking();
          id(epd).filled_rectangle(100, 700, 500, 200, Color(255, 255, 255));
          id(epd).print(350, 800, id(font_status), Color(0, 0, 0),
                        TextAlign::CENTER, "Updated");

          auto region = id(epd).detect_changed_region();
          if (!region.empty()) {
            id(epd).flush_region(region.x, region.y, region.width, region.height);
          }
```


> [!NOTE]
> `reset_change_tracking()` clears any region accumulated from previous draw operations. Call it before drawing to ensure `detect_changed_region()` returns only the bounds of what you just changed.

> [!TIP]
> See [configs/clock.yaml](configs/clock.yaml) for a complete partial-update example.

---

### 9. Cancel or defer display work deliberately

`cancel()` requests cancellation of the current or pending display operation. The actual teardown is deferred to `loop()` so the driver can keep panel access safe:

| Current stage | What `cancel()` does |
|---|---|
| Data transfer | Aborts the job on the next `loop()` call |
| Hardware refresh (BUSY pin LOW) | Waits until the panel finishes its physical refresh before tearing down |

SPI commands are never sent to a busy panel. In both cases, `is_busy()` stays `true` until teardown is complete. It is safe to call `cancel()` when no job is active.

```yaml
button:
  - platform: template
    name: "Abort refresh"
    on_press:
      - lambda: id(epd).cancel();
```

When you want to start a new refresh only after the previous one has completed, poll `is_busy()` from an automation instead of blocking inside a lambda:

```yaml
interval:
  - interval: 60s
    then:
      - lambda: |-
          if (!id(epd).is_busy()) {
            id(epd).update();
          }
```

> [!WARNING]
> Avoid blocking on `is_busy()` in a tight loop — this starves the ESPHome main loop. The cooperative pipeline is designed to be polled, not awaited.

---

## 📂 Example Configurations

This repository includes several ready-to-use configurations:

| Config | Description |
|--------|-------------|
| [hello-world.yaml](configs/hello-world.yaml) | Minimal "HELLO WORLD" in two colours |
| [clock.yaml](configs/clock.yaml) | Clock with status, onboarding, display pages, and manual refresh triggers |
| [image.yaml](configs/image.yaml) | Full-screen image display |
| [test-sheet.yaml](configs/test-sheet.yaml) | Panel calibration reference sheet |

---

## 🖼️ Image Conversion

This repository includes `scripts/convert_image.py`, an optional CLI that converts a source image into a PNG that uses only the Spectra 6 panel palette: **black**, **white**, **yellow**, **red**, **blue**, and **green**.

### Command synopsis

```bash
python scripts/convert_image.py INPUT [OUTPUT] [options]
python scripts/convert_image.py --guided
python scripts/convert_image.py --list-presets
```

If `OUTPUT` is omitted, the script writes a PNG to `configs/images/` and generates a filename from the active settings. `--output` takes precedence over the positional `OUTPUT` argument.

If `INPUT` is omitted, the script enters guided mode automatically.

### Core arguments

| Option | Description |
|--------|-------------|
| `INPUT` | Source image path |
| `OUTPUT` | Optional positional output path; defaults to an auto-generated file under `configs/images/` |
| `-o`, `--output PATH` | Explicit output path; overrides positional `OUTPUT` |
| `--guided` | Interactive mode; prompts for all settings step by step |
| `--list-presets` | Prints the available presets and exits |
| `--preset {default,vivid,graphics,accurate}` | Applies a preset as the base configuration; explicit flags still override preset values |

### Canvas and quantization options

| Option | Default | Description |
|--------|---------|-------------|
| `--width INT` | `1200` | Target canvas width |
| `--height INT` | `1600` | Target canvas height |
| `--fit {cover,contain,stretch}` | `cover` | Resize strategy before palette conversion |
| `--rotate {0,90,180,270}` | `0` | Extra rotation after EXIF orientation is normalised |
| `--background {black,white,yellow,red,blue,green}` | `white` | Fill colour for letterboxing when `--fit contain` is used |
| `--dither {none,floyd-steinberg,atkinson}` | `atkinson` | Dithering algorithm used during palette reduction |

### Adjustment options

| Option | Default | Description |
|--------|---------|-------------|
| `--brightness FLOAT` | `1.0` | Brightness multiplier |
| `--contrast FLOAT` | `1.0` | Contrast multiplier |
| `--saturation FLOAT` | `1.0` | Saturation multiplier |
| `--sharpness FLOAT` | `1.0` | Sharpness multiplier |
| `--edge-enhance` / `--no-edge-enhance` | `off` | Enables or disables PIL edge enhancement |
| `--smooth` / `--no-smooth` | `off` | Enables or disables PIL smoothing |
| `--filter-sharpen` / `--no-filter-sharpen` | `off` | Enables or disables PIL sharpening |
| `--detail-stack` | `off` | Enables `edge-enhance`, `smooth`, and `filter-sharpen` together |

### Example commands

```bash
python scripts/convert_image.py image.jpg
python scripts/convert_image.py image.jpg hero.png --preset graphics
python scripts/convert_image.py image.jpg --fit contain --background black --output configs/images/poster.png
python scripts/convert_image.py image.jpg --preset vivid --contrast 1.20
python scripts/convert_image.py screenshot.png --preset accurate --sharpness 1.02
```

### Use presets for common inputs

Presets provide recommended starting points for common source material. The fixed Spectra 6 palette still stylises results, especially for photographic inputs, so treat presets as defaults to start from rather than universal optima. Explicit flags still override preset values.

```bash
python scripts/convert_image.py image.jpg --preset default
python scripts/convert_image.py image.jpg --preset vivid
python scripts/convert_image.py poster.png --preset graphics
python scripts/convert_image.py screenshot.png --preset accurate
python scripts/convert_image.py image.jpg --preset vivid --saturation 1.20
```

| Preset | Best for | Goal |
|--------|----------|------|
| `default` | Most photos and mixed-content images | Best starting point for most images with a modest boost for the panel |
| `vivid` | Colorful photos, neon scenes, and hero artwork | Stronger contrast and saturation when the image should pop |
| `graphics` | Posters, icons, illustrations, and UI mockups | Clean shapes, stable palette mapping, and low flat-area noise |
| `accurate` | Screenshots, labels, charts, diagrams, and fidelity-first images | Best color fidelity and clear detail reproduction with restrained enhancement |

Manual flags always win over preset defaults. For example, `--preset vivid --saturation 1.20` starts from the `vivid` recipe and only overrides saturation.

### Guided mode

```bash
python scripts/convert_image.py --guided
```

Guided mode walks through the available settings interactively. It is helpful when exploring conversion options without memorising command-line flags.

For the display-side workflow after conversion, see [Prepare images before displaying them](#3-prepare-images-before-displaying-them).

---

## 🤝 Contributing

Contributions are welcome. Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

---

<div align="center">

**[⬆ Back to top](#esphome-epaper-spectra6-133)**

</div>
