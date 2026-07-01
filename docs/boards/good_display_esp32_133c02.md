# Good Display ESP32-133C02

The Good Display ESP32-133C02 is an ESP32-S3 controller board for
GDEP133C02-compatible 13.3-inch Spectra 6 displays. Ref1 and Ref2 share the same
base hardware; Ref2 adds three onboard buttons.

## Choose your revision

Both revisions use the same product name. Check the physical board if you plan
to use its onboard buttons.

| Visible hardware | Revision | ESPHome package |
| --- | --- | --- |
| No buttons labelled SW2, SW3, and SW4 | Ref1 | `esp32_133c02.yaml` |
| Three buttons labelled SW2, SW3, and SW4 | Ref2 | `esp32_133c02_ref2.yaml` |

The display component works identically with Ref1 and Ref2. The Ref1 package is
sufficient on either revision when the onboard buttons are not used. You only
need the Ref2 package if you want to use SW2, SW3, or SW4; it adds their GPIO
mapping as a convenient hardware reference and does not change display support.

## Hardware overview

| Feature | Ref1 | Ref2 | Notes |
| --- | --- | --- | --- |
| Display | 13.3-inch Spectra 6 | Same | 1200 × 1600 pixels, dual controller |
| Processor | ESP32-S3 | Same | ESP-IDF only |
| Memory | 16 MB flash, 8 MB PSRAM | Same | PSRAM holds the approximately 960 KB framebuffer |
| microSD slot | Yes | Yes | GPIO mapping provided; requires additional configuration |
| Onboard buttons | No | SW2, SW3, SW4 | Ref2 only; GPIO mapping provided |
| Power and programming | USB-C and UART0 | Same | UART0 logger runs at 115200 baud |

## Important notes

- Ref2 imports the Ref1 package and adds only the three button GPIOs.
- The microSD slot is wired to a separate SPI bus, but the package does not
  mount the card or select a filesystem.
- GPIO3 and GPIO45 are boot-strapping pins. External circuitry must not force
  them to an incompatible level while the board starts. ESPHome may log a
  warning when these pins are configured; to suppress it, add
  `ignore_strapping_warning: true` to the relevant pin configuration.
  See the [ESPHome FAQ on strapping pins](https://esphome.io/guides/faq/#why-am-i-getting-a-warning-about-strapping-pins).
- GPIO2, GPIO38, and GPIO39 are reserved for the display interface even though
  the current display component does not use them.

## Pin overview

All GPIOs listed here are connected or reserved by the board packages. Do not
treat them as free expansion pins.

<table>
<colgroup>
  <col style="width:22%">
  <col style="width:14%">
  <col style="width:12%">
  <col style="width:52%">
</colgroup>
<thead>
<tr>
  <th>Function</th>
  <th>Signal</th>
  <th>GPIO</th>
  <th>Description</th>
</tr>
</thead>
<tbody>
<tr>
  <td rowspan="8">Display</td>
  <td>CLK</td>
  <td>GPIO9</td>
  <td>SPI clock</td>
</tr>
<tr><td>DATA0</td><td>GPIO41</td><td>SPI data line 0 (MOSI)</td></tr>
<tr><td>DATA1</td><td>GPIO40</td><td>SPI data line 1</td></tr>
<tr><td>Left CS</td><td>GPIO18</td><td>Chip select for left controller IC</td></tr>
<tr><td>Right CS</td><td>GPIO17</td><td>Chip select for right controller IC</td></tr>
<tr><td>BUSY</td><td>GPIO7</td><td>Shared busy signal from both controller ICs</td></tr>
<tr><td>RESET</td><td>GPIO6</td><td>Hardware reset for both controller ICs</td></tr>
<tr><td>POWER</td><td>GPIO45</td><td>Panel power enable (boot-strapping pin — must not be forced low at startup)</td></tr>
<tr>
  <td rowspan="3">Additional display signals</td>
  <td>D/C</td>
  <td>GPIO2</td>
  <td>Command/data select; reserved — not used by the current SPI transport (boot-strapping pin)</td>
</tr>
<tr><td>DATA2</td><td>GPIO39</td><td>Quad-SPI data line 2; reserved — not used by the current transport</td></tr>
<tr><td>DATA3</td><td>GPIO38</td><td>Quad-SPI data line 3; reserved — not used by the current transport</td></tr>
<tr>
  <td rowspan="4">microSD</td>
  <td>CLK</td>
  <td>GPIO8</td>
  <td>SPI clock for the onboard SD card slot</td>
</tr>
<tr><td>MOSI</td><td>GPIO3</td><td>SPI data out to SD card (boot-strapping pin)</td></tr>
<tr><td>MISO</td><td>GPIO5</td><td>SPI data in from SD card</td></tr>
<tr><td>CS</td><td>GPIO15</td><td>Chip select for SD card</td></tr>
<tr>
  <td rowspan="3">Buttons <small>(Ref2 only)</small></td>
  <td>SW2</td>
  <td>GPIO12</td>
  <td>Onboard button; reads HIGH when pressed</td>
</tr>
<tr><td>SW3</td><td>GPIO13</td><td>Onboard button; reads HIGH when pressed</td></tr>
<tr><td>SW4</td><td>GPIO14</td><td>Onboard button; reads HIGH when pressed</td></tr>
<tr>
  <td rowspan="2">Native USB</td>
  <td>D−</td>
  <td>GPIO19</td>
  <td>USB D− (fixed ESP32-S3 assignment; not enabled by the package)</td>
</tr>
<tr><td>D+</td><td>GPIO20</td><td>USB D+ (fixed ESP32-S3 assignment; not enabled by the package)</td></tr>
<tr>
  <td rowspan="2">UART0</td>
  <td>TX</td>
  <td>GPIO43</td>
  <td>Serial console transmit; used for programming and logs at 115200 baud</td>
</tr>
<tr><td>RX</td><td>GPIO44</td><td>Serial console receive</td></tr>
</tbody>
</table>

GPIO26 through GPIO37 connect the ESP32-S3 module to flash and PSRAM and must
not be used for external hardware. Other unlisted GPIOs are not documented as
available expansion pins.

## Package import

Use the package matching the physical revision.

### Ref1

<!-- x-release-please-start-version -->
```yaml
packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/good_display/esp32_133c02.yaml@v0.4.0
```
<!-- x-release-please-end -->

### Ref2

<!-- x-release-please-start-version -->
```yaml
packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/good_display/esp32_133c02_ref2.yaml@v0.4.0
```
<!-- x-release-please-end -->

## Examples

- [Minimal display configuration](../../configs/hello-world.yaml)
- [Clock with scheduled updates](../../configs/clock.yaml)
- [Image rendering configuration](../../configs/image.yaml)

## Sources

Based on the Good Display product documentation, schematic, ESP-IDF example,
and GDEP133C02 specification, plus the ESP32-S3-WROOM-1 and TPS22913B
datasheets.
