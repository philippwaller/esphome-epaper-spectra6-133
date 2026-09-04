# Seeed Studio XIAO ePaper Display Board EE02

The XIAO ePaper Display Board EE02 is a Seeed Studio driver board for 13.3-inch
Spectra 6 panels. It carries a soldered XIAO ESP32-S3 Plus module, a 60-pin FPC
connector for the panel, a battery connector with power switch, and three user
buttons.

## Hardware overview

| Feature | Details | Notes |
| --- | --- | --- |
| Display | 13.3-inch Spectra 6 | 1200 × 1600 pixels, dual controller |
| Processor | XIAO ESP32-S3 Plus | ESP-IDF only |
| Memory | 16 MB flash, 8 MB PSRAM | PSRAM holds the approximately 960 KB framebuffer |
| Display connector | FPC, 60 pin, 0.5 mm pitch | Connects the panel ribbon directly |
| Onboard buttons | 1 reset, 1 boot, 3 user buttons | GPIO mapping provided for the user buttons |
| Battery | JST 2.0 mm, 2 pin, with slide switch | Charging IC onboard, voltage sense via ADC |
| Power and programming | USB-C | Native USB only; there is no UART bridge |

## Important notes

- The USB-C port is wired to the ESP32-S3 native USB peripheral. Flash the first
  ESPHome build over USB and use OTA afterward. The package configures the
  logger for `USB_SERIAL_JTAG`.
- UART0 is not usable for logging: GPIO43 and GPIO44 are wired to the panel
  power switch and the left chip select.
- GPIO43 drives a load switch that powers the panel interface rail. The switch
  has no external pull-down, so leaving the pin unconfigured produces
  unpredictable results. Always pass `power_pin: ${epd_power_pin}` to the
  display component.
- GPIO10, GPIO39, and GPIO42 are reserved for the display interface even though
  the current display component does not use them.
- Reading the battery voltage requires two pins: drive GPIO6 high to enable the
  divider, then read GPIO1. Keep GPIO6 low the rest of the time so the divider
  does not drain the battery.

## Pin overview

All GPIOs listed here are connected or reserved by the board. Do not treat them
as free expansion pins.

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
  <td>GPIO7</td>
  <td>SPI clock</td>
</tr>
<tr><td>DATA0</td><td>GPIO9</td><td>SPI data line 0 (MOSI)</td></tr>
<tr><td>DATA1</td><td>GPIO8</td><td>SPI data line 1</td></tr>
<tr><td>Left CS</td><td>GPIO44</td><td>Chip select for left controller IC</td></tr>
<tr><td>Right CS</td><td>GPIO41</td><td>Chip select for right controller IC</td></tr>
<tr><td>BUSY</td><td>GPIO4</td><td>Shared busy signal from both controller ICs</td></tr>
<tr><td>RESET</td><td>GPIO38</td><td>Hardware reset for both controller ICs</td></tr>
<tr><td>POWER</td><td>GPIO43</td><td>Enables the load switch that powers the panel rail</td></tr>
<tr>
  <td rowspan="3">Additional display signals</td>
  <td>D/C</td>
  <td>GPIO10</td>
  <td>Command/data select; reserved — not used by the current SPI transport</td>
</tr>
<tr><td>DATA2</td><td>GPIO39</td><td>Quad-SPI data line 2; reserved — not used by the current transport</td></tr>
<tr><td>DATA3</td><td>GPIO42</td><td>Quad-SPI data line 3; reserved — not used by the current transport</td></tr>
<tr>
  <td rowspan="3">User buttons</td>
  <td>Button 1</td>
  <td>GPIO2</td>
  <td>Onboard button; pulled up, reads LOW when pressed</td>
</tr>
<tr><td>Button 2</td><td>GPIO3</td><td>Onboard button; pulled up, reads LOW when pressed</td></tr>
<tr><td>Button 3</td><td>GPIO5</td><td>Onboard button; pulled up, reads LOW when pressed</td></tr>
<tr>
  <td rowspan="2">Battery</td>
  <td>BAT ADC</td>
  <td>GPIO1</td>
  <td>Battery voltage divider output</td>
</tr>
<tr><td>ADC enable</td><td>GPIO6</td><td>Drive HIGH to enable the divider before reading GPIO1</td></tr>
<tr>
  <td rowspan="2">Native USB</td>
  <td>D−</td>
  <td>GPIO19</td>
  <td>USB D− (fixed ESP32-S3 assignment; used by the USB-C connector)</td>
</tr>
<tr><td>D+</td><td>GPIO20</td><td>USB D+ (fixed ESP32-S3 assignment; used by the USB-C connector)</td></tr>
</tbody>
</table>

GPIO11, GPIO12, and GPIO13 form a second SPI bus that serves the onboard
GT32L32S0140 font chip and an additional flash chip. GPIO26 through GPIO37
connect the ESP32-S3 to flash and PSRAM and must not be used for external
hardware. Other unlisted GPIOs are not documented as available expansion pins.

## Package import

<!-- x-release-please-start-version -->
```yaml
packages:
  board: github://philippwaller/esphome-epaper-spectra6-133/packages/boards/seeed_studio/xiao_ee02.yaml@v0.4.0
```
<!-- x-release-please-end -->

## Examples

- [Minimal display configuration](../../configs/hello-world.yaml)
- [Clock with scheduled updates](../../configs/clock.yaml)
- [Image rendering configuration](../../configs/image.yaml)

The example configs default to the Good Display board pins. Replace their
`board:` package entry with the EE02 package above to run them on this board.

## Sources

Based on the Seeed Studio EE02 wiki, the EE02 V1.0 schematic, and the XIAO
ESP32-S3 Plus documentation.
