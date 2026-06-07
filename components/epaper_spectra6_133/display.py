"""ESPHome display platform schema and code generation for this component."""

import esphome.codegen as cg
from esphome import pins
from esphome.components import display as display_core
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_LAMBDA

from . import (
    CONF_AUTO_SLEEP,
    CONF_BUSY_PIN,
    CONF_CHANGE_DETECTION_MODE,
    CONF_CLK_PIN,
    CONF_CS0_PIN,
    CONF_CS1_PIN,
    CONF_DATA0_PIN,
    CONF_DATA1_PIN,
    CONF_POWER_PIN,
    CONF_POWER_OFF_AFTER_SLEEP,
    CONF_RESET_PIN,
    CONF_SPI_HOST,
    CONF_UPDATE_MODE,
    EpaperSpectra6133,
    _validate_spi_host,
)

AUTO_LOAD = ["display"]
DEPENDENCIES = ["esp32"]

_CHANGE_DETECTION_MODES = {
    "track": cg.RawExpression(
        "esphome::epaper_spectra6_133::ChangeDetectionMode::TRACK"
    ),
    "compare": cg.RawExpression(
        "esphome::epaper_spectra6_133::ChangeDetectionMode::COMPARE"
    ),
}

_UPDATE_MODES = {
    "full": cg.RawExpression("esphome::epaper_spectra6_133::UpdateMode::FULL"),
    "partial": cg.RawExpression("esphome::epaper_spectra6_133::UpdateMode::PARTIAL"),
}

CONFIG_SCHEMA = display_core.FULL_DISPLAY_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(EpaperSpectra6133),
        cv.Optional(CONF_SPI_HOST, default="SPI3_HOST"): _validate_spi_host,
        cv.Required(CONF_CS0_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_CS1_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_CLK_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_DATA0_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_DATA1_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_BUSY_PIN): pins.internal_gpio_input_pin_number,
        cv.Required(CONF_RESET_PIN): pins.internal_gpio_output_pin_number,
        cv.Required(CONF_POWER_PIN): pins.internal_gpio_output_pin_number,
        cv.Optional(CONF_CHANGE_DETECTION_MODE, default="track"): cv.one_of(
            *_CHANGE_DETECTION_MODES, lower=True
        ),
        cv.Optional(CONF_UPDATE_MODE, default="full"): cv.one_of(
            *_UPDATE_MODES, lower=True
        ),
        cv.Optional(CONF_AUTO_SLEEP, default=True): cv.boolean,
        cv.Optional(CONF_POWER_OFF_AFTER_SLEEP, default=False): cv.boolean,
    }
).extend(cv.polling_component_schema("never"))


async def to_code(config):
    """Generate C++ setup code for one ``epaper_spectra6_133`` display."""
    var = cg.new_Pvariable(config[CONF_ID])
    await display_core.register_display(var, config)

    cg.add(var.set_spi_host(config[CONF_SPI_HOST]))
    cg.add(var.set_cs0_pin(config[CONF_CS0_PIN]))
    cg.add(var.set_cs1_pin(config[CONF_CS1_PIN]))
    cg.add(var.set_clk_pin(config[CONF_CLK_PIN]))
    cg.add(var.set_data0_pin(config[CONF_DATA0_PIN]))
    cg.add(var.set_data1_pin(config[CONF_DATA1_PIN]))
    cg.add(var.set_busy_pin(config[CONF_BUSY_PIN]))
    cg.add(var.set_reset_pin(config[CONF_RESET_PIN]))
    cg.add(var.set_power_pin(config[CONF_POWER_PIN]))

    cg.add(
        var.set_change_detection_mode(
            _CHANGE_DETECTION_MODES[config[CONF_CHANGE_DETECTION_MODE]]
        )
    )
    cg.add(var.set_update_mode(_UPDATE_MODES[config[CONF_UPDATE_MODE]]))
    cg.add(var.set_auto_sleep(config[CONF_AUTO_SLEEP]))
    cg.add(var.set_power_off_after_sleep(config[CONF_POWER_OFF_AFTER_SLEEP]))

    if CONF_LAMBDA in config:
        lambda_ = await cg.process_lambda(
            config[CONF_LAMBDA], [(display_core.DisplayRef, "it")], return_type=cg.void
        )
        if lambda_ is not None:
            cg.add(var.set_writer(lambda_))
