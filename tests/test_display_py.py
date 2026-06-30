"""Unit tests for the display.py ESPHome code-generation module.

These tests exercise the _migrate_update_mode() compatibility shim that was
introduced to allow configs using the deprecated ``update_mode`` YAML option
to continue working while the replacement ``refresh_mode`` key becomes the
canonical form.
"""

from __future__ import annotations

import importlib.util
import logging
import sys
import types
import unittest
from pathlib import Path
from unittest.mock import MagicMock


ROOT_DIR = Path(__file__).resolve().parents[1]
COMPONENT_DIR = ROOT_DIR / "components" / "epaper_spectra6_133"


def _make_esphome_stubs() -> None:
    """Insert lightweight stub modules for every esphome.* import used by the component."""

    class _Invalid(Exception):
        """Stub for esphome.config_validation.Invalid."""

        def __init__(self, message: str = "") -> None:
            super().__init__(message)

    cv_stub = types.ModuleType("esphome.config_validation")
    cv_stub.Invalid = _Invalid  # type: ignore[attr-defined]
    cv_stub.string_strict = str  # type: ignore[attr-defined]
    cv_stub.boolean = bool  # type: ignore[attr-defined]
    cv_stub.Optional = MagicMock(return_value=MagicMock())  # type: ignore[attr-defined]
    cv_stub.Required = MagicMock(return_value=MagicMock())  # type: ignore[attr-defined]
    cv_stub.GenerateID = MagicMock(return_value=MagicMock())  # type: ignore[attr-defined]
    cv_stub.declare_id = MagicMock(return_value=MagicMock())  # type: ignore[attr-defined]
    cv_stub.All = MagicMock(side_effect=lambda *args: args[-1] if args else None)
    cv_stub.one_of = MagicMock(return_value=MagicMock())  # type: ignore[attr-defined]
    cv_stub.polling_component_schema = MagicMock(return_value=MagicMock())

    cg_stub = types.ModuleType("esphome.codegen")
    ns_mock = MagicMock()
    cg_stub.esphome_ns = ns_mock  # type: ignore[attr-defined]
    cg_stub.RawExpression = MagicMock(side_effect=lambda x: x)  # type: ignore[attr-defined]
    cg_stub.new_Pvariable = MagicMock()  # type: ignore[attr-defined]
    cg_stub.add = MagicMock()  # type: ignore[attr-defined]
    cg_stub.void = MagicMock()  # type: ignore[attr-defined]
    cg_stub.process_lambda = MagicMock()  # type: ignore[attr-defined]

    pins_stub = types.ModuleType("esphome.pins")
    pins_stub.internal_gpio_output_pin_number = MagicMock()  # type: ignore[attr-defined]
    pins_stub.internal_gpio_input_pin_number = MagicMock()  # type: ignore[attr-defined]

    display_stub = types.ModuleType("esphome.components.display")
    display_stub.FULL_DISPLAY_SCHEMA = MagicMock()  # type: ignore[attr-defined]
    display_stub.FULL_DISPLAY_SCHEMA.extend = MagicMock(return_value=MagicMock())
    display_stub.DisplayBuffer = object  # type: ignore[attr-defined]
    display_stub.DisplayRef = MagicMock()  # type: ignore[attr-defined]
    display_stub.register_display = MagicMock()  # type: ignore[attr-defined]

    esphome_components_stub = types.ModuleType("esphome.components")
    esphome_stub = types.ModuleType("esphome")
    const_stub = types.ModuleType("esphome.const")
    const_stub.CONF_ID = "id"  # type: ignore[attr-defined]
    const_stub.CONF_LAMBDA = "lambda"  # type: ignore[attr-defined]

    sys.modules.update(
        {
            "esphome": esphome_stub,
            "esphome.codegen": cg_stub,
            "esphome.config_validation": cv_stub,
            "esphome.pins": pins_stub,
            "esphome.components": esphome_components_stub,
            "esphome.components.display": display_stub,
            "esphome.const": const_stub,
        }
    )


def _load_component_module(rel_path: str, module_name: str) -> types.ModuleType:
    """Load a component Python file from disk as a standalone module."""
    abs_path = COMPONENT_DIR / rel_path
    spec = importlib.util.spec_from_file_location(module_name, abs_path)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Cannot load {abs_path}")
    mod = importlib.util.module_from_spec(spec)
    sys.modules[module_name] = mod
    spec.loader.exec_module(mod)  # type: ignore[union-attr]
    return mod


# Install stubs before importing component modules.
_make_esphome_stubs()

