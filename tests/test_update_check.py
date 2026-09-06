"""Regression test for the self-contained update-check package."""

import unittest
from pathlib import Path

import yaml

PACKAGE_FILE = Path(__file__).resolve().parents[1] / "packages" / "update_check.yaml"
SCRIPT_ID = "epaper_spectra6_133_check_for_updates"
LOGGER_TAG = "epaper_update_check"
CURRENT_VERSION_ID = "epaper_spectra6_133_current_version"


class UpdateCheckPackageTests(unittest.TestCase):
    """Verify that importing the package is enough to schedule safe checks."""

    def test_checks_on_connect_and_only_while_wifi_is_connected(self) -> None:
        config = yaml.safe_load(PACKAGE_FILE.read_text(encoding="utf-8"))

        self.assertNotIn("esphome", config)
        self.assertEqual(
            config["wifi"]["on_connect"],
            [{"delay": "1s"}, {"script.execute": SCRIPT_ID}],
        )

        script = config["script"][0]
        self.assertEqual(script["id"], SCRIPT_ID)
        self.assertEqual(script["mode"], "single")
        self.assertEqual(script["then"][0]["if"]["condition"], {"wifi.connected": None})

        connected_actions = script["then"][0]["if"]["then"]
        self.assertEqual(connected_actions[0], {"component.update": CURRENT_VERSION_ID})
        self.assertEqual(
            connected_actions[2]["http_request.get"]["max_response_buffer_size"],
            32768,
        )

        self.assertEqual(config["interval"][0]["startup_delay"], "6h")

    def test_enables_info_logging_for_update_checks(self) -> None:
        config = yaml.safe_load(PACKAGE_FILE.read_text(encoding="utf-8"))

        self.assertEqual(config["logger"]["logs"][LOGGER_TAG], "INFO")

    def test_logs_update_check_outcomes(self) -> None:
        source = PACKAGE_FILE.read_text(encoding="utf-8")

        for message in (
            "Checking for component updates",
            "Update check completed",
            "Update check skipped: WiFi is not connected",
            "Update check failed",
        ):
            self.assertIn(message, source)

    def test_exposes_manual_update_check_button(self) -> None:
        config = yaml.safe_load(PACKAGE_FILE.read_text(encoding="utf-8"))

        button = config["button"][0]
        self.assertEqual(button["platform"], "template")
        self.assertEqual(button["name"], "Check for Updates")
        self.assertEqual(button["entity_category"], "diagnostic")
        self.assertEqual(button["on_press"], [{"script.execute": SCRIPT_ID}])


if __name__ == "__main__":
    unittest.main()