_init_mod = _load_component_module("__init__.py", "epaper_spectra6_133")
_display_mod = _load_component_module("display.py", "epaper_spectra6_133.display")

_migrate_update_mode = _display_mod._migrate_update_mode
CONF_UPDATE_MODE = _init_mod.CONF_UPDATE_MODE  # "update_mode"
CONF_REFRESH_MODE = _init_mod.CONF_REFRESH_MODE  # "refresh_mode"
cv_Invalid = sys.modules["esphome.config_validation"].Invalid


class TestMigrateUpdateMode(unittest.TestCase):
    """Tests for the _migrate_update_mode() compatibility shim in display.py."""

    def test_no_update_mode_returns_config_unchanged(self) -> None:
        """If update_mode is absent the config dict is returned as-is."""
        config = {CONF_REFRESH_MODE: "full", "some_other_key": 42}
        result = _migrate_update_mode(config)
        self.assertIs(result, config)
        self.assertEqual(result, {CONF_REFRESH_MODE: "full", "some_other_key": 42})

    def test_update_mode_mapped_to_refresh_mode(self) -> None:
        """update_mode is moved to refresh_mode and the key is removed."""
        config = {CONF_UPDATE_MODE: "full"}
        result = _migrate_update_mode(config)
        self.assertIn(CONF_REFRESH_MODE, result)
        self.assertEqual(result[CONF_REFRESH_MODE], "full")
        self.assertNotIn(CONF_UPDATE_MODE, result)

    def test_update_mode_partial_mapped_to_refresh_mode_partial(self) -> None:
        """The value 'partial' is preserved during migration."""
        config = {CONF_UPDATE_MODE: "partial"}
        result = _migrate_update_mode(config)
        self.assertEqual(result[CONF_REFRESH_MODE], "partial")
        self.assertNotIn(CONF_UPDATE_MODE, result)

    def test_migration_does_not_mutate_original_dict(self) -> None:
        """The returned config is a copy; the original must be untouched."""
        original = {CONF_UPDATE_MODE: "full", "other": "value"}
        original_copy = dict(original)
        _migrate_update_mode(original)
        self.assertEqual(original, original_copy)

    def test_both_update_mode_and_refresh_mode_raises_invalid(self) -> None:
        """Simultaneous use of update_mode and refresh_mode is an error."""
        config = {CONF_UPDATE_MODE: "full", CONF_REFRESH_MODE: "partial"}
        with self.assertRaises(cv_Invalid):
            _migrate_update_mode(config)

    def test_conflict_error_mentions_both_option_names(self) -> None:
        """The Invalid exception message must name both conflicting options."""
        config = {CONF_UPDATE_MODE: "partial", CONF_REFRESH_MODE: "full"}
        with self.assertRaises(cv_Invalid) as ctx:
            _migrate_update_mode(config)
        msg = str(ctx.exception)
        self.assertIn("update_mode", msg)
        self.assertIn("refresh_mode", msg)

    def test_update_mode_logs_deprecation_warning(self) -> None:
        """A WARNING-level log message must be emitted when update_mode is used."""
        config = {CONF_UPDATE_MODE: "full"}
        with self.assertLogs(
            "epaper_spectra6_133.display", level=logging.WARNING
        ) as log_ctx:
            _migrate_update_mode(config)
        self.assertTrue(
            any("update_mode" in record for record in log_ctx.output),
            "Expected a warning mentioning 'update_mode' in the log output",
        )

    def test_no_warning_logged_when_update_mode_absent(self) -> None:
        """No log output must be emitted for configs that don't use update_mode."""
        config = {CONF_REFRESH_MODE: "full"}
        logger = logging.getLogger("epaper_spectra6_133.display")
        with self.assertNoLogs(logger, level=logging.WARNING):
            _migrate_update_mode(config)

    def test_empty_config_returned_unchanged(self) -> None:
        """An empty config must pass through without error."""
        config: dict = {}
        result = _migrate_update_mode(config)
        self.assertIs(result, config)
        self.assertEqual(result, {})

    def test_other_keys_preserved_during_migration(self) -> None:
        """Unrelated keys must survive the update_mode to refresh_mode migration."""
        config = {CONF_UPDATE_MODE: "partial", "cs0_pin": 18, "auto_sleep": True}
        result = _migrate_update_mode(config)
        self.assertEqual(result["cs0_pin"], 18)
        self.assertEqual(result["auto_sleep"], True)
        self.assertEqual(result[CONF_REFRESH_MODE], "partial")
        self.assertNotIn(CONF_UPDATE_MODE, result)


if __name__ == "__main__":
    unittest.main()
